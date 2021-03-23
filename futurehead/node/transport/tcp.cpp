#include <futurehead/lib/stats.hpp>
#include <futurehead/node/node.hpp>
#include <futurehead/node/transport/tcp.hpp>

#include <boost/format.hpp>

futurehead::transport::channel_tcp::channel_tcp (futurehead::node & node_a, std::weak_ptr<futurehead::socket> socket_a) :
channel (node_a),
socket (socket_a)
{
}

futurehead::transport::channel_tcp::~channel_tcp ()
{
	futurehead::lock_guard<std::mutex> lk (channel_mutex);
	// Close socket. Exception: socket is used by bootstrap_server
	if (auto socket_l = socket.lock ())
	{
		if (!temporary)
		{
			socket_l->close ();
		}
		// Remove response server
		if (auto response_server_l = response_server.lock ())
		{
			response_server_l->stop ();
		}
	}
}

size_t futurehead::transport::channel_tcp::hash_code () const
{
	std::hash<::futurehead::tcp_endpoint> hash;
	if (auto socket_l = socket.lock ())
	{
		return hash (socket_l->remote_endpoint ());
	}

	return 0;
}

bool futurehead::transport::channel_tcp::operator== (futurehead::transport::channel const & other_a) const
{
	bool result (false);
	auto other_l (dynamic_cast<futurehead::transport::channel_tcp const *> (&other_a));
	if (other_l != nullptr)
	{
		return *this == *other_l;
	}
	return result;
}

void futurehead::transport::channel_tcp::send_buffer (futurehead::shared_const_buffer const & buffer_a, futurehead::stat::detail detail_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a, futurehead::buffer_drop_policy drop_policy_a)
{
	if (auto socket_l = socket.lock ())
	{
		socket_l->async_write (buffer_a, tcp_callback (detail_a, socket_l->remote_endpoint (), callback_a), drop_policy_a);
	}
	else if (callback_a)
	{
		node.background ([callback_a]() {
			callback_a (boost::system::errc::make_error_code (boost::system::errc::not_supported), 0);
		});
	}
}

std::function<void(boost::system::error_code const &, size_t)> futurehead::transport::channel_tcp::callback (futurehead::stat::detail detail_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a) const
{
	return callback_a;
}

std::function<void(boost::system::error_code const &, size_t)> futurehead::transport::channel_tcp::tcp_callback (futurehead::stat::detail detail_a, futurehead::tcp_endpoint const & endpoint_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a) const
{
	return [endpoint_a, node = std::weak_ptr<futurehead::node> (node.shared ()), callback_a](boost::system::error_code const & ec, size_t size_a) {
		if (auto node_l = node.lock ())
		{
			if (!ec)
			{
				node_l->network.tcp_channels.update (endpoint_a);
			}
			if (ec == boost::system::errc::host_unreachable)
			{
				node_l->stats.inc (futurehead::stat::type::error, futurehead::stat::detail::unreachable_host, futurehead::stat::dir::out);
			}
			if (callback_a)
			{
				callback_a (ec, size_a);
			}
		}
	};
}

std::string futurehead::transport::channel_tcp::to_string () const
{
	if (auto socket_l = socket.lock ())
	{
		return boost::str (boost::format ("%1%") % socket_l->remote_endpoint ());
	}
	return "";
}

futurehead::transport::tcp_channels::tcp_channels (futurehead::node & node_a) :
node (node_a)
{
}

bool futurehead::transport::tcp_channels::insert (std::shared_ptr<futurehead::transport::channel_tcp> channel_a, std::shared_ptr<futurehead::socket> socket_a, std::shared_ptr<futurehead::bootstrap_server> bootstrap_server_a)
{
	auto endpoint (channel_a->get_tcp_endpoint ());
	debug_assert (endpoint.address ().is_v6 ());
	auto udp_endpoint (futurehead::transport::map_tcp_to_endpoint (endpoint));
	bool error (true);
	if (!node.network.not_a_peer (udp_endpoint, node.config.allow_local_peers) && !stopped)
	{
		futurehead::unique_lock<std::mutex> lock (mutex);
		auto existing (channels.get<endpoint_tag> ().find (endpoint));
		if (existing == channels.get<endpoint_tag> ().end ())
		{
			auto node_id (channel_a->get_node_id ());
			if (!channel_a->temporary)
			{
				channels.get<node_id_tag> ().erase (node_id);
			}
			channels.get<endpoint_tag> ().emplace (channel_a, socket_a, bootstrap_server_a);
			attempts.get<endpoint_tag> ().erase (endpoint);
			error = false;
			lock.unlock ();
			node.network.channel_observer (channel_a);
			// Remove UDP channel to same IP:port if exists
			node.network.udp_channels.erase (udp_endpoint);
			// Remove UDP channels with same node ID
			node.network.udp_channels.clean_node_id (node_id);
		}
	}
	return error;
}

void futurehead::transport::tcp_channels::erase (futurehead::tcp_endpoint const & endpoint_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	channels.get<endpoint_tag> ().erase (endpoint_a);
}

size_t futurehead::transport::tcp_channels::size () const
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return channels.size ();
}

std::shared_ptr<futurehead::transport::channel_tcp> futurehead::transport::tcp_channels::find_channel (futurehead::tcp_endpoint const & endpoint_a) const
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	std::shared_ptr<futurehead::transport::channel_tcp> result;
	auto existing (channels.get<endpoint_tag> ().find (endpoint_a));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		result = existing->channel;
	}
	return result;
}

std::unordered_set<std::shared_ptr<futurehead::transport::channel>> futurehead::transport::tcp_channels::random_set (size_t count_a, uint8_t min_version, bool include_temporary_channels_a) const
{
	std::unordered_set<std::shared_ptr<futurehead::transport::channel>> result;
	result.reserve (count_a);
	futurehead::lock_guard<std::mutex> lock (mutex);
	// Stop trying to fill result with random samples after this many attempts
	auto random_cutoff (count_a * 2);
	auto peers_size (channels.size ());
	// Usually count_a will be much smaller than peers.size()
	// Otherwise make sure we have a cutoff on attempting to randomly fill
	if (!channels.empty ())
	{
		for (auto i (0); i < random_cutoff && result.size () < count_a; ++i)
		{
			auto index (futurehead::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (peers_size - 1)));

			auto channel = channels.get<random_access_tag> ()[index].channel;
			if (channel->get_network_version () >= min_version && (include_temporary_channels_a || !channel->temporary))
			{
				result.insert (channel);
			}
		}
	}
	return result;
}

void futurehead::transport::tcp_channels::random_fill (std::array<futurehead::endpoint, 8> & target_a) const
{
	auto peers (random_set (target_a.size ()));
	debug_assert (peers.size () <= target_a.size ());
	auto endpoint (futurehead::endpoint (boost::asio::ip::address_v6{}, 0));
	debug_assert (endpoint.address ().is_v6 ());
	std::fill (target_a.begin (), target_a.end (), endpoint);
	auto j (target_a.begin ());
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i, ++j)
	{
		debug_assert ((*i)->get_endpoint ().address ().is_v6 ());
		debug_assert (j < target_a.end ());
		*j = (*i)->get_endpoint ();
	}
}

bool futurehead::transport::tcp_channels::store_all (bool clear_peers)
{
	// We can't hold the mutex while starting a write transaction, so
	// we collect endpoints to be saved and then relase the lock.
	std::vector<futurehead::endpoint> endpoints;
	{
		futurehead::lock_guard<std::mutex> lock (mutex);
		endpoints.reserve (channels.size ());
		std::transform (channels.begin (), channels.end (),
		std::back_inserter (endpoints), [](const auto & channel) { return futurehead::transport::map_tcp_to_endpoint (channel.endpoint ()); });
	}
	bool result (false);
	if (!endpoints.empty ())
	{
		// Clear all peers then refresh with the current list of peers
		auto transaction (node.store.tx_begin_write ({ tables::peers }));
		if (clear_peers)
		{
			node.store.peer_clear (transaction);
		}
		for (auto endpoint : endpoints)
		{
			futurehead::endpoint_key endpoint_key (endpoint.address ().to_v6 ().to_bytes (), endpoint.port ());
			node.store.peer_put (transaction, std::move (endpoint_key));
		}
		result = true;
	}
	return result;
}

std::shared_ptr<futurehead::transport::channel_tcp> futurehead::transport::tcp_channels::find_node_id (futurehead::account const & node_id_a)
{
	std::shared_ptr<futurehead::transport::channel_tcp> result;
	futurehead::lock_guard<std::mutex> lock (mutex);
	auto existing (channels.get<node_id_tag> ().find (node_id_a));
	if (existing != channels.get<node_id_tag> ().end ())
	{
		result = existing->channel;
	}
	return result;
}

futurehead::tcp_endpoint futurehead::transport::tcp_channels::bootstrap_peer (uint8_t connection_protocol_version_min)
{
	futurehead::tcp_endpoint result (boost::asio::ip::address_v6::any (), 0);
	futurehead::lock_guard<std::mutex> lock (mutex);
	for (auto i (channels.get<last_bootstrap_attempt_tag> ().begin ()), n (channels.get<last_bootstrap_attempt_tag> ().end ()); i != n;)
	{
		if (i->channel->get_network_version () >= connection_protocol_version_min)
		{
			result = i->endpoint ();
			channels.get<last_bootstrap_attempt_tag> ().modify (i, [](channel_tcp_wrapper & wrapper_a) {
				wrapper_a.channel->set_last_bootstrap_attempt (std::chrono::steady_clock::now ());
			});
			i = n;
		}
		else
		{
			++i;
		}
	}
	return result;
}

void futurehead::transport::tcp_channels::process_messages ()
{
	while (!stopped)
	{
		auto item (node.network.tcp_message_manager.get_message ());
		if (item.message != nullptr)
		{
			process_message (*item.message, item.endpoint, item.node_id, item.socket, item.type);
		}
	}
}

void futurehead::transport::tcp_channels::process_message (futurehead::message const & message_a, futurehead::tcp_endpoint const & endpoint_a, futurehead::account const & node_id_a, std::shared_ptr<futurehead::socket> socket_a, futurehead::bootstrap_server_type type_a)
{
	if (!stopped && message_a.header.version_using >= protocol_constants ().protocol_version_min (node.ledger.cache.epoch_2_started))
	{
		auto channel (node.network.find_channel (futurehead::transport::map_tcp_to_endpoint (endpoint_a)));
		if (channel)
		{
			node.network.process_message (message_a, channel);
		}
		else
		{
			channel = node.network.find_node_id (node_id_a);
			if (channel)
			{
				node.network.process_message (message_a, channel);
			}
			else if (!node.network.excluded_peers.check (endpoint_a))
			{
				if (!node_id_a.is_zero ())
				{
					// Add temporary channel
					socket_a->set_writer_concurrency (futurehead::socket::concurrency::multi_writer);
					auto temporary_channel (std::make_shared<futurehead::transport::channel_tcp> (node, socket_a));
					debug_assert (endpoint_a == temporary_channel->get_tcp_endpoint ());
					temporary_channel->set_node_id (node_id_a);
					temporary_channel->set_network_version (message_a.header.version_using);
					temporary_channel->set_last_packet_received (std::chrono::steady_clock::now ());
					temporary_channel->set_last_packet_sent (std::chrono::steady_clock::now ());
					temporary_channel->temporary = true;
					debug_assert (type_a == futurehead::bootstrap_server_type::realtime || type_a == futurehead::bootstrap_server_type::realtime_response_server);
					// Don't insert temporary channels for response_server
					if (type_a == futurehead::bootstrap_server_type::realtime)
					{
						insert (temporary_channel, socket_a, nullptr);
					}
					node.network.process_message (message_a, temporary_channel);
				}
				else
				{
					// Initial node_id_handshake request without node ID
					debug_assert (message_a.header.type == futurehead::message_type::node_id_handshake);
					debug_assert (type_a == futurehead::bootstrap_server_type::undefined);
					node.stats.inc (futurehead::stat::type::message, futurehead::stat::detail::node_id_handshake, futurehead::stat::dir::in);
				}
			}
		}
	}
}

void futurehead::transport::tcp_channels::start ()
{
	ongoing_keepalive ();
}

void futurehead::transport::tcp_channels::stop ()
{
	stopped = true;
	futurehead::unique_lock<std::mutex> lock (mutex);
	// Close all TCP sockets
	for (auto i (channels.begin ()), j (channels.end ()); i != j; ++i)
	{
		if (i->socket)
		{
			i->socket->close ();
		}
		// Remove response server
		if (i->response_server)
		{
			i->response_server->stop ();
		}
	}
	channels.clear ();
	node_id_handshake_sockets.clear ();
}

bool futurehead::transport::tcp_channels::max_ip_connections (futurehead::tcp_endpoint const & endpoint_a)
{
	bool result (false);
	if (!node.flags.disable_max_peers_per_ip)
	{
		futurehead::unique_lock<std::mutex> lock (mutex);
		result = channels.get<ip_address_tag> ().count (endpoint_a.address ()) >= node.network_params.node.max_peers_per_ip;
		if (!result)
		{
			result = attempts.get<ip_address_tag> ().count (endpoint_a.address ()) >= node.network_params.node.max_peers_per_ip;
		}
	}
	return result;
}

bool futurehead::transport::tcp_channels::reachout (futurehead::endpoint const & endpoint_a)
{
	auto tcp_endpoint (futurehead::transport::map_endpoint_to_tcp (endpoint_a));
	// Don't overload single IP
	bool error = node.network.excluded_peers.check (tcp_endpoint) || max_ip_connections (tcp_endpoint);
	if (!error && !node.flags.disable_tcp_realtime)
	{
		// Don't keepalive to nodes that already sent us something
		error |= find_channel (tcp_endpoint) != nullptr;
		futurehead::lock_guard<std::mutex> lock (mutex);
		auto inserted (attempts.emplace (tcp_endpoint));
		error |= !inserted.second;
	}
	return error;
}

std::unique_ptr<futurehead::container_info_component> futurehead::transport::tcp_channels::collect_container_info (std::string const & name)
{
	size_t channels_count;
	size_t attemps_count;
	size_t node_id_handshake_sockets_count;
	{
		futurehead::lock_guard<std::mutex> guard (mutex);
		channels_count = channels.size ();
		attemps_count = attempts.size ();
		node_id_handshake_sockets_count = node_id_handshake_sockets.size ();
	}

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "channels", channels_count, sizeof (decltype (channels)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "attempts", attemps_count, sizeof (decltype (attempts)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "node_id_handshake_sockets", node_id_handshake_sockets_count, sizeof (decltype (node_id_handshake_sockets)::value_type) }));

	return composite;
}

void futurehead::transport::tcp_channels::purge (std::chrono::steady_clock::time_point const & cutoff_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	auto disconnect_cutoff (channels.get<last_packet_sent_tag> ().lower_bound (cutoff_a));
	channels.get<last_packet_sent_tag> ().erase (channels.get<last_packet_sent_tag> ().begin (), disconnect_cutoff);
	// Remove keepalive attempt tracking for attempts older than cutoff
	auto attempts_cutoff (attempts.get<last_attempt_tag> ().lower_bound (cutoff_a));
	attempts.get<last_attempt_tag> ().erase (attempts.get<last_attempt_tag> ().begin (), attempts_cutoff);

	// Check if any tcp channels belonging to old protocol versions which may still be alive due to async operations
	auto lower_bound = channels.get<version_tag> ().lower_bound (node.network_params.protocol.protocol_version_min (node.ledger.cache.epoch_2_started));
	channels.get<version_tag> ().erase (channels.get<version_tag> ().begin (), lower_bound);

	// Cleanup any sockets which may still be existing from failed node id handshakes
	node_id_handshake_sockets.erase (std::remove_if (node_id_handshake_sockets.begin (), node_id_handshake_sockets.end (), [this](auto socket) {
		return channels.get<endpoint_tag> ().find (socket->remote_endpoint ()) == channels.get<endpoint_tag> ().end ();
	}),
	node_id_handshake_sockets.end ());
}

void futurehead::transport::tcp_channels::ongoing_keepalive ()
{
	futurehead::keepalive message;
	node.network.random_fill (message.peers);
	futurehead::unique_lock<std::mutex> lock (mutex);
	// Wake up channels
	std::vector<std::shared_ptr<futurehead::transport::channel_tcp>> send_list;
	auto keepalive_sent_cutoff (channels.get<last_packet_sent_tag> ().lower_bound (std::chrono::steady_clock::now () - node.network_params.node.period));
	for (auto i (channels.get<last_packet_sent_tag> ().begin ()); i != keepalive_sent_cutoff; ++i)
	{
		send_list.push_back (i->channel);
	}
	lock.unlock ();
	for (auto & channel : send_list)
	{
		channel->send (message);
	}
	// Attempt to start TCP connections to known UDP peers
	futurehead::tcp_endpoint invalid_endpoint (boost::asio::ip::address_v6::any (), 0);
	if (!node.network_params.network.is_test_network () && !node.flags.disable_udp)
	{
		size_t random_count (std::min (static_cast<size_t> (6), static_cast<size_t> (std::ceil (std::sqrt (node.network.udp_channels.size ())))));
		for (auto i (0); i <= random_count; ++i)
		{
			auto tcp_endpoint (node.network.udp_channels.bootstrap_peer (node.network_params.protocol.protocol_version_min (node.ledger.cache.epoch_2_started)));
			if (tcp_endpoint != invalid_endpoint && find_channel (tcp_endpoint) == nullptr && !node.network.excluded_peers.check (tcp_endpoint))
			{
				start_tcp (futurehead::transport::map_tcp_to_endpoint (tcp_endpoint));
			}
		}
	}
	std::weak_ptr<futurehead::node> node_w (node.shared ());
	node.alarm.add (std::chrono::steady_clock::now () + node.network_params.node.half_period, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			if (!node_l->network.tcp_channels.stopped)
			{
				node_l->network.tcp_channels.ongoing_keepalive ();
			}
		}
	});
}

void futurehead::transport::tcp_channels::list_below_version (std::vector<std::shared_ptr<futurehead::transport::channel>> & channels_a, uint8_t cutoff_version_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	// clang-format off
	futurehead::transform_if (channels.get<random_access_tag> ().begin (), channels.get<random_access_tag> ().end (), std::back_inserter (channels_a),
		[cutoff_version_a](auto & channel_a) { return channel_a.channel->get_network_version () < cutoff_version_a; },
		[](const auto & channel) { return channel.channel; });
	// clang-format on
}

void futurehead::transport::tcp_channels::list (std::deque<std::shared_ptr<futurehead::transport::channel>> & deque_a, uint8_t minimum_version_a, bool include_temporary_channels_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	// clang-format off
	futurehead::transform_if (channels.get<random_access_tag> ().begin (), channels.get<random_access_tag> ().end (), std::back_inserter (deque_a),
		[include_temporary_channels_a, minimum_version_a](auto & channel_a) { return channel_a.channel->get_network_version () >= minimum_version_a && (include_temporary_channels_a || !channel_a.channel->temporary); },
		[](const auto & channel) { return channel.channel; });
	// clang-format on
}

void futurehead::transport::tcp_channels::modify (std::shared_ptr<futurehead::transport::channel_tcp> channel_a, std::function<void(std::shared_ptr<futurehead::transport::channel_tcp>)> modify_callback_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	auto existing (channels.get<endpoint_tag> ().find (channel_a->get_tcp_endpoint ()));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		channels.get<endpoint_tag> ().modify (existing, [modify_callback_a](channel_tcp_wrapper & wrapper_a) {
			modify_callback_a (wrapper_a.channel);
		});
	}
}

void futurehead::transport::tcp_channels::update (futurehead::tcp_endpoint const & endpoint_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	auto existing (channels.get<endpoint_tag> ().find (endpoint_a));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		channels.get<endpoint_tag> ().modify (existing, [](channel_tcp_wrapper & wrapper_a) {
			wrapper_a.channel->set_last_packet_sent (std::chrono::steady_clock::now ());
		});
	}
}

bool futurehead::transport::tcp_channels::node_id_handhake_sockets_empty () const
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	return node_id_handshake_sockets.empty ();
}

void futurehead::transport::tcp_channels::push_node_id_handshake_socket (std::shared_ptr<futurehead::socket> const & socket_a)
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	node_id_handshake_sockets.push_back (socket_a);
}

void futurehead::transport::tcp_channels::remove_node_id_handshake_socket (std::shared_ptr<futurehead::socket> const & socket_a)
{
	std::weak_ptr<futurehead::node> node_w (node.shared ());
	if (auto node_l = node_w.lock ())
	{
		futurehead::lock_guard<std::mutex> guard (mutex);
		node_id_handshake_sockets.erase (std::remove (node_id_handshake_sockets.begin (), node_id_handshake_sockets.end (), socket_a), node_id_handshake_sockets.end ());
	}
}

void futurehead::transport::tcp_channels::start_tcp (futurehead::endpoint const & endpoint_a, std::function<void(std::shared_ptr<futurehead::transport::channel>)> const & callback_a)
{
	if (node.flags.disable_tcp_realtime)
	{
		node.network.tcp_channels.udp_fallback (endpoint_a, callback_a);
		return;
	}
	auto socket (std::make_shared<futurehead::socket> (node.shared_from_this (), boost::none, futurehead::socket::concurrency::multi_writer));
	std::weak_ptr<futurehead::socket> socket_w (socket);
	auto channel (std::make_shared<futurehead::transport::channel_tcp> (node, socket_w));
	std::weak_ptr<futurehead::node> node_w (node.shared ());
	socket->async_connect (futurehead::transport::map_endpoint_to_tcp (endpoint_a),
	[node_w, channel, socket, endpoint_a, callback_a](boost::system::error_code const & ec) {
		if (auto node_l = node_w.lock ())
		{
			if (!ec && channel)
			{
				// TCP node ID handshake
				auto cookie (node_l->network.syn_cookies.assign (endpoint_a));
				futurehead::node_id_handshake message (cookie, boost::none);
				auto bytes = message.to_shared_const_buffer (node_l->ledger.cache.epoch_2_started);
				if (node_l->config.logging.network_node_id_handshake_logging ())
				{
					node_l->logger.try_log (boost::str (boost::format ("Node ID handshake request sent with node ID %1% to %2%: query %3%") % node_l->node_id.pub.to_node_id () % endpoint_a % (*cookie).to_string ()));
				}
				std::shared_ptr<std::vector<uint8_t>> receive_buffer (std::make_shared<std::vector<uint8_t>> ());
				receive_buffer->resize (256);
				node_l->network.tcp_channels.push_node_id_handshake_socket (socket);
				channel->send_buffer (bytes, futurehead::stat::detail::node_id_handshake, [node_w, channel, endpoint_a, receive_buffer, callback_a](boost::system::error_code const & ec, size_t size_a) {
					if (auto node_l = node_w.lock ())
					{
						if (!ec && channel)
						{
							node_l->network.tcp_channels.start_tcp_receive_node_id (channel, endpoint_a, receive_buffer, callback_a);
						}
						else
						{
							if (auto socket_l = channel->socket.lock ())
							{
								node_l->network.tcp_channels.remove_node_id_handshake_socket (socket_l);
								socket_l->close ();
							}
							if (node_l->config.logging.network_node_id_handshake_logging ())
							{
								node_l->logger.try_log (boost::str (boost::format ("Error sending node_id_handshake to %1%: %2%") % endpoint_a % ec.message ()));
							}
							node_l->network.tcp_channels.udp_fallback (endpoint_a, callback_a);
						}
					}
				});
			}
			else
			{
				node_l->network.tcp_channels.udp_fallback (endpoint_a, callback_a);
			}
		}
	});
}

void futurehead::transport::tcp_channels::start_tcp_receive_node_id (std::shared_ptr<futurehead::transport::channel_tcp> channel_a, futurehead::endpoint const & endpoint_a, std::shared_ptr<std::vector<uint8_t>> receive_buffer_a, std::function<void(std::shared_ptr<futurehead::transport::channel>)> const & callback_a)
{
	std::weak_ptr<futurehead::node> node_w (node.shared ());
	if (auto socket_l = channel_a->socket.lock ())
	{
		auto cleanup_node_id_handshake_socket = [socket_w = channel_a->socket, node_w](futurehead::endpoint const & endpoint_a, std::function<void(std::shared_ptr<futurehead::transport::channel>)> const & callback_a) {
			if (auto node_l = node_w.lock ())
			{
				if (auto socket_l = socket_w.lock ())
				{
					node_l->network.tcp_channels.remove_node_id_handshake_socket (socket_l);
					socket_l->close ();
				}
			}
		};

		auto cleanup_and_udp_fallback = [socket_w = channel_a->socket, node_w, cleanup_node_id_handshake_socket](futurehead::endpoint const & endpoint_a, std::function<void(std::shared_ptr<futurehead::transport::channel>)> const & callback_a) {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.tcp_channels.udp_fallback (endpoint_a, callback_a);
				cleanup_node_id_handshake_socket (endpoint_a, callback_a);
			}
		};

		socket_l->async_read (receive_buffer_a, 8 + sizeof (futurehead::account) + sizeof (futurehead::account) + sizeof (futurehead::signature), [node_w, channel_a, endpoint_a, receive_buffer_a, callback_a, cleanup_and_udp_fallback, cleanup_node_id_handshake_socket](boost::system::error_code const & ec, size_t size_a) {
			if (auto node_l = node_w.lock ())
			{
				if (!ec && channel_a)
				{
					node_l->stats.inc (futurehead::stat::type::message, futurehead::stat::detail::node_id_handshake, futurehead::stat::dir::in);
					auto error (false);
					futurehead::bufferstream stream (receive_buffer_a->data (), size_a);
					futurehead::message_header header (error, stream);
					if (!error && header.type == futurehead::message_type::node_id_handshake)
					{
						if (header.version_using >= node_l->network_params.protocol.protocol_version_min (node_l->ledger.cache.epoch_2_started))
						{
							futurehead::node_id_handshake message (error, stream, header);
							if (!error && message.response && message.query)
							{
								channel_a->set_network_version (header.version_using);
								auto node_id (message.response->first);
								bool process (!node_l->network.syn_cookies.validate (endpoint_a, node_id, message.response->second) && node_id != node_l->node_id.pub);
								if (process)
								{
									/* If node ID is known, don't establish new connection
									   Exception: temporary channels from bootstrap_server */
									auto existing_channel (node_l->network.tcp_channels.find_node_id (node_id));
									if (existing_channel)
									{
										process = existing_channel->temporary;
									}
								}
								if (process)
								{
									channel_a->set_node_id (node_id);
									channel_a->set_last_packet_received (std::chrono::steady_clock::now ());
									boost::optional<std::pair<futurehead::account, futurehead::signature>> response (std::make_pair (node_l->node_id.pub, futurehead::sign_message (node_l->node_id.prv, node_l->node_id.pub, *message.query)));
									futurehead::node_id_handshake response_message (boost::none, response);
									auto bytes = response_message.to_shared_const_buffer (node_l->ledger.cache.epoch_2_started);
									if (node_l->config.logging.network_node_id_handshake_logging ())
									{
										node_l->logger.try_log (boost::str (boost::format ("Node ID handshake response sent with node ID %1% to %2%: query %3%") % node_l->node_id.pub.to_node_id () % endpoint_a % (*message.query).to_string ()));
									}
									channel_a->send_buffer (bytes, futurehead::stat::detail::node_id_handshake, [node_w, channel_a, endpoint_a, callback_a, cleanup_and_udp_fallback](boost::system::error_code const & ec, size_t size_a) {
										if (auto node_l = node_w.lock ())
										{
											if (!ec && channel_a)
											{
												// Insert new node ID connection
												if (auto socket_l = channel_a->socket.lock ())
												{
													channel_a->set_last_packet_sent (std::chrono::steady_clock::now ());
													auto response_server = std::make_shared<futurehead::bootstrap_server> (socket_l, node_l);
													node_l->network.tcp_channels.insert (channel_a, socket_l, response_server);
													if (callback_a)
													{
														callback_a (channel_a);
													}
													// Listen for possible responses
													response_server->type = futurehead::bootstrap_server_type::realtime_response_server;
													response_server->remote_node_id = channel_a->get_node_id ();
													response_server->receive ();
													node_l->network.tcp_channels.remove_node_id_handshake_socket (socket_l);

													if (!node_l->flags.disable_initial_telemetry_requests)
													{
														node_l->telemetry->get_metrics_single_peer_async (channel_a, [](futurehead::telemetry_data_response /* unused */) {
															// Intentionally empty, starts the telemetry request cycle to more quickly disconnect from invalid peers
														});
													}
												}
											}
											else
											{
												if (node_l->config.logging.network_node_id_handshake_logging ())
												{
													node_l->logger.try_log (boost::str (boost::format ("Error sending node_id_handshake to %1%: %2%") % endpoint_a % ec.message ()));
												}
												cleanup_and_udp_fallback (endpoint_a, callback_a);
											}
										}
									});
								}
							}
							else
							{
								cleanup_and_udp_fallback (endpoint_a, callback_a);
							}
						}
						else
						{
							// Version of channel is not high enough, just abort. Don't fallback to udp, instead cleanup attempt
							cleanup_node_id_handshake_socket (endpoint_a, callback_a);
							{
								futurehead::lock_guard<std::mutex> lock (node_l->network.tcp_channels.mutex);
								node_l->network.tcp_channels.attempts.get<endpoint_tag> ().erase (futurehead::transport::map_endpoint_to_tcp (endpoint_a));
							}
						}
					}
					else
					{
						cleanup_and_udp_fallback (endpoint_a, callback_a);
					}
				}
				else
				{
					if (node_l->config.logging.network_node_id_handshake_logging ())
					{
						node_l->logger.try_log (boost::str (boost::format ("Error reading node_id_handshake from %1%: %2%") % endpoint_a % ec.message ()));
					}
					cleanup_and_udp_fallback (endpoint_a, callback_a);
				}
			}
		});
	}
}

void futurehead::transport::tcp_channels::udp_fallback (futurehead::endpoint const & endpoint_a, std::function<void(std::shared_ptr<futurehead::transport::channel>)> const & callback_a)
{
	{
		futurehead::lock_guard<std::mutex> lock (mutex);
		attempts.get<endpoint_tag> ().erase (futurehead::transport::map_endpoint_to_tcp (endpoint_a));
	}
	if (callback_a && !node.flags.disable_udp)
	{
		auto channel_udp (node.network.udp_channels.create (endpoint_a));
		callback_a (channel_udp);
	}
}
