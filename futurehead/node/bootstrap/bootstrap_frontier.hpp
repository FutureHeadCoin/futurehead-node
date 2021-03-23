#pragma once

#include <futurehead/node/common.hpp>

#include <deque>
#include <future>

namespace futurehead
{
class bootstrap_attempt;
class bootstrap_client;
class frontier_req_client final : public std::enable_shared_from_this<futurehead::frontier_req_client>
{
public:
	explicit frontier_req_client (std::shared_ptr<futurehead::bootstrap_client>, std::shared_ptr<futurehead::bootstrap_attempt>);
	~frontier_req_client ();
	void run ();
	void receive_frontier ();
	void received_frontier (boost::system::error_code const &, size_t);
	void unsynced (futurehead::block_hash const &, futurehead::block_hash const &);
	void next ();
	std::shared_ptr<futurehead::bootstrap_client> connection;
	std::shared_ptr<futurehead::bootstrap_attempt> attempt;
	futurehead::account current;
	futurehead::block_hash frontier;
	unsigned count;
	futurehead::account landing;
	futurehead::account faucet;
	std::chrono::steady_clock::time_point start_time;
	std::promise<bool> promise;
	/** A very rough estimate of the cost of `bulk_push`ing missing blocks */
	uint64_t bulk_push_cost;
	std::deque<std::pair<futurehead::account, futurehead::block_hash>> accounts;
	static size_t constexpr size_frontier = sizeof (futurehead::account) + sizeof (futurehead::block_hash);
};
class bootstrap_server;
class frontier_req;
class frontier_req_server final : public std::enable_shared_from_this<futurehead::frontier_req_server>
{
public:
	frontier_req_server (std::shared_ptr<futurehead::bootstrap_server> const &, std::unique_ptr<futurehead::frontier_req>);
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	void next ();
	std::shared_ptr<futurehead::bootstrap_server> connection;
	futurehead::account current;
	futurehead::block_hash frontier;
	std::unique_ptr<futurehead::frontier_req> request;
	size_t count;
	std::deque<std::pair<futurehead::account, futurehead::block_hash>> accounts;
};
}
