#include <futurehead/boost/asio/bind_executor.hpp>
#include <futurehead/boost/asio/dispatch.hpp>
#include <futurehead/crypto_lib/random_pool.hpp>
#include <futurehead/lib/stats.hpp>
#include <futurehead/node/node.hpp>
#include <futurehead/node/transport/udp.hpp>

#include <boost/format.hpp>

futurehead::transport::channel_udp::channel_udp (futurehead::transport::udp_channels & channels_a, futurehead::endpoint const & endpoint_a, uint8_t protocol_version_a) :
channel (channels_a.node),
endpoint (endpoint_a),
channels (channels_a)
{
	set_network_version (protocol_version_a);
	debug_assert (endpoint_a.address ().is_v6 ());
}

size_t futurehead::transport::channel_udp::hash_code () const
{
	std::hash<::futurehead::endpoint> hash;
	return hash (endpoint);
}

bool futurehead::transport::channel_udp::operator== (futurehead::transport::channel const & other_a) const
{
	bool result (false);
	auto other_l (dynamic_cast<futurehead::transport::channel_udp const *> (&other_a));
	if (other_l != nullptr)
	{
		return *this == *other_l;
	}
	return result;
}

void futurehead::transport::channel_udp::send_buffer (futurehead::shared_const_buffer const & buffer_a, futurehead::stat::detail detail_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a, futurehead::buffer_drop_policy drop_policy_a)
{
	set_last_packet_sent (std::chrono::steady_clock::now ());
	channels.send (buffer_a, endpoint, callback (detail_a, callback_a));
}

std::function<void(boost::system::error_code const &, size_t)> futurehead::transport::channel_udp::callback (futurehead::stat::detail detail_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a) const
{
	return [node = std::weak_ptr<futurehead::node> (channels.node.shared ()), callback_a](boost::system::error_code const & ec, size_t size_a) {
		if (auto node_l = node.lock ())
		{
			if (ec == boost::system::errc::host_unreachable)
			{
				node_l->stats.inc (futurehead::stat::type::error, futurehead::stat::detail::unreachable_host, futurehead::stat::dir::out);
			}
			if (size_a > 0)
			{
				node_l->stats.add (futurehead::stat::type::traffic_udp, futurehead::stat::dir::out, size_a);
			}

			if (callback_a)
			{
				callback_a (ec, size_a);
			}
		}
	};
}

std::string futurehead::transport::channel_udp::to_string () const
{
	return boost::str (boost::format ("%1%") % endpoint);
}

futurehead::transport::udp_channels::udp_channels (futurehead::node & node_a, uint16_t port_a) :
node (node_a),
strand (node_a.io_ctx.get_executor ())
{
	if (!node.flags.disable_udp)
	{
		socket = std::make_unique<boost::asio::ip::udp::socket> (node_a.io_ctx, futurehead::endpoint (boost::asio::ip::address_v6::any (), port_a));
		boost::system::error_code ec;
		auto port (socket->local_endpoint (ec).port ());
		if (ec)
		{
			node.logger.try_log ("Unable to retrieve port: ", ec.message ());
		}
		local_endpoint = futurehead::endpoint (boost::asio::ip::address_v6::loopback (), port);
	}
	else
	{
		local_endpoint = futurehead::endpoint (boost::asio::ip::address_v6::loopback (), 0);
		stopped = true;
	}
}

void futurehead::transport::udp_channels::send (futurehead::shared_const_buffer const & buffer_a, futurehead::endpoint endpoint_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a)
{
	boost::asio::post (strand,
	[this, buffer_a, endpoint_a, callback_a]() {
		if (!this->stopped)
		{
			this->socket->async_send_to (buffer_a, endpoint_a,
			boost::asio::bind_executor (strand, callback_a));
		}
	});
}

std::shared_ptr<futurehead::transport::channel_udp> futurehead::transport::udp_channels::insert (futurehead::endpoint const & endpoint_a, unsigned network_version_a)
{
	debug_assert (endpoint_a.address ().is_v6 ());
	std::shared_ptr<futurehead::transport::channel_udp> result;
	if (!node.network.not_a_peer (endpoint_a, node.config.allow_local_peers) && (node.network_params.network.is_test_network () || !max_ip_connections (endpoint_a)))
	{
		futurehead::unique_lock<std::mutex> lock (mutex);
		auto existing (channels.get<endpoint_tag> ().find (endpoint_a));
		if (existing != channels.get<endpoint_tag> ().end ())
		{
			result = existing->channel;
		}
		else
		{
			result = std::make_shared<futurehead::transport::channel_udp> (*this, endpoint_a, network_version_a);
			channels.get<endpoint_tag> ().insert (result);
			attempts.get<endpoint_tag> ().erase (endpoint_a);
			lock.unlock ();
			node.network.channel_observer (result);
		}
	}
	return result;
}

void futurehead::transport::udp_channels::erase (futurehead::endpoint const & endpoint_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	channels.get<endpoint_tag> ().erase (endpoint_a);
}

size_t futurehead::transport::udp_channels::size () const
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return channels.size ();
}

std::shared_ptr<futurehead::transport::channel_udp> futurehead::transport::udp_channels::channel (futurehead::endpoint const & endpoint_a) const
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	std::shared_ptr<futurehead::transport::channel_udp> result;
	auto existing (channels.get<endpoint_tag> ().find (endpoint_a));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		result = existing->channel;
	}
	return result;
}

std::unordered_set<std::shared_ptr<futurehead::transport::channel>> futurehead::transport::udp_channels::random_set (size_t count_a, uint8_t min_version) const
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
			if (channel->get_network_version () >= min_version)
			{
				result.insert (channel);
			}
		}
	}
	return result;
}

void futurehead::transport::udp_channels::random_fill (std::array<futurehead::endpoint, 8> & target_a) const
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

bool futurehead::transport::udp_channels::store_all (bool clear_peers)
{
	// We can't hold the mutex while starting a write transaction, so
	// we collect endpoints to be saved and then relase the lock.
	std::vector<futurehead::endpoint> endpoints;
	{
		futurehead::lock_guard<std::mutex> lock (mutex);
		endpoints.reserve (channels.size ());
		std::transform (channels.begin (), channels.end (),
		std::back_inserter (endpoints), [](const auto & channel) { return channel.endpoint (); });
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

std::shared_ptr<futurehead::transport::channel_udp> futurehead::transport::udp_channels::find_node_id (futurehead::account const & node_id_a)
{
	std::shared_ptr<futurehead::transport::channel_udp> result;
	futurehead::lock_guard<std::mutex> lock (mutex);
	auto existing (channels.get<node_id_tag> ().find (node_id_a));
	if (existing != channels.get<node_id_tag> ().end ())
	{
		result = existing->channel;
	}
	return result;
}

void futurehead::transport::udp_channels::clean_node_id (futurehead::account const & node_id_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	channels.get<node_id_tag> ().erase (node_id_a);
}

void futurehead::transport::udp_channels::clean_node_id (futurehead::endpoint const & endpoint_a, futurehead::account const & node_id_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	auto existing (channels.get<node_id_tag> ().equal_range (node_id_a));
	for (auto & record : boost::make_iterator_range (existing))
	{
		// Remove duplicate node ID for same IP address
		if (record.endpoint ().address () == endpoint_a.address () && record.endpoint ().port () != endpoint_a.port ())
		{
			channels.get<endpoint_tag> ().erase (record.endpoint ());
			break;
		}
	}
}

futurehead::tcp_endpoint futurehead::transport::udp_channels::bootstrap_peer (uint8_t connection_protocol_version_min)
{
	futurehead::tcp_endpoint result (boost::asio::ip::address_v6::any (), 0);
	futurehead::lock_guard<std::mutex> lock (mutex);
	for (auto i (channels.get<last_bootstrap_attempt_tag> ().begin ()), n (channels.get<last_bootstrap_attempt_tag> ().end ()); i != n;)
	{
		if (i->channel->get_network_version () >= connection_protocol_version_min)
		{
			result = futurehead::transport::map_endpoint_to_tcp (i->endpoint ());
			channels.get<last_bootstrap_attempt_tag> ().modify (i, [](channel_udp_wrapper & wrapper_a) {
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

void futurehead::transport::udp_channels::receive ()
{
	if (!stopped)
	{
		release_assert (socket != nullptr);
		if (node.config.logging.network_packet_logging ())
		{
			node.logger.try_log ("Receiving packet");
		}

		auto data (node.network.buffer_container.allocate ());

		socket->async_receive_from (boost::asio::buffer (data->buffer, futurehead::network::buffer_size), data->endpoint,
		boost::asio::bind_executor (strand,
		[this, data](boost::system::error_code const & error, std::size_t size_a) {
			if (!error && !this->stopped)
			{
				data->size = size_a;
				this->node.network.buffer_container.enqueue (data);
				this->receive ();
			}
			else
			{
				this->node.network.buffer_container.release (data);
				if (error)
				{
					if (this->node.config.logging.network_logging ())
					{
						this->node.logger.try_log (boost::str (boost::format ("UDP Receive error: %1%") % error.message ()));
					}
				}
				if (!this->stopped)
				{
					this->node.alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [this]() { this->receive (); });
				}
			}
		}));
	}
}

void futurehead::transport::udp_channels::start ()
{
	debug_assert (!node.flags.disable_udp);
	for (size_t i = 0; i < node.config.io_threads && !stopped; ++i)
	{
		boost::asio::post (strand, [this]() {
			receive ();
		});
	}
	ongoing_keepalive ();
}

void futurehead::transport::udp_channels::stop ()
{
	// Stop and invalidate local endpoint
	if (!stopped.exchange (true))
	{
		futurehead::lock_guard<std::mutex> lock (mutex);
		local_endpoint = futurehead::endpoint (boost::asio::ip::address_v6::loopback (), 0);

		// On test-net, close directly to avoid address-reuse issues. On livenet, close
		// through the strand as multiple IO threads may access the socket.
		if (node.network_params.network.is_test_network ())
		{
			this->close_socket ();
		}
		else
		{
			boost::asio::dispatch (strand, [this] {
				this->close_socket ();
			});
		}
	}
}

void futurehead::transport::udp_channels::close_socket ()
{
	if (this->socket != nullptr)
	{
		boost::system::error_code ignored;
		this->socket->close (ignored);
	}
}

futurehead::endpoint futurehead::transport::udp_channels::get_local_endpoint () const
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return local_endpoint;
}

namespace
{
class udp_message_visitor : public futurehead::message_visitor
{
public:
	udp_message_visitor (futurehead::node & node_a, futurehead::endpoint const & endpoint_a) :
	node (node_a),
	endpoint (endpoint_a)
	{
	}
	void keepalive (futurehead::keepalive const & message_a) override
	{
		if (!node.network.udp_channels.max_ip_connections (endpoint))
		{
			auto cookie (node.network.syn_cookies.assign (endpoint));
			if (cookie)
			{
				// New connection
				auto find_channel (node.network.udp_channels.channel (endpoint));
				if (find_channel)
				{
					node.network.send_node_id_handshake (find_channel, *cookie, boost::none);
					node.network.send_keepalive_self (find_channel);
				}
				else if (!node.network.tcp_channels.find_channel (futurehead::transport::map_endpoint_to_tcp (endpoint)))
				{
					// Don't start connection if TCP channel to same IP:port exists
					find_channel = std::make_shared<futurehead::transport::channel_udp> (node.network.udp_channels, endpoint, node.network_params.protocol.protocol_version);
					node.network.send_node_id_handshake (find_channel, *cookie, boost::none);
				}
			}
			// Check for special node port data
			auto peer0 (message_a.peers[0]);
			if (peer0.address () == boost::asio::ip::address_v6{} && peer0.port () != 0)
			{
				futurehead::endpoint new_endpoint (endpoint.address (), peer0.port ());
				node.network.merge_peer (new_endpoint);
			}
		}
		message (message_a);
	}
	void publish (futurehead::publish const & message_a) override
	{
		message (message_a);
	}
	void confirm_req (futurehead::confirm_req const & message_a) override
	{
		message (message_a);
	}
	void confirm_ack (futurehead::confirm_ack const & message_a) override
	{
		message (message_a);
	}
	void bulk_pull (futurehead::bulk_pull const &) override
	{
		debug_assert (false);
	}
	void bulk_pull_account (futurehead::bulk_pull_account const &) override
	{
		debug_assert (false);
	}
	void bulk_push (futurehead::bulk_push const &) override
	{
		debug_assert (false);
	}
	void frontier_req (futurehead::frontier_req const &) override
	{
		debug_assert (false);
	}
	void telemetry_req (futurehead::telemetry_req const & message_a) override
	{
		auto find_channel (node.network.udp_channels.channel (endpoint));
		if (find_channel)
		{
			auto is_very_first_message = find_channel->get_last_telemetry_req () == std::chrono::steady_clock::time_point{};
			auto cache_exceeded = std::chrono::steady_clock::now () >= find_channel->get_last_telemetry_req () + futurehead::telemetry_cache_cutoffs::network_to_time (node.network_params.network);
			if (is_very_first_message || cache_exceeded)
			{
				node.network.udp_channels.modify (find_channel, [](std::shared_ptr<futurehead::transport::channel_udp> channel_a) {
					channel_a->set_last_telemetry_req (std::chrono::steady_clock::now ());
				});
				message (message_a);
			}
			else
			{
				node.network.udp_channels.modify (find_channel, [](std::shared_ptr<futurehead::transport::channel_udp> channel_a) {
					channel_a->set_last_packet_received (std::chrono::steady_clock::now ());
				});
			}
		}
	}
	void telemetry_ack (futurehead::telemetry_ack const & message_a) override
	{
		message (message_a);
	}
	void node_id_handshake (futurehead::node_id_handshake const & message_a) override
	{
		if (node.config.logging.network_node_id_handshake_logging ())
		{
			node.logger.try_log (boost::str (boost::format ("Received node_id_handshake message from %1% with query %2% and response ID %3%") % endpoint % (message_a.query ? message_a.query->to_string () : std::string ("[none]")) % (message_a.response ? message_a.response->first.to_node_id () : std::string ("[none]"))));
		}
		boost::optional<futurehead::uint256_union> out_query;
		boost::optional<futurehead::uint256_union> out_respond_to;
		if (message_a.query)
		{
			out_respond_to = message_a.query;
		}
		auto validated_response (false);
		if (message_a.response)
		{
			if (!node.network.syn_cookies.validate (endpoint, message_a.response->first, message_a.response->second))
			{
				validated_response = true;
				if (message_a.response->first != node.node_id.pub && !node.network.tcp_channels.find_node_id (message_a.response->first))
				{
					node.network.udp_channels.clean_node_id (endpoint, message_a.response->first);
					auto new_channel (node.network.udp_channels.insert (endpoint, message_a.header.version_using));
					if (new_channel)
					{
						node.network.udp_channels.modify (new_channel, [&message_a](std::shared_ptr<futurehead::transport::channel_udp> channel_a) {
							channel_a->set_node_id (message_a.response->first);
							channel_a->set_last_packet_received (std::chrono::steady_clock::now ());
						});
					}
				}
			}
			else if (node.config.logging.network_node_id_handshake_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Failed to validate syn cookie signature %1% by %2%") % message_a.response->second.to_string () % message_a.response->first.to_account ()));
			}
		}
		if (!validated_response && node.network.udp_channels.channel (endpoint) == nullptr)
		{
			out_query = node.network.syn_cookies.assign (endpoint);
		}
		if (out_query || out_respond_to)
		{
			auto find_channel (node.network.udp_channels.channel (endpoint));
			if (!find_channel)
			{
				find_channel = std::make_shared<futurehead::transport::channel_udp> (node.network.udp_channels, endpoint, node.network_params.protocol.protocol_version);
			}
			node.network.send_node_id_handshake (find_channel, out_query, out_respond_to);
		}
		message (message_a);
	}
	void message (futurehead::message const & message_a)
	{
		auto find_channel (node.network.udp_channels.channel (endpoint));
		if (find_channel)
		{
			node.network.udp_channels.modify (find_channel, [](std::shared_ptr<futurehead::transport::channel_udp> channel_a) {
				channel_a->set_last_packet_received (std::chrono::steady_clock::now ());
			});
			node.network.process_message (message_a, find_channel);
		}
	}
	futurehead::node & node;
	futurehead::endpoint endpoint;
};
}

void futurehead::transport::udp_channels::receive_action (futurehead::message_buffer * data_a)
{
	auto allowed_sender (true);
	if (data_a->endpoint == get_local_endpoint ())
	{
		allowed_sender = false;
	}
	else if (data_a->endpoint.address ().to_v6 ().is_unspecified ())
	{
		allowed_sender = false;
	}
	else if (futurehead::transport::reserved_address (data_a->endpoint, node.config.allow_local_peers))
	{
		allowed_sender = false;
	}
	if (allowed_sender)
	{
		udp_message_visitor visitor (node, data_a->endpoint);
		futurehead::message_parser parser (node.network.publish_filter, node.block_uniquer, node.vote_uniquer, visitor, node.work, node.ledger.cache.epoch_2_started);
		parser.deserialize_buffer (data_a->buffer, data_a->size);
		if (parser.status == futurehead::message_parser::parse_status::success)
		{
			node.stats.add (futurehead::stat::type::traffic_udp, futurehead::stat::dir::in, data_a->size);
		}
		else if (parser.status == futurehead::message_parser::parse_status::duplicate_publish_message)
		{
			node.stats.inc (futurehead::stat::type::filter, futurehead::stat::detail::duplicate_publish);
		}
		else
		{
			node.stats.inc (futurehead::stat::type::error);

			switch (parser.status)
			{
				case futurehead::message_parser::parse_status::insufficient_work:
					// We've already increment error count, update detail only
					node.stats.inc_detail_only (futurehead::stat::type::error, futurehead::stat::detail::insufficient_work);
					break;
				case futurehead::message_parser::parse_status::invalid_magic:
					node.stats.inc (futurehead::stat::type::udp, futurehead::stat::detail::invalid_magic);
					break;
				case futurehead::message_parser::parse_status::invalid_network:
					node.stats.inc (futurehead::stat::type::udp, futurehead::stat::detail::invalid_network);
					break;
				case futurehead::message_parser::parse_status::invalid_header:
					node.stats.inc (futurehead::stat::type::udp, futurehead::stat::detail::invalid_header);
					break;
				case futurehead::message_parser::parse_status::invalid_message_type:
					node.stats.inc (futurehead::stat::type::udp, futurehead::stat::detail::invalid_message_type);
					break;
				case futurehead::message_parser::parse_status::invalid_keepalive_message:
					node.stats.inc (futurehead::stat::type::udp, futurehead::stat::detail::invalid_keepalive_message);
					break;
				case futurehead::message_parser::parse_status::invalid_publish_message:
					node.stats.inc (futurehead::stat::type::udp, futurehead::stat::detail::invalid_publish_message);
					break;
				case futurehead::message_parser::parse_status::invalid_confirm_req_message:
					node.stats.inc (futurehead::stat::type::udp, futurehead::stat::detail::invalid_confirm_req_message);
					break;
				case futurehead::message_parser::parse_status::invalid_confirm_ack_message:
					node.stats.inc (futurehead::stat::type::udp, futurehead::stat::detail::invalid_confirm_ack_message);
					break;
				case futurehead::message_parser::parse_status::invalid_node_id_handshake_message:
					node.stats.inc (futurehead::stat::type::udp, futurehead::stat::detail::invalid_node_id_handshake_message);
					break;
				case futurehead::message_parser::parse_status::invalid_telemetry_req_message:
					node.stats.inc (futurehead::stat::type::udp, futurehead::stat::detail::invalid_telemetry_req_message);
					break;
				case futurehead::message_parser::parse_status::invalid_telemetry_ack_message:
					node.stats.inc (futurehead::stat::type::udp, futurehead::stat::detail::invalid_telemetry_ack_message);
					break;
				case futurehead::message_parser::parse_status::outdated_version:
					node.stats.inc (futurehead::stat::type::udp, futurehead::stat::detail::outdated_version);
					break;
				case futurehead::message_parser::parse_status::duplicate_publish_message:
				case futurehead::message_parser::parse_status::success:
					/* Already checked, unreachable */
					break;
			}
		}
	}
	else
	{
		if (node.config.logging.network_packet_logging ())
		{
			node.logger.try_log (boost::str (boost::format ("Reserved sender %1%") % data_a->endpoint));
		}

		node.stats.inc_detail_only (futurehead::stat::type::error, futurehead::stat::detail::bad_sender);
	}
}

void futurehead::transport::udp_channels::process_packets ()
{
	while (!stopped)
	{
		auto data (node.network.buffer_container.dequeue ());
		if (data == nullptr)
		{
			break;
		}
		receive_action (data);
		node.network.buffer_container.release (data);
	}
}

std::shared_ptr<futurehead::transport::channel> futurehead::transport::udp_channels::create (futurehead::endpoint const & endpoint_a)
{
	return std::make_shared<futurehead::transport::channel_udp> (*this, endpoint_a, node.network_params.protocol.protocol_version);
}

bool futurehead::transport::udp_channels::max_ip_connections (futurehead::endpoint const & endpoint_a)
{
	bool result (false);
	if (!node.flags.disable_max_peers_per_ip)
	{
		futurehead::unique_lock<std::mutex> lock (mutex);
		result = channels.get<ip_address_tag> ().count (endpoint_a.address ()) >= node.network_params.node.max_peers_per_ip;
	}
	return result;
}

bool futurehead::transport::udp_channels::reachout (futurehead::endpoint const & endpoint_a)
{
	// Don't overload single IP
	bool error = max_ip_connections (endpoint_a);
	if (!error && !node.flags.disable_udp)
	{
		auto endpoint_l (futurehead::transport::map_endpoint_to_v6 (endpoint_a));
		// Don't keepalive to nodes that already sent us something
		error |= channel (endpoint_l) != nullptr;
		futurehead::lock_guard<std::mutex> lock (mutex);
		auto inserted (attempts.emplace (endpoint_l));
		error |= !inserted.second;
	}
	return error;
}

std::unique_ptr<futurehead::container_info_component> futurehead::transport::udp_channels::collect_container_info (std::string const & name)
{
	size_t channels_count;
	size_t attemps_count;
	{
		futurehead::lock_guard<std::mutex> guard (mutex);
		channels_count = channels.size ();
		attemps_count = attempts.size ();
	}

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "channels", channels_count, sizeof (decltype (channels)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "attempts", attemps_count, sizeof (decltype (attempts)::value_type) }));

	return composite;
}

void futurehead::transport::udp_channels::purge (std::chrono::steady_clock::time_point const & cutoff_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	auto disconnect_cutoff (channels.get<last_packet_received_tag> ().lower_bound (cutoff_a));
	channels.get<last_packet_received_tag> ().erase (channels.get<last_packet_received_tag> ().begin (), disconnect_cutoff);
	// Remove keepalive attempt tracking for attempts older than cutoff
	auto attempts_cutoff (attempts.get<last_attempt_tag> ().lower_bound (cutoff_a));
	attempts.get<last_attempt_tag> ().erase (attempts.get<last_attempt_tag> ().begin (), attempts_cutoff);
}

void futurehead::transport::udp_channels::ongoing_keepalive ()
{
	futurehead::keepalive message;
	node.network.random_fill (message.peers);
	std::vector<std::shared_ptr<futurehead::transport::channel_udp>> send_list;
	futurehead::unique_lock<std::mutex> lock (mutex);
	auto keepalive_cutoff (channels.get<last_packet_received_tag> ().lower_bound (std::chrono::steady_clock::now () - node.network_params.node.period));
	for (auto i (channels.get<last_packet_received_tag> ().begin ()); i != keepalive_cutoff; ++i)
	{
		send_list.push_back (i->channel);
	}
	lock.unlock ();
	for (auto & channel : send_list)
	{
		channel->send (message);
	}
	std::weak_ptr<futurehead::node> node_w (node.shared ());
	node.alarm.add (std::chrono::steady_clock::now () + node.network_params.node.period, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->network.udp_channels.ongoing_keepalive ();
		}
	});
}

void futurehead::transport::udp_channels::list_below_version (std::vector<std::shared_ptr<futurehead::transport::channel>> & channels_a, uint8_t cutoff_version_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	// clang-format off
	futurehead::transform_if (channels.get<random_access_tag> ().begin (), channels.get<random_access_tag> ().end (), std::back_inserter (channels_a),
		[cutoff_version_a](auto & channel_a) { return channel_a.channel->get_network_version () < cutoff_version_a; },
		[](const auto & channel) { return channel.channel; });
	// clang-format on
}

void futurehead::transport::udp_channels::list (std::deque<std::shared_ptr<futurehead::transport::channel>> & deque_a, uint8_t minimum_version_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	// clang-format off
	futurehead::transform_if (channels.get<random_access_tag> ().begin (), channels.get<random_access_tag> ().end (), std::back_inserter (deque_a),
		[minimum_version_a](auto & channel_a) { return channel_a.channel->get_network_version () >= minimum_version_a; },
		[](const auto & channel) { return channel.channel; });
	// clang-format on
}

void futurehead::transport::udp_channels::modify (std::shared_ptr<futurehead::transport::channel_udp> channel_a, std::function<void(std::shared_ptr<futurehead::transport::channel_udp>)> modify_callback_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	auto existing (channels.get<endpoint_tag> ().find (channel_a->endpoint));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		channels.get<endpoint_tag> ().modify (existing, [modify_callback_a](channel_udp_wrapper & wrapper_a) {
			modify_callback_a (wrapper_a.channel);
		});
	}
}
