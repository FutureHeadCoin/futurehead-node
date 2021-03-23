#pragma once

#include <futurehead/lib/errors.hpp>
#include <futurehead/node/node.hpp>

#include <chrono>

namespace futurehead
{
/** Test-system related error codes */
enum class error_system
{
	generic = 1,
	deadline_expired
};
class system final
{
public:
	system ();
	system (uint16_t, futurehead::transport::transport_type = futurehead::transport::transport_type::tcp, futurehead::node_flags = futurehead::node_flags ());
	~system ();
	void generate_activity (futurehead::node &, std::vector<futurehead::account> &);
	void generate_mass_activity (uint32_t, futurehead::node &);
	void generate_usage_traffic (uint32_t, uint32_t, size_t);
	void generate_usage_traffic (uint32_t, uint32_t);
	futurehead::account get_random_account (std::vector<futurehead::account> &);
	futurehead::uint128_t get_random_amount (futurehead::transaction const &, futurehead::node &, futurehead::account const &);
	void generate_rollback (futurehead::node &, std::vector<futurehead::account> &);
	void generate_change_known (futurehead::node &, std::vector<futurehead::account> &);
	void generate_change_unknown (futurehead::node &, std::vector<futurehead::account> &);
	void generate_receive (futurehead::node &);
	void generate_send_new (futurehead::node &, std::vector<futurehead::account> &);
	void generate_send_existing (futurehead::node &, std::vector<futurehead::account> &);
	std::unique_ptr<futurehead::state_block> upgrade_genesis_epoch (futurehead::node &, futurehead::epoch const);
	std::shared_ptr<futurehead::wallet> wallet (size_t);
	futurehead::account account (futurehead::transaction const &, size_t);
	/** Generate work with difficulty between \p min_difficulty_a (inclusive) and \p max_difficulty_a (exclusive) */
	uint64_t work_generate_limited (futurehead::block_hash const & root_a, uint64_t min_difficulty_a, uint64_t max_difficulty_a);
	/**
	 * Polls, sleep if there's no work to be done (default 50ms), then check the deadline
	 * @returns 0 or futurehead::deadline_expired
	 */
	std::error_code poll (const std::chrono::nanoseconds & sleep_time = std::chrono::milliseconds (50));
	std::error_code poll_until_true (std::chrono::nanoseconds deadline, std::function<bool()>);
	void stop ();
	void deadline_set (const std::chrono::duration<double, std::nano> & delta);
	std::shared_ptr<futurehead::node> add_node (futurehead::node_flags = futurehead::node_flags (), futurehead::transport::transport_type = futurehead::transport::transport_type::tcp);
	std::shared_ptr<futurehead::node> add_node (futurehead::node_config const &, futurehead::node_flags = futurehead::node_flags (), futurehead::transport::transport_type = futurehead::transport::transport_type::tcp);
	boost::asio::io_context io_ctx;
	futurehead::alarm alarm{ io_ctx };
	std::vector<std::shared_ptr<futurehead::node>> nodes;
	futurehead::logging logging;
	futurehead::work_pool work{ std::max (std::thread::hardware_concurrency (), 1u) };
	std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<double>> deadline{ std::chrono::steady_clock::time_point::max () };
	double deadline_scaling_factor{ 1.0 };
	unsigned node_sequence{ 0 };
};
std::unique_ptr<futurehead::state_block> upgrade_epoch (futurehead::work_pool &, futurehead::ledger &, futurehead::epoch);
void blocks_confirm (futurehead::node &, std::vector<std::shared_ptr<futurehead::block>> const &);
}
REGISTER_ERROR_CODES (futurehead, error_system);
