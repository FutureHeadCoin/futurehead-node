#include <futurehead/core_test/testutil.hpp>
#include <futurehead/lib/threading.hpp>
#include <futurehead/node/socket.hpp>
#include <futurehead/node/testing.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (socket, drop_policy)
{
	auto node_flags = futurehead::inactive_node_flag_defaults ();
	node_flags.read_only = false;
	futurehead::inactive_node inactivenode (futurehead::unique_path (), node_flags);
	auto node = inactivenode.node;

	futurehead::thread_runner runner (node->io_ctx, 1);

	std::vector<std::shared_ptr<futurehead::socket>> connections;

	// We're going to write twice the queue size + 1, and the server isn't reading
	// The total number of drops should thus be 1 (the socket allows doubling the queue size for no_socket_drop)
	size_t max_write_queue_size = 0;
	{
		auto client_dummy (std::make_shared<futurehead::socket> (node, boost::none, futurehead::socket::concurrency::multi_writer));
		max_write_queue_size = client_dummy->get_max_write_queue_size ();
	}

	auto func = [&](size_t total_message_count, futurehead::buffer_drop_policy drop_policy) {
		auto server_port (futurehead::get_available_port ());
		boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v4::any (), server_port);

		auto server_socket (std::make_shared<futurehead::server_socket> (node, endpoint, 1, futurehead::socket::concurrency::multi_writer));
		boost::system::error_code ec;
		server_socket->start (ec);
		ASSERT_FALSE (ec);

		// Accept connection, but don't read so the writer will drop.
		server_socket->on_connection ([&connections](std::shared_ptr<futurehead::socket> new_connection, boost::system::error_code const & ec_a) {
			connections.push_back (new_connection);
			return true;
		});

		auto client (std::make_shared<futurehead::socket> (node, boost::none, futurehead::socket::concurrency::multi_writer));
		futurehead::util::counted_completion write_completion (total_message_count);

		client->async_connect (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::loopback (), server_port),
		[client, total_message_count, node, &write_completion, &drop_policy](boost::system::error_code const & ec_a) {
			for (int i = 0; i < total_message_count; i++)
			{
				std::vector<uint8_t> buff (1);
				client->async_write (
				futurehead::shared_const_buffer (std::move (buff)), [&write_completion](boost::system::error_code const & ec, size_t size_a) {
					write_completion.increment ();
				},
				drop_policy);
			}
		});
		write_completion.await_count_for (std::chrono::seconds (5));
	};

	func (max_write_queue_size * 2 + 1, futurehead::buffer_drop_policy::no_socket_drop);
	ASSERT_EQ (1, node->stats.count (futurehead::stat::type::tcp, futurehead::stat::detail::tcp_write_no_socket_drop, futurehead::stat::dir::out));
	ASSERT_EQ (0, node->stats.count (futurehead::stat::type::tcp, futurehead::stat::detail::tcp_write_drop, futurehead::stat::dir::out));

	func (max_write_queue_size + 1, futurehead::buffer_drop_policy::limiter);
	// The stats are accumulated from before
	ASSERT_EQ (1, node->stats.count (futurehead::stat::type::tcp, futurehead::stat::detail::tcp_write_no_socket_drop, futurehead::stat::dir::out));
	ASSERT_EQ (1, node->stats.count (futurehead::stat::type::tcp, futurehead::stat::detail::tcp_write_drop, futurehead::stat::dir::out));

	node->stop ();
	runner.stop_event_processing ();
	runner.join ();
}

TEST (socket, concurrent_writes)
{
	auto node_flags = futurehead::inactive_node_flag_defaults ();
	node_flags.read_only = false;
	futurehead::inactive_node inactivenode (futurehead::unique_path (), node_flags);
	auto node = inactivenode.node;

	// This gives more realistic execution than using system#poll, allowing writes to
	// queue up and drain concurrently.
	futurehead::thread_runner runner (node->io_ctx, 1);

	constexpr size_t max_connections = 4;
	constexpr size_t client_count = max_connections;
	constexpr size_t message_count = 4;
	constexpr size_t total_message_count = client_count * message_count;

	// We're expecting client_count*4 messages
	futurehead::util::counted_completion read_count_completion (total_message_count);
	std::function<void(std::shared_ptr<futurehead::socket>)> reader = [&read_count_completion, &total_message_count, &reader](std::shared_ptr<futurehead::socket> socket_a) {
		auto buff (std::make_shared<std::vector<uint8_t>> ());
		buff->resize (1);
#ifndef _WIN32
#pragma GCC diagnostic push
#if defined(__has_warning)
#if __has_warning("-Wunused-lambda-capture")
/** total_message_count is constexpr and a capture isn't needed. However, removing it fails to compile on VS2017 due to a known compiler bug. */
#pragma GCC diagnostic ignored "-Wunused-lambda-capture"
#endif
#endif
#endif
		socket_a->async_read (buff, 1, [&read_count_completion, &reader, &total_message_count, socket_a, buff](boost::system::error_code const & ec, size_t size_a) {
			if (!ec)
			{
				if (read_count_completion.increment () < total_message_count)
				{
					reader (socket_a);
				}
			}
			else if (ec != boost::asio::error::eof)
			{
				std::cerr << "async_read: " << ec.message () << std::endl;
			}
		});
#ifndef _WIN32
#pragma GCC diagnostic pop
#endif
	};

	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v4::any (), 25000);

	auto server_socket (std::make_shared<futurehead::server_socket> (node, endpoint, max_connections, futurehead::socket::concurrency::multi_writer));
	boost::system::error_code ec;
	server_socket->start (ec);
	ASSERT_FALSE (ec);
	std::vector<std::shared_ptr<futurehead::socket>> connections;

	// On every new connection, start reading data
	server_socket->on_connection ([&connections, &reader](std::shared_ptr<futurehead::socket> new_connection, boost::system::error_code const & ec_a) {
		if (ec_a)
		{
			std::cerr << "on_connection: " << ec_a.message () << std::endl;
		}
		else
		{
			connections.push_back (new_connection);
			reader (new_connection);
		}
		// Keep accepting connections
		return true;
	});

	futurehead::util::counted_completion connection_count_completion (client_count);
	std::vector<std::shared_ptr<futurehead::socket>> clients;
	for (unsigned i = 0; i < client_count; i++)
	{
		auto client (std::make_shared<futurehead::socket> (node, boost::none, futurehead::socket::concurrency::multi_writer));
		clients.push_back (client);
		client->async_connect (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::loopback (), 25000),
		[&connection_count_completion](boost::system::error_code const & ec_a) {
			if (ec_a)
			{
				std::cerr << "async_connect: " << ec_a.message () << std::endl;
			}
			else
			{
				connection_count_completion.increment ();
			}
		});
	}
	ASSERT_FALSE (connection_count_completion.await_count_for (10s));

	// Execute overlapping writes from multiple threads
	auto client (clients[0]);
	std::vector<std::thread> client_threads;
	for (int i = 0; i < client_count; i++)
	{
#ifndef _WIN32
#pragma GCC diagnostic push
#if defined(__has_warning)
#if __has_warning("-Wunused-lambda-capture")
/** total_message_count is constexpr and a capture isn't needed. However, removing it fails to compile on VS2017 due to a known compiler bug. */
#pragma GCC diagnostic ignored "-Wunused-lambda-capture"
#endif
#endif
#endif
		client_threads.emplace_back ([&client, &message_count]() {
			for (int i = 0; i < message_count; i++)
			{
				std::vector<uint8_t> buff;
				buff.push_back ('A' + i);
				client->async_write (futurehead::shared_const_buffer (std::move (buff)));
			}
		});
#ifndef _WIN32
#pragma GCC diagnostic pop
#endif
	}

	ASSERT_FALSE (read_count_completion.await_count_for (10s));
	node->stop ();
	runner.stop_event_processing ();
	runner.join ();

	ASSERT_EQ (node->stats.count (futurehead::stat::type::tcp, futurehead::stat::detail::tcp_accept_success, futurehead::stat::dir::in), client_count);
	// We may exhaust max connections and have some tcp accept failures, but no more than the client count
	ASSERT_LT (node->stats.count (futurehead::stat::type::tcp, futurehead::stat::detail::tcp_accept_failure, futurehead::stat::dir::in), client_count);

	for (auto & t : client_threads)
	{
		t.join ();
	}
}
