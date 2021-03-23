#include <futurehead/lib/asio.hpp>
#include <futurehead/lib/json_error_response.hpp>
#include <futurehead/lib/threading.hpp>
#include <futurehead/rpc/rpc_request_processor.hpp>

#include <boost/endian/conversion.hpp>

futurehead::rpc_request_processor::rpc_request_processor (boost::asio::io_context & io_ctx, futurehead::rpc_config & rpc_config) :
ipc_address (rpc_config.rpc_process.ipc_address),
ipc_port (rpc_config.rpc_process.ipc_port),
thread ([this]() {
	futurehead::thread_role::set (futurehead::thread_role::name::rpc_request_processor);
	this->run ();
})
{
	futurehead::lock_guard<std::mutex> lk (this->request_mutex);
	this->connections.reserve (rpc_config.rpc_process.num_ipc_connections);
	for (auto i = 0u; i < rpc_config.rpc_process.num_ipc_connections; ++i)
	{
		connections.push_back (std::make_shared<futurehead::ipc_connection> (futurehead::ipc::ipc_client (io_ctx), false));
		auto connection = this->connections.back ();
		connection->client.async_connect (ipc_address, ipc_port, [connection, &connections_mutex = this->connections_mutex](futurehead::error err) {
			// Even if there is an error this needs to be set so that another attempt can be made to connect with the ipc connection
			futurehead::lock_guard<std::mutex> lk (connections_mutex);
			connection->is_available = true;
		});
	}
}

futurehead::rpc_request_processor::~rpc_request_processor ()
{
	stop ();
}

void futurehead::rpc_request_processor::stop ()
{
	{
		futurehead::lock_guard<std::mutex> lock (request_mutex);
		stopped = true;
	}
	condition.notify_one ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void futurehead::rpc_request_processor::add (std::shared_ptr<rpc_request> request)
{
	{
		futurehead::lock_guard<std::mutex> lk (request_mutex);
		requests.push_back (request);
	}
	condition.notify_one ();
}

void futurehead::rpc_request_processor::read_payload (std::shared_ptr<futurehead::ipc_connection> connection, std::shared_ptr<std::vector<uint8_t>> res, std::shared_ptr<futurehead::rpc_request> rpc_request)
{
	uint32_t payload_size_l = boost::endian::big_to_native (*reinterpret_cast<uint32_t *> (res->data ()));
	res->resize (payload_size_l);
	// Read JSON payload
	connection->client.async_read (res, payload_size_l, [this, connection, res, rpc_request](futurehead::error err_read_a, size_t size_read_a) {
		// We need 2 sequential reads to get both the header and payload, so only allow other writes
		// when they have both been read.
		make_available (*connection);
		if (!err_read_a && size_read_a != 0)
		{
			rpc_request->response (std::string (res->begin (), res->end ()));
			if (rpc_request->action == "stop")
			{
				this->stop_callback ();
			}
		}
		else
		{
			json_error_response (rpc_request->response, "Failed to read payload");
		}
	});
}

void futurehead::rpc_request_processor::make_available (futurehead::ipc_connection & connection)
{
	futurehead::lock_guard<std::mutex> lk (connections_mutex);
	connection.is_available = true; // Allow people to use it now
}

// Connection does not exist or has been closed, try to connect to it again and then resend IPC request
void futurehead::rpc_request_processor::try_reconnect_and_execute_request (std::shared_ptr<futurehead::ipc_connection> connection, futurehead::shared_const_buffer const & req, std::shared_ptr<std::vector<uint8_t>> res, std::shared_ptr<futurehead::rpc_request> rpc_request)
{
	connection->client.async_connect (ipc_address, ipc_port, [this, connection, req, res, rpc_request](futurehead::error err) {
		if (!err)
		{
			connection->client.async_write (req, [this, connection, res, rpc_request](futurehead::error err_a, size_t size_a) {
				if (size_a != 0 && !err_a)
				{
					// Read length
					connection->client.async_read (res, sizeof (uint32_t), [this, connection, res, rpc_request](futurehead::error err_read_a, size_t size_read_a) {
						if (size_read_a != 0 && !err_read_a)
						{
							this->read_payload (connection, res, rpc_request);
						}
						else
						{
							json_error_response (rpc_request->response, "Connection to node has failed");
							make_available (*connection);
						}
					});
				}
				else
				{
					json_error_response (rpc_request->response, "Cannot write to the node");
					make_available (*connection);
				}
			});
		}
		else
		{
			json_error_response (rpc_request->response, "There is a problem connecting to the node. Make sure ipc->tcp is enabled in the node config, ipc ports match and ipc_address is the ip where the node is located");
			make_available (*connection);
		}
	});
}

void futurehead::rpc_request_processor::run ()
{
	// This should be a conditioned wait
	futurehead::unique_lock<std::mutex> lk (request_mutex);
	while (!stopped)
	{
		if (!requests.empty ())
		{
			lk.unlock ();
			futurehead::unique_lock<std::mutex> conditions_lk (connections_mutex);
			// Find the first free ipc_client
			auto it = std::find_if (connections.begin (), connections.end (), [](auto connection) -> bool {
				return connection->is_available;
			});

			if (it != connections.cend ())
			{
				// Successfully found one
				lk.lock ();
				auto rpc_request = requests.front ();
				requests.pop_front ();
				lk.unlock ();
				auto connection = *it;
				connection->is_available = false; // Make sure no one else can take it
				conditions_lk.unlock ();
				auto encoding (rpc_request->rpc_api_version == 1 ? futurehead::ipc::payload_encoding::json_v1 : futurehead::ipc::payload_encoding::flatbuffers_json);
				auto req (futurehead::ipc::prepare_request (encoding, rpc_request->body));
				auto res (std::make_shared<std::vector<uint8_t>> ());

				// Have we tried to connect yet?
				connection->client.async_write (req, [this, connection, req, res, rpc_request](futurehead::error err_a, size_t size_a) {
					if (!err_a)
					{
						connection->client.async_read (res, sizeof (uint32_t), [this, connection, req, res, rpc_request](futurehead::error err_read_a, size_t size_read_a) {
							if (size_read_a != 0 && !err_read_a)
							{
								this->read_payload (connection, res, rpc_request);
							}
							else
							{
								this->try_reconnect_and_execute_request (connection, req, res, rpc_request);
							}
						});
					}
					else
					{
						try_reconnect_and_execute_request (connection, req, res, rpc_request);
					}
				});
			}
			lk.lock ();
		}
		else
		{
			condition.wait (lk);
		}
	}
}
