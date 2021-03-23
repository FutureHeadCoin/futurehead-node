#pragma once

#include <futurehead/node/common.hpp>
#include <futurehead/node/socket.hpp>

#include <atomic>

namespace futurehead
{
class node;
namespace transport
{
	class channel_tcp;
}

class bootstrap_attempt;
class bootstrap_connections;
class frontier_req_client;
class pull_info;
class bootstrap_client final : public std::enable_shared_from_this<bootstrap_client>
{
public:
	bootstrap_client (std::shared_ptr<futurehead::node> node_a, std::shared_ptr<futurehead::bootstrap_connections> connections_a, std::shared_ptr<futurehead::transport::channel_tcp> channel_a, std::shared_ptr<futurehead::socket> socket_a);
	~bootstrap_client ();
	std::shared_ptr<futurehead::bootstrap_client> shared ();
	void stop (bool force);
	double block_rate () const;
	double elapsed_seconds () const;
	void set_start_time (std::chrono::steady_clock::time_point start_time_a);
	std::shared_ptr<futurehead::node> node;
	std::shared_ptr<futurehead::bootstrap_connections> connections;
	std::shared_ptr<futurehead::transport::channel_tcp> channel;
	std::shared_ptr<futurehead::socket> socket;
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::atomic<uint64_t> block_count{ 0 };
	std::atomic<bool> pending_stop{ false };
	std::atomic<bool> hard_stop{ false };

private:
	mutable std::mutex start_time_mutex;
	std::chrono::steady_clock::time_point start_time_m;
};

class bootstrap_connections final : public std::enable_shared_from_this<bootstrap_connections>
{
public:
	bootstrap_connections (futurehead::node & node_a);
	std::shared_ptr<futurehead::bootstrap_connections> shared ();
	std::shared_ptr<futurehead::bootstrap_client> connection (std::shared_ptr<futurehead::bootstrap_attempt> attempt_a = nullptr, bool use_front_connection = false);
	void pool_connection (std::shared_ptr<futurehead::bootstrap_client> client_a, bool new_client = false, bool push_front = false);
	void add_connection (futurehead::endpoint const & endpoint_a);
	std::shared_ptr<futurehead::bootstrap_client> find_connection (futurehead::tcp_endpoint const & endpoint_a);
	void connect_client (futurehead::tcp_endpoint const & endpoint_a, bool push_front = false);
	unsigned target_connections (size_t pulls_remaining, size_t attempts_count);
	void populate_connections (bool repeat = true);
	void start_populate_connections ();
	void add_pull (futurehead::pull_info const & pull_a);
	void request_pull (futurehead::unique_lock<std::mutex> & lock_a);
	void requeue_pull (futurehead::pull_info const & pull_a, bool network_error = false);
	void clear_pulls (uint64_t);
	void run ();
	void stop ();
	std::deque<std::weak_ptr<futurehead::bootstrap_client>> clients;
	std::atomic<unsigned> connections_count{ 0 };
	futurehead::node & node;
	std::deque<std::shared_ptr<futurehead::bootstrap_client>> idle;
	std::deque<futurehead::pull_info> pulls;
	std::atomic<bool> populate_connections_started{ false };
	std::atomic<bool> new_connections_empty{ false };
	std::atomic<bool> stopped{ false };
	std::mutex mutex;
	futurehead::condition_variable condition;
};
}
