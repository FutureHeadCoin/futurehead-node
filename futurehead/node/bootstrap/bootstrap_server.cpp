#include <futurehead/node/bootstrap/bootstrap_bulk_push.hpp>
#include <futurehead/node/bootstrap/bootstrap_frontier.hpp>
#include <futurehead/node/bootstrap/bootstrap_server.hpp>
#include <futurehead/node/node.hpp>
#include <futurehead/node/transport/tcp.hpp>

#include <boost/format.hpp>

futurehead::bootstrap_listener::bootstrap_listener (uint16_t port_a, futurehead::node & node_a) :
node (node_a),
port (port_a)
{
}

void futurehead::bootstrap_listener::start ()
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	on = true;
	listening_socket = std::make_shared<futurehead::server_socket> (node.shared (), boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::any (), port), node.config.tcp_incoming_connections_max);
	boost::system::error_code ec;
	listening_socket->start (ec);
	if (ec)
	{
		node.logger.try_log (boost::str (boost::format ("Error while binding for incoming TCP/bootstrap on port %1%: %2%") % listening_socket->listening_port () % ec.message ()));
		throw std::runtime_error (ec.message ());
	}
	debug_assert (node.network.endpoint ().port () == listening_socket->listening_port ());
	listening_socket->on_connection ([this](std::shared_ptr<futurehead::socket> new_connection, boost::system::error_code const & ec_a) {
		bool keep_accepting = true;
		if (ec_a)
		{
			keep_accepting = false;
			this->node.logger.try_log (boost::str (boost::format ("Error while accepting incoming TCP/bootstrap connections: %1%") % ec_a.message ()));
		}
		else
		{
			accept_action (ec_a, new_connection);
		}
		return keep_accepting;
	});
}

void futurehead::bootstrap_listener::stop ()
{
	decltype (connections) connections_l;
	{
		futurehead::lock_guard<std::mutex> lock (mutex);
		on = false;
		connections_l.swap (connections);
	}
	if (listening_socket)
	{
		futurehead::lock_guard<std::mutex> lock (mutex);
		listening_socket->close ();
		listening_socket = nullptr;
	}
}

size_t futurehead::bootstrap_listener::connection_count ()
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return connections.size ();
}

void futurehead::bootstrap_listener::accept_action (boost::system::error_code const & ec, std::shared_ptr<futurehead::socket> socket_a)
{
	if (!node.network.excluded_peers.check (socket_a->remote_endpoint ()))
	{
		auto connection (std::make_shared<futurehead::bootstrap_server> (socket_a, node.shared ()));
		futurehead::lock_guard<std::mutex> lock (mutex);
		connections[connection.get ()] = connection;
		connection->receive ();
	}
	else
	{
		node.stats.inc (futurehead::stat::type::tcp, futurehead::stat::detail::tcp_excluded);
		if (node.config.logging.network_rejected_logging ())
		{
			node.logger.try_log ("Rejected connection from excluded peer ", socket_a->remote_endpoint ());
		}
	}
}

boost::asio::ip::tcp::endpoint futurehead::bootstrap_listener::endpoint ()
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	if (on && listening_socket)
	{
		return boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::loopback (), listening_socket->listening_port ());
	}
	else
	{
		return boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::loopback (), 0);
	}
}

std::unique_ptr<futurehead::container_info_component> futurehead::collect_container_info (bootstrap_listener & bootstrap_listener, const std::string & name)
{
	auto sizeof_element = sizeof (decltype (bootstrap_listener.connections)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "connections", bootstrap_listener.connection_count (), sizeof_element }));
	return composite;
}

futurehead::bootstrap_server::bootstrap_server (std::shared_ptr<futurehead::socket> socket_a, std::shared_ptr<futurehead::node> node_a) :
receive_buffer (std::make_shared<std::vector<uint8_t>> ()),
socket (socket_a),
node (node_a)
{
	receive_buffer->resize (1024);
}

futurehead::bootstrap_server::~bootstrap_server ()
{
	if (node->config.logging.bulk_pull_logging ())
	{
		node->logger.try_log ("Exiting incoming TCP/bootstrap server");
	}
	if (type == futurehead::bootstrap_server_type::bootstrap)
	{
		--node->bootstrap.bootstrap_count;
	}
	else if (type == futurehead::bootstrap_server_type::realtime)
	{
		--node->bootstrap.realtime_count;
		// Clear temporary channel
		auto exisiting_response_channel (node->network.tcp_channels.find_channel (remote_endpoint));
		if (exisiting_response_channel != nullptr)
		{
			exisiting_response_channel->temporary = false;
			node->network.tcp_channels.erase (remote_endpoint);
		}
	}
	stop ();
	futurehead::lock_guard<std::mutex> lock (node->bootstrap.mutex);
	node->bootstrap.connections.erase (this);
}

void futurehead::bootstrap_server::stop ()
{
	if (!stopped.exchange (true))
	{
		if (socket != nullptr)
		{
			socket->close ();
		}
	}
}

void futurehead::bootstrap_server::receive ()
{
	// Increase timeout to receive TCP header (idle server socket)
	socket->set_timeout (node->network_params.node.idle_timeout);
	auto this_l (shared_from_this ());
	socket->async_read (receive_buffer, 8, [this_l](boost::system::error_code const & ec, size_t size_a) {
		// Set remote_endpoint
		if (this_l->remote_endpoint.port () == 0)
		{
			this_l->remote_endpoint = this_l->socket->remote_endpoint ();
		}
		// Decrease timeout to default
		this_l->socket->set_timeout (this_l->node->config.tcp_io_timeout);
		// Receive header
		this_l->receive_header_action (ec, size_a);
	});
}

void futurehead::bootstrap_server::receive_header_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		debug_assert (size_a == 8);
		futurehead::bufferstream type_stream (receive_buffer->data (), size_a);
		auto error (false);
		futurehead::message_header header (error, type_stream);
		if (!error)
		{
			auto this_l (shared_from_this ());
			switch (header.type)
			{
				case futurehead::message_type::bulk_pull:
				{
					node->stats.inc (futurehead::stat::type::bootstrap, futurehead::stat::detail::bulk_pull, futurehead::stat::dir::in);
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_bulk_pull_action (ec, size_a, header);
					});
					break;
				}
				case futurehead::message_type::bulk_pull_account:
				{
					node->stats.inc (futurehead::stat::type::bootstrap, futurehead::stat::detail::bulk_pull_account, futurehead::stat::dir::in);
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_bulk_pull_account_action (ec, size_a, header);
					});
					break;
				}
				case futurehead::message_type::frontier_req:
				{
					node->stats.inc (futurehead::stat::type::bootstrap, futurehead::stat::detail::frontier_req, futurehead::stat::dir::in);
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_frontier_req_action (ec, size_a, header);
					});
					break;
				}
				case futurehead::message_type::bulk_push:
				{
					node->stats.inc (futurehead::stat::type::bootstrap, futurehead::stat::detail::bulk_push, futurehead::stat::dir::in);
					if (is_bootstrap_connection ())
					{
						add_request (std::make_unique<futurehead::bulk_push> (header));
					}
					break;
				}
				case futurehead::message_type::keepalive:
				{
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_keepalive_action (ec, size_a, header);
					});
					break;
				}
				case futurehead::message_type::publish:
				{
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_publish_action (ec, size_a, header);
					});
					break;
				}
				case futurehead::message_type::confirm_ack:
				{
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_confirm_ack_action (ec, size_a, header);
					});
					break;
				}
				case futurehead::message_type::confirm_req:
				{
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_confirm_req_action (ec, size_a, header);
					});
					break;
				}
				case futurehead::message_type::node_id_handshake:
				{
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_node_id_handshake_action (ec, size_a, header);
					});
					break;
				}
				case futurehead::message_type::telemetry_req:
				{
					if (is_realtime_connection ())
					{
						// Only handle telemetry requests if they are outside of the cutoff time
						auto is_very_first_message = last_telemetry_req == std::chrono::steady_clock::time_point{};
						auto cache_exceeded = std::chrono::steady_clock::now () >= last_telemetry_req + futurehead::telemetry_cache_cutoffs::network_to_time (node->network_params.network);
						if (is_very_first_message || cache_exceeded)
						{
							last_telemetry_req = std::chrono::steady_clock::now ();
							add_request (std::make_unique<futurehead::telemetry_req> (header));
						}
						else
						{
							node->stats.inc (futurehead::stat::type::telemetry, futurehead::stat::detail::request_within_protection_cache_zone);
						}
					}
					receive ();
					break;
				}
				case futurehead::message_type::telemetry_ack:
				{
					socket->async_read (receive_buffer, header.payload_length_bytes (), [this_l, header](boost::system::error_code const & ec, size_t size_a) {
						this_l->receive_telemetry_ack_action (ec, size_a, header);
					});
					break;
				}
				default:
				{
					if (node->config.logging.network_logging ())
					{
						node->logger.try_log (boost::str (boost::format ("Received invalid type from bootstrap connection %1%") % static_cast<uint8_t> (header.type)));
					}
					break;
				}
			}
		}
	}
	else
	{
		if (node->config.logging.bulk_pull_logging ())
		{
			node->logger.try_log (boost::str (boost::format ("Error while receiving type: %1%") % ec.message ()));
		}
	}
}

void futurehead::bootstrap_server::receive_bulk_pull_action (boost::system::error_code const & ec, size_t size_a, futurehead::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		futurehead::bufferstream stream (receive_buffer->data (), size_a);
		auto request (std::make_unique<futurehead::bulk_pull> (error, stream, header_a));
		if (!error)
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				node->logger.try_log (boost::str (boost::format ("Received bulk pull for %1% down to %2%, maximum of %3%") % request->start.to_string () % request->end.to_string () % (request->count ? request->count : std::numeric_limits<double>::infinity ())));
			}
			if (is_bootstrap_connection () && !node->flags.disable_bootstrap_bulk_pull_server)
			{
				add_request (std::unique_ptr<futurehead::message> (request.release ()));
			}
			receive ();
		}
	}
}

void futurehead::bootstrap_server::receive_bulk_pull_account_action (boost::system::error_code const & ec, size_t size_a, futurehead::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		debug_assert (size_a == header_a.payload_length_bytes ());
		futurehead::bufferstream stream (receive_buffer->data (), size_a);
		auto request (std::make_unique<futurehead::bulk_pull_account> (error, stream, header_a));
		if (!error)
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				node->logger.try_log (boost::str (boost::format ("Received bulk pull account for %1% with a minimum amount of %2%") % request->account.to_account () % futurehead::amount (request->minimum_amount).format_balance (futurehead::Mxrb_ratio, 10, true)));
			}
			if (is_bootstrap_connection () && !node->flags.disable_bootstrap_bulk_pull_server)
			{
				add_request (std::unique_ptr<futurehead::message> (request.release ()));
			}
			receive ();
		}
	}
}

void futurehead::bootstrap_server::receive_frontier_req_action (boost::system::error_code const & ec, size_t size_a, futurehead::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		futurehead::bufferstream stream (receive_buffer->data (), size_a);
		auto request (std::make_unique<futurehead::frontier_req> (error, stream, header_a));
		if (!error)
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				node->logger.try_log (boost::str (boost::format ("Received frontier request for %1% with age %2%") % request->start.to_string () % request->age));
			}
			if (is_bootstrap_connection ())
			{
				add_request (std::unique_ptr<futurehead::message> (request.release ()));
			}
			receive ();
		}
	}
	else
	{
		if (node->config.logging.network_logging ())
		{
			node->logger.try_log (boost::str (boost::format ("Error sending receiving frontier request: %1%") % ec.message ()));
		}
	}
}

void futurehead::bootstrap_server::receive_keepalive_action (boost::system::error_code const & ec, size_t size_a, futurehead::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		futurehead::bufferstream stream (receive_buffer->data (), size_a);
		auto request (std::make_unique<futurehead::keepalive> (error, stream, header_a));
		if (!error)
		{
			if (is_realtime_connection ())
			{
				add_request (std::unique_ptr<futurehead::message> (request.release ()));
			}
			receive ();
		}
	}
	else
	{
		if (node->config.logging.network_keepalive_logging ())
		{
			node->logger.try_log (boost::str (boost::format ("Error receiving keepalive: %1%") % ec.message ()));
		}
	}
}

void futurehead::bootstrap_server::receive_telemetry_ack_action (boost::system::error_code const & ec, size_t size_a, futurehead::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		futurehead::bufferstream stream (receive_buffer->data (), size_a);
		auto request (std::make_unique<futurehead::telemetry_ack> (error, stream, header_a));
		if (!error)
		{
			if (is_realtime_connection ())
			{
				add_request (std::unique_ptr<futurehead::message> (request.release ()));
			}
			receive ();
		}
	}
	else
	{
		if (node->config.logging.network_telemetry_logging ())
		{
			node->logger.try_log (boost::str (boost::format ("Error receiving telemetry ack: %1%") % ec.message ()));
		}
	}
}

void futurehead::bootstrap_server::receive_publish_action (boost::system::error_code const & ec, size_t size_a, futurehead::message_header const & header_a)
{
	if (!ec)
	{
		futurehead::uint128_t digest;
		if (!node->network.publish_filter.apply (receive_buffer->data (), size_a, &digest))
		{
			auto error (false);
			futurehead::bufferstream stream (receive_buffer->data (), size_a);
			auto request (std::make_unique<futurehead::publish> (error, stream, header_a, digest));
			if (!error)
			{
				if (is_realtime_connection ())
				{
					add_request (std::unique_ptr<futurehead::message> (request.release ()));
				}
				receive ();
			}
		}
		else
		{
			node->stats.inc (futurehead::stat::type::filter, futurehead::stat::detail::duplicate_publish);
			receive ();
		}
	}
	else
	{
		if (node->config.logging.network_message_logging ())
		{
			node->logger.try_log (boost::str (boost::format ("Error receiving publish: %1%") % ec.message ()));
		}
	}
}

void futurehead::bootstrap_server::receive_confirm_req_action (boost::system::error_code const & ec, size_t size_a, futurehead::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		futurehead::bufferstream stream (receive_buffer->data (), size_a);
		auto request (std::make_unique<futurehead::confirm_req> (error, stream, header_a));
		if (!error)
		{
			if (is_realtime_connection ())
			{
				add_request (std::unique_ptr<futurehead::message> (request.release ()));
			}
			receive ();
		}
	}
	else if (node->config.logging.network_message_logging ())
	{
		node->logger.try_log (boost::str (boost::format ("Error receiving confirm_req: %1%") % ec.message ()));
	}
}

void futurehead::bootstrap_server::receive_confirm_ack_action (boost::system::error_code const & ec, size_t size_a, futurehead::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		futurehead::bufferstream stream (receive_buffer->data (), size_a);
		auto request (std::make_unique<futurehead::confirm_ack> (error, stream, header_a));
		if (!error)
		{
			if (is_realtime_connection ())
			{
				add_request (std::unique_ptr<futurehead::message> (request.release ()));
			}
			receive ();
		}
	}
	else if (node->config.logging.network_message_logging ())
	{
		node->logger.try_log (boost::str (boost::format ("Error receiving confirm_ack: %1%") % ec.message ()));
	}
}

void futurehead::bootstrap_server::receive_node_id_handshake_action (boost::system::error_code const & ec, size_t size_a, futurehead::message_header const & header_a)
{
	if (!ec)
	{
		auto error (false);
		futurehead::bufferstream stream (receive_buffer->data (), size_a);
		auto request (std::make_unique<futurehead::node_id_handshake> (error, stream, header_a));
		if (!error)
		{
			if (type == futurehead::bootstrap_server_type::undefined && !node->flags.disable_tcp_realtime)
			{
				add_request (std::unique_ptr<futurehead::message> (request.release ()));
			}
			receive ();
		}
	}
	else if (node->config.logging.network_node_id_handshake_logging ())
	{
		node->logger.try_log (boost::str (boost::format ("Error receiving node_id_handshake: %1%") % ec.message ()));
	}
}

void futurehead::bootstrap_server::add_request (std::unique_ptr<futurehead::message> message_a)
{
	debug_assert (message_a != nullptr);
	futurehead::unique_lock<std::mutex> lock (mutex);
	auto start (requests.empty ());
	requests.push (std::move (message_a));
	if (start)
	{
		run_next (lock);
	}
}

void futurehead::bootstrap_server::finish_request ()
{
	futurehead::unique_lock<std::mutex> lock (mutex);
	requests.pop ();
	if (!requests.empty ())
	{
		run_next (lock);
	}
	else
	{
		std::weak_ptr<futurehead::bootstrap_server> this_w (shared_from_this ());
		node->alarm.add (std::chrono::steady_clock::now () + (node->config.tcp_io_timeout * 2) + std::chrono::seconds (1), [this_w]() {
			if (auto this_l = this_w.lock ())
			{
				this_l->timeout ();
			}
		});
	}
}

void futurehead::bootstrap_server::finish_request_async ()
{
	std::weak_ptr<futurehead::bootstrap_server> this_w (shared_from_this ());
	node->background ([this_w]() {
		if (auto this_l = this_w.lock ())
		{
			this_l->finish_request ();
		}
	});
}

void futurehead::bootstrap_server::timeout ()
{
	if (socket != nullptr)
	{
		if (socket->has_timed_out ())
		{
			if (node->config.logging.bulk_pull_logging ())
			{
				node->logger.try_log ("Closing incoming tcp / bootstrap server by timeout");
			}
			{
				futurehead::lock_guard<std::mutex> lock (node->bootstrap.mutex);
				node->bootstrap.connections.erase (this);
			}
			socket->close ();
		}
	}
	else
	{
		futurehead::lock_guard<std::mutex> lock (node->bootstrap.mutex);
		node->bootstrap.connections.erase (this);
	}
}

namespace
{
class request_response_visitor : public futurehead::message_visitor
{
public:
	explicit request_response_visitor (std::shared_ptr<futurehead::bootstrap_server> const & connection_a) :
	connection (connection_a)
	{
	}
	void keepalive (futurehead::keepalive const & message_a) override
	{
		connection->node->network.tcp_message_manager.put_message (futurehead::tcp_message_item{ std::make_shared<futurehead::keepalive> (message_a), connection->remote_endpoint, connection->remote_node_id, connection->socket, connection->type });
	}
	void publish (futurehead::publish const & message_a) override
	{
		connection->node->network.tcp_message_manager.put_message (futurehead::tcp_message_item{ std::make_shared<futurehead::publish> (message_a), connection->remote_endpoint, connection->remote_node_id, connection->socket, connection->type });
	}
	void confirm_req (futurehead::confirm_req const & message_a) override
	{
		connection->node->network.tcp_message_manager.put_message (futurehead::tcp_message_item{ std::make_shared<futurehead::confirm_req> (message_a), connection->remote_endpoint, connection->remote_node_id, connection->socket, connection->type });
	}
	void confirm_ack (futurehead::confirm_ack const & message_a) override
	{
		connection->node->network.tcp_message_manager.put_message (futurehead::tcp_message_item{ std::make_shared<futurehead::confirm_ack> (message_a), connection->remote_endpoint, connection->remote_node_id, connection->socket, connection->type });
	}
	void bulk_pull (futurehead::bulk_pull const &) override
	{
		auto response (std::make_shared<futurehead::bulk_pull_server> (connection, std::unique_ptr<futurehead::bulk_pull> (static_cast<futurehead::bulk_pull *> (connection->requests.front ().release ()))));
		response->send_next ();
	}
	void bulk_pull_account (futurehead::bulk_pull_account const &) override
	{
		auto response (std::make_shared<futurehead::bulk_pull_account_server> (connection, std::unique_ptr<futurehead::bulk_pull_account> (static_cast<futurehead::bulk_pull_account *> (connection->requests.front ().release ()))));
		response->send_frontier ();
	}
	void bulk_push (futurehead::bulk_push const &) override
	{
		auto response (std::make_shared<futurehead::bulk_push_server> (connection));
		response->throttled_receive ();
	}
	void frontier_req (futurehead::frontier_req const &) override
	{
		auto response (std::make_shared<futurehead::frontier_req_server> (connection, std::unique_ptr<futurehead::frontier_req> (static_cast<futurehead::frontier_req *> (connection->requests.front ().release ()))));
		response->send_next ();
	}
	void telemetry_req (futurehead::telemetry_req const & message_a) override
	{
		connection->node->network.tcp_message_manager.put_message (futurehead::tcp_message_item{ std::make_shared<futurehead::telemetry_req> (message_a), connection->remote_endpoint, connection->remote_node_id, connection->socket, connection->type });
	}
	void telemetry_ack (futurehead::telemetry_ack const & message_a) override
	{
		connection->node->network.tcp_message_manager.put_message (futurehead::tcp_message_item{ std::make_shared<futurehead::telemetry_ack> (message_a), connection->remote_endpoint, connection->remote_node_id, connection->socket, connection->type });
	}
	void node_id_handshake (futurehead::node_id_handshake const & message_a) override
	{
		if (connection->node->config.logging.network_node_id_handshake_logging ())
		{
			connection->node->logger.try_log (boost::str (boost::format ("Received node_id_handshake message from %1%") % connection->remote_endpoint));
		}
		if (message_a.query)
		{
			boost::optional<std::pair<futurehead::account, futurehead::signature>> response (std::make_pair (connection->node->node_id.pub, futurehead::sign_message (connection->node->node_id.prv, connection->node->node_id.pub, *message_a.query)));
			debug_assert (!futurehead::validate_message (response->first, *message_a.query, response->second));
			auto cookie (connection->node->network.syn_cookies.assign (futurehead::transport::map_tcp_to_endpoint (connection->remote_endpoint)));
			futurehead::node_id_handshake response_message (cookie, response);
			auto shared_const_buffer = response_message.to_shared_const_buffer (connection->node->ledger.cache.epoch_2_started);
			connection->socket->async_write (shared_const_buffer, [connection = std::weak_ptr<futurehead::bootstrap_server> (connection)](boost::system::error_code const & ec, size_t size_a) {
				if (auto connection_l = connection.lock ())
				{
					if (ec)
					{
						if (connection_l->node->config.logging.network_node_id_handshake_logging ())
						{
							connection_l->node->logger.try_log (boost::str (boost::format ("Error sending node_id_handshake to %1%: %2%") % connection_l->remote_endpoint % ec.message ()));
						}
						// Stop invalid handshake
						connection_l->stop ();
					}
					else
					{
						connection_l->node->stats.inc (futurehead::stat::type::message, futurehead::stat::detail::node_id_handshake, futurehead::stat::dir::out);
						connection_l->finish_request ();
					}
				}
			});
		}
		else if (message_a.response)
		{
			futurehead::account const & node_id (message_a.response->first);
			if (!connection->node->network.syn_cookies.validate (futurehead::transport::map_tcp_to_endpoint (connection->remote_endpoint), node_id, message_a.response->second) && node_id != connection->node->node_id.pub)
			{
				connection->remote_node_id = node_id;
				connection->type = futurehead::bootstrap_server_type::realtime;
				++connection->node->bootstrap.realtime_count;
				connection->finish_request_async ();
			}
			else
			{
				// Stop invalid handshake
				connection->stop ();
			}
		}
		else
		{
			connection->finish_request_async ();
		}
		futurehead::account node_id (connection->remote_node_id);
		futurehead::bootstrap_server_type type (connection->type);
		debug_assert (node_id.is_zero () || type == futurehead::bootstrap_server_type::realtime);
		connection->node->network.tcp_message_manager.put_message (futurehead::tcp_message_item{ std::make_shared<futurehead::node_id_handshake> (message_a), connection->remote_endpoint, connection->remote_node_id, connection->socket, connection->type });
	}
	std::shared_ptr<futurehead::bootstrap_server> connection;
};
}

void futurehead::bootstrap_server::run_next (futurehead::unique_lock<std::mutex> & lock_a)
{
	debug_assert (!requests.empty ());
	request_response_visitor visitor (shared_from_this ());
	auto type (requests.front ()->header.type);
	if (type == futurehead::message_type::bulk_pull || type == futurehead::message_type::bulk_pull_account || type == futurehead::message_type::bulk_push || type == futurehead::message_type::frontier_req || type == futurehead::message_type::node_id_handshake)
	{
		// Bootstrap & node ID (realtime start)
		// Request removed from queue in request_response_visitor. For bootstrap with requests.front ().release (), for node ID with finish_request ()
		requests.front ()->visit (visitor);
	}
	else
	{
		// Realtime
		auto request (std::move (requests.front ()));
		requests.pop ();
		auto timeout_check (requests.empty ());
		lock_a.unlock ();
		request->visit (visitor);
		if (timeout_check)
		{
			std::weak_ptr<futurehead::bootstrap_server> this_w (shared_from_this ());
			node->alarm.add (std::chrono::steady_clock::now () + (node->config.tcp_io_timeout * 2) + std::chrono::seconds (1), [this_w]() {
				if (auto this_l = this_w.lock ())
				{
					this_l->timeout ();
				}
			});
		}
	}
}

bool futurehead::bootstrap_server::is_bootstrap_connection ()
{
	if (type == futurehead::bootstrap_server_type::undefined && !node->flags.disable_bootstrap_listener && node->bootstrap.bootstrap_count < node->config.bootstrap_connections_max)
	{
		++node->bootstrap.bootstrap_count;
		type = futurehead::bootstrap_server_type::bootstrap;
	}
	return type == futurehead::bootstrap_server_type::bootstrap;
}

bool futurehead::bootstrap_server::is_realtime_connection ()
{
	return type == futurehead::bootstrap_server_type::realtime || type == futurehead::bootstrap_server_type::realtime_response_server;
}
