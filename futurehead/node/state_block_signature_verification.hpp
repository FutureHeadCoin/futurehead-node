#pragma once

#include <futurehead/lib/locks.hpp>
#include <futurehead/secure/common.hpp>

#include <deque>
#include <functional>
#include <thread>

namespace futurehead
{
class epochs;
class logger_mt;
class node_config;
class signature_checker;

class state_block_signature_verification
{
public:
	state_block_signature_verification (futurehead::signature_checker &, futurehead::epochs &, futurehead::node_config &, futurehead::logger_mt &, uint64_t);
	~state_block_signature_verification ();
	void add (futurehead::unchecked_info const & info_a);
	size_t size ();
	void stop ();
	bool is_active ();

	std::function<void(std::deque<futurehead::unchecked_info> &, std::vector<int> const &, std::vector<futurehead::block_hash> const &, std::vector<futurehead::signature> const &)> blocks_verified_callback;
	std::function<void()> transition_inactive_callback;

private:
	futurehead::signature_checker & signature_checker;
	futurehead::epochs & epochs;
	futurehead::node_config & node_config;
	futurehead::logger_mt & logger;

	std::mutex mutex;
	bool stopped{ false };
	bool active{ false };
	std::deque<futurehead::unchecked_info> state_blocks;
	futurehead::condition_variable condition;
	std::thread thread;

	void run (uint64_t block_processor_verification_size);
	std::deque<futurehead::unchecked_info> setup_items (size_t);
	void verify_state_blocks (std::deque<futurehead::unchecked_info> &);
};

std::unique_ptr<futurehead::container_info_component> collect_container_info (state_block_signature_verification & state_block_signature_verification, const std::string & name);
}
