#include <futurehead/lib/threading.hpp>
#include <futurehead/node/bootstrap/bootstrap.hpp>
#include <futurehead/node/bootstrap/bootstrap_attempt.hpp>
#include <futurehead/node/bootstrap/bootstrap_lazy.hpp>
#include <futurehead/node/common.hpp>
#include <futurehead/node/node.hpp>

#include <boost/format.hpp>

#include <algorithm>

futurehead::bootstrap_initiator::bootstrap_initiator (futurehead::node & node_a) :
node (node_a)
{
	connections = std::make_shared<futurehead::bootstrap_connections> (node);
	bootstrap_initiator_threads.push_back (boost::thread ([this]() {
		futurehead::thread_role::set (futurehead::thread_role::name::bootstrap_connections);
		connections->run ();
	}));
	for (size_t i = 0; i < node.config.bootstrap_initiator_threads; ++i)
	{
		bootstrap_initiator_threads.push_back (boost::thread ([this]() {
			futurehead::thread_role::set (futurehead::thread_role::name::bootstrap_initiator);
			run_bootstrap ();
		}));
	}
}

futurehead::bootstrap_initiator::~bootstrap_initiator ()
{
	stop ();
}

void futurehead::bootstrap_initiator::bootstrap (bool force, std::string id_a)
{
	if (force)
	{
		stop_attempts ();
	}
	futurehead::unique_lock<std::mutex> lock (mutex);
	if (!stopped && find_attempt (futurehead::bootstrap_mode::legacy) == nullptr)
	{
		node.stats.inc (futurehead::stat::type::bootstrap, futurehead::stat::detail::initiate, futurehead::stat::dir::out);
		auto legacy_attempt (std::make_shared<futurehead::bootstrap_attempt_legacy> (node.shared (), attempts.incremental++, id_a));
		attempts_list.push_back (legacy_attempt);
		attempts.add (legacy_attempt);
		lock.unlock ();
		condition.notify_all ();
	}
}

void futurehead::bootstrap_initiator::bootstrap (futurehead::endpoint const & endpoint_a, bool add_to_peers, bool frontiers_confirmed, std::string id_a)
{
	if (add_to_peers)
	{
		if (!node.flags.disable_udp)
		{
			node.network.udp_channels.insert (futurehead::transport::map_endpoint_to_v6 (endpoint_a), node.network_params.protocol.protocol_version);
		}
		else if (!node.flags.disable_tcp_realtime)
		{
			node.network.merge_peer (futurehead::transport::map_endpoint_to_v6 (endpoint_a));
		}
	}
	if (!stopped)
	{
		stop_attempts ();
		node.stats.inc (futurehead::stat::type::bootstrap, futurehead::stat::detail::initiate, futurehead::stat::dir::out);
		futurehead::lock_guard<std::mutex> lock (mutex);
		auto legacy_attempt (std::make_shared<futurehead::bootstrap_attempt_legacy> (node.shared (), attempts.incremental++, id_a));
		attempts_list.push_back (legacy_attempt);
		attempts.add (legacy_attempt);
		if (frontiers_confirmed)
		{
			node.network.excluded_peers.remove (futurehead::transport::map_endpoint_to_tcp (endpoint_a));
		}
		if (!node.network.excluded_peers.check (futurehead::transport::map_endpoint_to_tcp (endpoint_a)))
		{
			connections->add_connection (endpoint_a);
		}
		legacy_attempt->frontiers_confirmed = frontiers_confirmed;
	}
	condition.notify_all ();
}

void futurehead::bootstrap_initiator::bootstrap_lazy (futurehead::hash_or_account const & hash_or_account_a, bool force, bool confirmed, std::string id_a)
{
	auto lazy_attempt (current_lazy_attempt ());
	if (lazy_attempt == nullptr || force)
	{
		if (force)
		{
			stop_attempts ();
		}
		node.stats.inc (futurehead::stat::type::bootstrap, futurehead::stat::detail::initiate_lazy, futurehead::stat::dir::out);
		futurehead::lock_guard<std::mutex> lock (mutex);
		if (!stopped && find_attempt (futurehead::bootstrap_mode::lazy) == nullptr)
		{
			lazy_attempt = std::make_shared<futurehead::bootstrap_attempt_lazy> (node.shared (), attempts.incremental++, id_a.empty () ? hash_or_account_a.to_string () : id_a);
			attempts_list.push_back (lazy_attempt);
			attempts.add (lazy_attempt);
			lazy_attempt->lazy_start (hash_or_account_a, confirmed);
		}
	}
	else
	{
		lazy_attempt->lazy_start (hash_or_account_a, confirmed);
	}
	condition.notify_all ();
}

void futurehead::bootstrap_initiator::bootstrap_wallet (std::deque<futurehead::account> & accounts_a)
{
	debug_assert (!accounts_a.empty ());
	auto wallet_attempt (current_wallet_attempt ());
	node.stats.inc (futurehead::stat::type::bootstrap, futurehead::stat::detail::initiate_wallet_lazy, futurehead::stat::dir::out);
	if (wallet_attempt == nullptr)
	{
		futurehead::lock_guard<std::mutex> lock (mutex);
		std::string id (!accounts_a.empty () ? accounts_a[0].to_account () : "");
		wallet_attempt = std::make_shared<futurehead::bootstrap_attempt_wallet> (node.shared (), attempts.incremental++, id);
		attempts_list.push_back (wallet_attempt);
		attempts.add (wallet_attempt);
		wallet_attempt->wallet_start (accounts_a);
	}
	else
	{
		wallet_attempt->wallet_start (accounts_a);
	}
	condition.notify_all ();
}

void futurehead::bootstrap_initiator::run_bootstrap ()
{
	futurehead::unique_lock<std::mutex> lock (mutex);
	while (!stopped)
	{
		if (has_new_attempts ())
		{
			auto attempt (new_attempt ());
			lock.unlock ();
			if (attempt != nullptr)
			{
				attempt->run ();
				remove_attempt (attempt);
			}
			lock.lock ();
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void futurehead::bootstrap_initiator::lazy_requeue (futurehead::block_hash const & hash_a, futurehead::block_hash const & previous_a, bool confirmed_a)
{
	auto lazy_attempt (current_lazy_attempt ());
	if (lazy_attempt != nullptr)
	{
		lazy_attempt->lazy_requeue (hash_a, previous_a, confirmed_a);
	}
}

void futurehead::bootstrap_initiator::add_observer (std::function<void(bool)> const & observer_a)
{
	futurehead::lock_guard<std::mutex> lock (observers_mutex);
	observers.push_back (observer_a);
}

bool futurehead::bootstrap_initiator::in_progress ()
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return !attempts_list.empty ();
}

std::shared_ptr<futurehead::bootstrap_attempt> futurehead::bootstrap_initiator::find_attempt (futurehead::bootstrap_mode mode_a)
{
	for (auto & i : attempts_list)
	{
		if (i->mode == mode_a)
		{
			return i;
		}
	}
	return nullptr;
}

void futurehead::bootstrap_initiator::remove_attempt (std::shared_ptr<futurehead::bootstrap_attempt> attempt_a)
{
	futurehead::unique_lock<std::mutex> lock (mutex);
	auto attempt (std::find (attempts_list.begin (), attempts_list.end (), attempt_a));
	if (attempt != attempts_list.end ())
	{
		attempts.remove ((*attempt)->incremental_id);
		attempts_list.erase (attempt);
		debug_assert (attempts.size () == attempts_list.size ());
	}
	lock.unlock ();
	condition.notify_all ();
}

std::shared_ptr<futurehead::bootstrap_attempt> futurehead::bootstrap_initiator::new_attempt ()
{
	for (auto & i : attempts_list)
	{
		if (!i->started.exchange (true))
		{
			return i;
		}
	}
	return nullptr;
}

bool futurehead::bootstrap_initiator::has_new_attempts ()
{
	for (auto & i : attempts_list)
	{
		if (!i->started)
		{
			return true;
		}
	}
	return false;
}

std::shared_ptr<futurehead::bootstrap_attempt> futurehead::bootstrap_initiator::current_attempt ()
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return find_attempt (futurehead::bootstrap_mode::legacy);
}

std::shared_ptr<futurehead::bootstrap_attempt> futurehead::bootstrap_initiator::current_lazy_attempt ()
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return find_attempt (futurehead::bootstrap_mode::lazy);
}

std::shared_ptr<futurehead::bootstrap_attempt> futurehead::bootstrap_initiator::current_wallet_attempt ()
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return find_attempt (futurehead::bootstrap_mode::wallet_lazy);
}

void futurehead::bootstrap_initiator::stop_attempts ()
{
	futurehead::unique_lock<std::mutex> lock (mutex);
	std::vector<std::shared_ptr<futurehead::bootstrap_attempt>> copy_attempts;
	copy_attempts.swap (attempts_list);
	attempts.clear ();
	lock.unlock ();
	for (auto & i : copy_attempts)
	{
		i->stop ();
	}
}

void futurehead::bootstrap_initiator::stop ()
{
	if (!stopped.exchange (true))
	{
		stop_attempts ();
		connections->stop ();
		condition.notify_all ();

		for (auto & thread : bootstrap_initiator_threads)
		{
			if (thread.joinable ())
			{
				thread.join ();
			}
		}
	}
}

void futurehead::bootstrap_initiator::notify_listeners (bool in_progress_a)
{
	futurehead::lock_guard<std::mutex> lock (observers_mutex);
	for (auto & i : observers)
	{
		i (in_progress_a);
	}
}

std::unique_ptr<futurehead::container_info_component> futurehead::collect_container_info (bootstrap_initiator & bootstrap_initiator, const std::string & name)
{
	size_t count;
	size_t cache_count;
	{
		futurehead::lock_guard<std::mutex> guard (bootstrap_initiator.observers_mutex);
		count = bootstrap_initiator.observers.size ();
	}
	{
		futurehead::lock_guard<std::mutex> guard (bootstrap_initiator.cache.pulls_cache_mutex);
		cache_count = bootstrap_initiator.cache.cache.size ();
	}

	auto sizeof_element = sizeof (decltype (bootstrap_initiator.observers)::value_type);
	auto sizeof_cache_element = sizeof (decltype (bootstrap_initiator.cache.cache)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "observers", count, sizeof_element }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "pulls_cache", cache_count, sizeof_cache_element }));
	return composite;
}

void futurehead::pulls_cache::add (futurehead::pull_info const & pull_a)
{
	if (pull_a.processed > 500)
	{
		futurehead::lock_guard<std::mutex> guard (pulls_cache_mutex);
		// Clean old pull
		if (cache.size () > cache_size_max)
		{
			cache.erase (cache.begin ());
		}
		debug_assert (cache.size () <= cache_size_max);
		futurehead::uint512_union head_512 (pull_a.account_or_head, pull_a.head_original);
		auto existing (cache.get<account_head_tag> ().find (head_512));
		if (existing == cache.get<account_head_tag> ().end ())
		{
			// Insert new pull
			auto inserted (cache.emplace (futurehead::cached_pulls{ std::chrono::steady_clock::now (), head_512, pull_a.head }));
			(void)inserted;
			debug_assert (inserted.second);
		}
		else
		{
			// Update existing pull
			cache.get<account_head_tag> ().modify (existing, [pull_a](futurehead::cached_pulls & cache_a) {
				cache_a.time = std::chrono::steady_clock::now ();
				cache_a.new_head = pull_a.head;
			});
		}
	}
}

void futurehead::pulls_cache::update_pull (futurehead::pull_info & pull_a)
{
	futurehead::lock_guard<std::mutex> guard (pulls_cache_mutex);
	futurehead::uint512_union head_512 (pull_a.account_or_head, pull_a.head_original);
	auto existing (cache.get<account_head_tag> ().find (head_512));
	if (existing != cache.get<account_head_tag> ().end ())
	{
		pull_a.head = existing->new_head;
	}
}

void futurehead::pulls_cache::remove (futurehead::pull_info const & pull_a)
{
	futurehead::lock_guard<std::mutex> guard (pulls_cache_mutex);
	futurehead::uint512_union head_512 (pull_a.account_or_head, pull_a.head_original);
	cache.get<account_head_tag> ().erase (head_512);
}

void futurehead::bootstrap_attempts::add (std::shared_ptr<futurehead::bootstrap_attempt> attempt_a)
{
	futurehead::lock_guard<std::mutex> lock (bootstrap_attempts_mutex);
	attempts.emplace (attempt_a->incremental_id, attempt_a);
}

void futurehead::bootstrap_attempts::remove (uint64_t incremental_id_a)
{
	futurehead::lock_guard<std::mutex> lock (bootstrap_attempts_mutex);
	attempts.erase (incremental_id_a);
}

void futurehead::bootstrap_attempts::clear ()
{
	futurehead::lock_guard<std::mutex> lock (bootstrap_attempts_mutex);
	attempts.clear ();
}

std::shared_ptr<futurehead::bootstrap_attempt> futurehead::bootstrap_attempts::find (uint64_t incremental_id_a)
{
	futurehead::lock_guard<std::mutex> lock (bootstrap_attempts_mutex);
	auto find_attempt (attempts.find (incremental_id_a));
	if (find_attempt != attempts.end ())
	{
		return find_attempt->second;
	}
	else
	{
		return nullptr;
	}
}

size_t futurehead::bootstrap_attempts::size ()
{
	futurehead::lock_guard<std::mutex> lock (bootstrap_attempts_mutex);
	return attempts.size ();
}
