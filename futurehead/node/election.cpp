#include <futurehead/node/confirmation_solicitor.hpp>
#include <futurehead/node/election.hpp>
#include <futurehead/node/network.hpp>
#include <futurehead/node/node.hpp>

#include <boost/format.hpp>

using namespace std::chrono;

int constexpr futurehead::election::passive_duration_factor;
int constexpr futurehead::election::active_request_count_min;
int constexpr futurehead::election::active_broadcasting_duration_factor;
int constexpr futurehead::election::confirmed_duration_factor;

std::chrono::milliseconds futurehead::election::base_latency () const
{
	return node.network_params.network.is_test_network () ? 25ms : 1000ms;
}

futurehead::election_vote_result::election_vote_result (bool replay_a, bool processed_a)
{
	replay = replay_a;
	processed = processed_a;
}

futurehead::election::election (futurehead::node & node_a, std::shared_ptr<futurehead::block> block_a, std::function<void(std::shared_ptr<futurehead::block>)> const & confirmation_action_a, bool prioritized_a) :
confirmation_action (confirmation_action_a),
prioritized_m (prioritized_a),
node (node_a),
status ({ block_a, 0, std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()), std::chrono::duration_values<std::chrono::milliseconds>::zero (), 0, 1, 0, futurehead::election_status_type::ongoing }),
height (block_a->sideband ().height)
{
	last_votes.emplace (node.network_params.random.not_an_account, futurehead::vote_info{ std::chrono::steady_clock::now (), 0, block_a->hash () });
	blocks.emplace (block_a->hash (), block_a);
	update_dependent ();
	if (prioritized_a)
	{
		generate_votes (block_a->hash ());
	}
}

void futurehead::election::confirm_once (futurehead::election_status_type type_a)
{
	debug_assert (!node.active.mutex.try_lock ());
	// This must be kept above the setting of election state, as dependent confirmed elections require up to date changes to election_winner_details
	futurehead::unique_lock<std::mutex> election_winners_lk (node.active.election_winner_details_mutex);
	if (state_m.exchange (futurehead::election::state_t::confirmed) != futurehead::election::state_t::confirmed && (node.active.election_winner_details.count (status.winner->hash ()) == 0))
	{
		status.election_end = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ());
		status.election_duration = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - election_start);
		status.confirmation_request_count = confirmation_request_count;
		status.block_count = futurehead::narrow_cast<decltype (status.block_count)> (blocks.size ());
		status.voter_count = futurehead::narrow_cast<decltype (status.voter_count)> (last_votes.size ());
		status.type = type_a;
		auto status_l (status);
		auto node_l (node.shared ());
		auto confirmation_action_l (confirmation_action);
		node.active.election_winner_details.emplace (status.winner->hash (), shared_from_this ());
		node.active.add_recently_confirmed (status_l.winner->qualified_root (), status_l.winner->hash ());
		node_l->process_confirmed (status_l);
		node.background ([node_l, status_l, confirmation_action_l]() {
			confirmation_action_l (status_l.winner);
		});
		adjust_dependent_difficulty ();
	}
}

bool futurehead::election::valid_change (futurehead::election::state_t expected_a, futurehead::election::state_t desired_a) const
{
	bool result = false;
	switch (expected_a)
	{
		case futurehead::election::state_t::idle:
			switch (desired_a)
			{
				case futurehead::election::state_t::passive:
				case futurehead::election::state_t::active:
					result = true;
					break;
				default:
					break;
			}
			break;
		case futurehead::election::state_t::passive:
			switch (desired_a)
			{
				case futurehead::election::state_t::idle:
				case futurehead::election::state_t::active:
				case futurehead::election::state_t::confirmed:
				case futurehead::election::state_t::expired_unconfirmed:
					result = true;
					break;
				default:
					break;
			}
			break;
		case futurehead::election::state_t::active:
			switch (desired_a)
			{
				case futurehead::election::state_t::idle:
				case futurehead::election::state_t::broadcasting:
				case futurehead::election::state_t::confirmed:
				case futurehead::election::state_t::expired_unconfirmed:
					result = true;
					break;
				default:
					break;
			}
		case futurehead::election::state_t::broadcasting:
			switch (desired_a)
			{
				case futurehead::election::state_t::idle:
				case futurehead::election::state_t::backtracking:
				case futurehead::election::state_t::confirmed:
				case futurehead::election::state_t::expired_unconfirmed:
					result = true;
					break;
				default:
					break;
			}
		case futurehead::election::state_t::backtracking:
			switch (desired_a)
			{
				case futurehead::election::state_t::idle:
				case futurehead::election::state_t::confirmed:
				case futurehead::election::state_t::expired_unconfirmed:
					result = true;
					break;
				default:
					break;
			}
		case futurehead::election::state_t::confirmed:
			switch (desired_a)
			{
				case futurehead::election::state_t::expired_confirmed:
					result = true;
					break;
				default:
					break;
			}
		case futurehead::election::state_t::expired_unconfirmed:
			break;
		case futurehead::election::state_t::expired_confirmed:
			break;
	}
	return result;
}

bool futurehead::election::state_change (futurehead::election::state_t expected_a, futurehead::election::state_t desired_a)
{
	debug_assert (!timepoints_mutex.try_lock ());
	bool result = true;
	if (valid_change (expected_a, desired_a))
	{
		if (state_m.compare_exchange_strong (expected_a, desired_a))
		{
			state_start = std::chrono::steady_clock::now ();
			result = false;
		}
	}
	else
	{
		debug_assert (false);
	}
	return result;
}

void futurehead::election::send_confirm_req (futurehead::confirmation_solicitor & solicitor_a)
{
	if (base_latency () * 5 < std::chrono::steady_clock::now () - last_req)
	{
		if (!solicitor_a.add (*this))
		{
			last_req = std::chrono::steady_clock::now ();
			++confirmation_request_count;
		}
	}
}

void futurehead::election::transition_passive ()
{
	futurehead::lock_guard<std::mutex> guard (timepoints_mutex);
	transition_passive_impl ();
}

void futurehead::election::transition_passive_impl ()
{
	state_change (futurehead::election::state_t::idle, futurehead::election::state_t::passive);
}

void futurehead::election::transition_active ()
{
	futurehead::lock_guard<std::mutex> guard (timepoints_mutex);
	transition_active_impl ();
}

void futurehead::election::transition_active_impl ()
{
	state_change (futurehead::election::state_t::idle, futurehead::election::state_t::active);
}

bool futurehead::election::idle () const
{
	return state_m == futurehead::election::state_t::idle;
}

bool futurehead::election::confirmed () const
{
	return state_m == futurehead::election::state_t::confirmed || state_m == futurehead::election::state_t::expired_confirmed;
}

void futurehead::election::activate_dependencies ()
{
	debug_assert (!node.active.mutex.try_lock ());
	node.active.pending_dependencies.emplace_back (status.winner->hash (), height);
}

void futurehead::election::broadcast_block (futurehead::confirmation_solicitor & solicitor_a)
{
	if (base_latency () * 15 < std::chrono::steady_clock::now () - last_block)
	{
		if (!solicitor_a.broadcast (*this))
		{
			last_block = std::chrono::steady_clock::now ();
		}
	}
}

bool futurehead::election::transition_time (futurehead::confirmation_solicitor & solicitor_a)
{
	debug_assert (!node.active.mutex.try_lock ());
	futurehead::lock_guard<std::mutex> guard (timepoints_mutex);
	bool result = false;
	switch (state_m)
	{
		case futurehead::election::state_t::idle:
			break;
		case futurehead::election::state_t::passive:
		{
			if (base_latency () * passive_duration_factor < std::chrono::steady_clock::now () - state_start)
			{
				state_change (futurehead::election::state_t::passive, futurehead::election::state_t::active);
			}
			break;
		}
		case futurehead::election::state_t::active:
			send_confirm_req (solicitor_a);
			if (confirmation_request_count > active_request_count_min)
			{
				state_change (futurehead::election::state_t::active, futurehead::election::state_t::broadcasting);
			}
			break;
		case futurehead::election::state_t::broadcasting:
			broadcast_block (solicitor_a);
			send_confirm_req (solicitor_a);
			if (base_latency () * active_broadcasting_duration_factor < std::chrono::steady_clock::now () - state_start)
			{
				state_change (futurehead::election::state_t::broadcasting, futurehead::election::state_t::backtracking);
				activate_dependencies ();
			}
			break;
		case futurehead::election::state_t::backtracking:
			broadcast_block (solicitor_a);
			send_confirm_req (solicitor_a);
			break;
		case futurehead::election::state_t::confirmed:
			if (base_latency () * confirmed_duration_factor < std::chrono::steady_clock::now () - state_start)
			{
				result = true;
				state_change (futurehead::election::state_t::confirmed, futurehead::election::state_t::expired_confirmed);
			}
			break;
		case futurehead::election::state_t::expired_unconfirmed:
		case futurehead::election::state_t::expired_confirmed:
			debug_assert (false);
			break;
	}
	if (!confirmed () && std::chrono::minutes (5) < std::chrono::steady_clock::now () - election_start)
	{
		result = true;
		state_change (state_m.load (), futurehead::election::state_t::expired_unconfirmed);
		status.type = futurehead::election_status_type::stopped;
		log_votes (tally ());
	}
	return result;
}

bool futurehead::election::have_quorum (futurehead::tally_t const & tally_a, futurehead::uint128_t tally_sum) const
{
	bool result = false;
	if (tally_sum >= node.config.online_weight_minimum.number ())
	{
		auto i (tally_a.begin ());
		++i;
		auto second (i != tally_a.end () ? i->first : 0);
		auto delta_l (node.delta ());
		result = tally_a.begin ()->first > (second + delta_l);
	}
	return result;
}

futurehead::tally_t futurehead::election::tally ()
{
	std::unordered_map<futurehead::block_hash, futurehead::uint128_t> block_weights;
	for (auto vote_info : last_votes)
	{
		block_weights[vote_info.second.hash] += node.ledger.weight (vote_info.first);
	}
	last_tally = block_weights;
	futurehead::tally_t result;
	for (auto item : block_weights)
	{
		auto block (blocks.find (item.first));
		if (block != blocks.end ())
		{
			result.emplace (item.second, block->second);
		}
	}
	return result;
}

void futurehead::election::confirm_if_quorum ()
{
	auto tally_l (tally ());
	debug_assert (!tally_l.empty ());
	auto winner (tally_l.begin ());
	auto block_l (winner->second);
	auto winner_hash_l (block_l->hash ());
	status.tally = winner->first;
	auto status_winner_hash_l (status.winner->hash ());
	futurehead::uint128_t sum (0);
	for (auto & i : tally_l)
	{
		sum += i.first;
	}
	if (sum >= node.config.online_weight_minimum.number () && winner_hash_l != status_winner_hash_l)
	{
		status.winner = block_l;
		remove_votes (status_winner_hash_l);
		node.block_processor.force (block_l);
		update_dependent ();
		node.active.add_adjust_difficulty (winner_hash_l);
	}
	if (have_quorum (tally_l, sum))
	{
		if (node.config.logging.vote_logging () || blocks.size () > 1)
		{
			log_votes (tally_l);
		}
		confirm_once (futurehead::election_status_type::active_confirmed_quorum);
	}
}

void futurehead::election::log_votes (futurehead::tally_t const & tally_a) const
{
	std::stringstream tally;
	std::string line_end (node.config.logging.single_line_record () ? "\t" : "\n");
	tally << boost::str (boost::format ("%1%Vote tally for root %2%") % line_end % status.winner->root ().to_string ());
	for (auto i (tally_a.begin ()), n (tally_a.end ()); i != n; ++i)
	{
		tally << boost::str (boost::format ("%1%Block %2% weight %3%") % line_end % i->second->hash ().to_string () % i->first.convert_to<std::string> ());
	}
	for (auto i (last_votes.begin ()), n (last_votes.end ()); i != n; ++i)
	{
		if (i->first != node.network_params.random.not_an_account)
		{
			tally << boost::str (boost::format ("%1%%2% %3% %4%") % line_end % i->first.to_account () % std::to_string (i->second.sequence) % i->second.hash.to_string ());
		}
	}
	node.logger.try_log (tally.str ());
}

futurehead::election_vote_result futurehead::election::vote (futurehead::account rep, uint64_t sequence, futurehead::block_hash block_hash)
{
	// see republish_vote documentation for an explanation of these rules
	auto replay (false);
	auto online_stake (node.online_reps.online_stake ());
	auto weight (node.ledger.weight (rep));
	auto should_process (false);
	if (node.network_params.network.is_test_network () || weight > node.minimum_principal_weight (online_stake))
	{
		unsigned int cooldown;
		if (weight < online_stake / 100) // 0.1% to 1%
		{
			cooldown = 15;
		}
		else if (weight < online_stake / 20) // 1% to 5%
		{
			cooldown = 5;
		}
		else // 5% or above
		{
			cooldown = 1;
		}
		auto last_vote_it (last_votes.find (rep));
		if (last_vote_it == last_votes.end ())
		{
			should_process = true;
		}
		else
		{
			auto last_vote_l (last_vote_it->second);
			if (last_vote_l.sequence < sequence || (last_vote_l.sequence == sequence && last_vote_l.hash < block_hash))
			{
				if (last_vote_l.time <= std::chrono::steady_clock::now () - std::chrono::seconds (cooldown))
				{
					should_process = true;
				}
			}
			else
			{
				replay = true;
			}
		}
		if (should_process)
		{
			node.stats.inc (futurehead::stat::type::election, futurehead::stat::detail::vote_new);
			last_votes[rep] = { std::chrono::steady_clock::now (), sequence, block_hash };
			if (!confirmed ())
			{
				confirm_if_quorum ();
			}
		}
	}
	return futurehead::election_vote_result (replay, should_process);
}

bool futurehead::election::publish (std::shared_ptr<futurehead::block> block_a)
{
	// Do not insert new blocks if already confirmed
	auto result (confirmed ());
	if (!result && blocks.size () >= 10)
	{
		if (last_tally[block_a->hash ()] < node.online_reps.online_stake () / 10)
		{
			result = true;
		}
	}
	if (!result)
	{
		auto existing = blocks.find (block_a->hash ());
		if (existing == blocks.end ())
		{
			blocks.emplace (std::make_pair (block_a->hash (), block_a));
			if (!insert_inactive_votes_cache (block_a->hash ()))
			{
				// Even if no votes were in cache, they could be in the election
				confirm_if_quorum ();
			}
			node.network.flood_block (block_a, futurehead::buffer_drop_policy::no_limiter_drop);
		}
		else
		{
			result = true;
			existing->second = block_a;
			if (status.winner->hash () == block_a->hash ())
			{
				status.winner = block_a;
			}
		}
	}
	return result;
}

size_t futurehead::election::last_votes_size ()
{
	futurehead::lock_guard<std::mutex> lock (node.active.mutex);
	return last_votes.size ();
}

void futurehead::election::update_dependent ()
{
	debug_assert (!node.active.mutex.try_lock ());
	std::vector<futurehead::block_hash> blocks_search;
	auto hash (status.winner->hash ());
	auto previous (status.winner->previous ());
	if (!previous.is_zero ())
	{
		blocks_search.push_back (previous);
	}
	auto source (status.winner->source ());
	if (!source.is_zero () && source != previous)
	{
		blocks_search.push_back (source);
	}
	auto link (status.winner->link ());
	if (!link.is_zero () && !node.ledger.is_epoch_link (link) && link != previous)
	{
		blocks_search.push_back (link);
	}
	for (auto & block_search : blocks_search)
	{
		auto existing (node.active.blocks.find (block_search));
		if (existing != node.active.blocks.end () && !existing->second->confirmed ())
		{
			if (existing->second->dependent_blocks.find (hash) == existing->second->dependent_blocks.end ())
			{
				existing->second->dependent_blocks.insert (hash);
			}
		}
	}
}

void futurehead::election::adjust_dependent_difficulty ()
{
	for (auto & dependent_block : dependent_blocks)
	{
		node.active.add_adjust_difficulty (dependent_block);
	}
}

void futurehead::election::cleanup ()
{
	bool unconfirmed (!confirmed ());
	auto winner_root (status.winner->qualified_root ());
	auto winner_hash (status.winner->hash ());
	for (auto const & block : blocks)
	{
		auto & hash (block.first);
		auto erased (node.active.blocks.erase (hash));
		(void)erased;
		debug_assert (erased == 1);
		node.active.erase_inactive_votes_cache (hash);
		// Notify observers about dropped elections & blocks lost confirmed elections
		if (unconfirmed || hash != winner_hash)
		{
			node.observers.active_stopped.notify (hash);
		}
	}
	if (unconfirmed)
	{
		node.active.recently_dropped.add (winner_root);

		// Clear network filter in another thread
		node.worker.push_task ([node_l = node.shared (), blocks_l = std::move (blocks)]() {
			for (auto const & block : blocks_l)
			{
				node_l->network.publish_filter.clear (block.second);
			}
		});
	}
}

size_t futurehead::election::insert_inactive_votes_cache (futurehead::block_hash const & hash_a)
{
	auto cache (node.active.find_inactive_votes_cache (hash_a));
	for (auto const & rep : cache.voters)
	{
		auto inserted (last_votes.emplace (rep, futurehead::vote_info{ std::chrono::steady_clock::time_point::min (), 0, hash_a }));
		if (inserted.second)
		{
			node.stats.inc (futurehead::stat::type::election, futurehead::stat::detail::vote_cached);
		}
	}
	if (!confirmed () && !cache.voters.empty ())
	{
		auto delay (std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - cache.arrival));
		if (delay > late_blocks_delay)
		{
			node.stats.inc (futurehead::stat::type::election, futurehead::stat::detail::late_block);
			node.stats.add (futurehead::stat::type::election, futurehead::stat::detail::late_block_seconds, futurehead::stat::dir::in, delay.count (), true);
		}
		confirm_if_quorum ();
	}
	return cache.voters.size ();
}

bool futurehead::election::prioritized () const
{
	return prioritized_m;
}

void futurehead::election::prioritize_election (futurehead::vote_generator_session & generator_session_a)
{
	debug_assert (!node.active.mutex.try_lock ());
	debug_assert (!prioritized_m);
	prioritized_m = true;
	generator_session_a.add (status.winner->hash ());
}

void futurehead::election::try_generate_votes (futurehead::block_hash const & hash_a)
{
	futurehead::unique_lock<std::mutex> lock (node.active.mutex);
	if (status.winner->hash () == hash_a)
	{
		lock.unlock ();
		generate_votes (hash_a);
	}
}

void futurehead::election::generate_votes (futurehead::block_hash const & hash_a)
{
	if (node.config.enable_voting && node.wallets.reps ().voting > 0)
	{
		node.active.generator.add (hash_a);
	}
}

void futurehead::election::remove_votes (futurehead::block_hash const & hash_a)
{
	if (node.config.enable_voting && node.wallets.reps ().voting > 0)
	{
		// Remove votes from election
		auto list_generated_votes (node.votes_cache.find (hash_a));
		for (auto const & vote : list_generated_votes)
		{
			last_votes.erase (vote->account);
		}
		// Clear votes cache
		node.votes_cache.remove (hash_a);
	}
}
