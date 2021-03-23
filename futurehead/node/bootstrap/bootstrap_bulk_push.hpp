#pragma once

#include <futurehead/node/common.hpp>

#include <future>

namespace futurehead
{
class bootstrap_attempt;
class bootstrap_client;
class bulk_push_client final : public std::enable_shared_from_this<futurehead::bulk_push_client>
{
public:
	explicit bulk_push_client (std::shared_ptr<futurehead::bootstrap_client> const &, std::shared_ptr<futurehead::bootstrap_attempt> const &);
	~bulk_push_client ();
	void start ();
	void push ();
	void push_block (futurehead::block const &);
	void send_finished ();
	std::shared_ptr<futurehead::bootstrap_client> connection;
	std::shared_ptr<futurehead::bootstrap_attempt> attempt;
	std::promise<bool> promise;
	std::pair<futurehead::block_hash, futurehead::block_hash> current_target;
};
class bootstrap_server;
class bulk_push_server final : public std::enable_shared_from_this<futurehead::bulk_push_server>
{
public:
	explicit bulk_push_server (std::shared_ptr<futurehead::bootstrap_server> const &);
	void throttled_receive ();
	void receive ();
	void received_type ();
	void received_block (boost::system::error_code const &, size_t, futurehead::block_type);
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::shared_ptr<futurehead::bootstrap_server> connection;
};
}
