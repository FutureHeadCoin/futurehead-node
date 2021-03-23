#pragma once

#include <futurehead/secure/blockstore.hpp>
#include <futurehead/secure/common.hpp>
#include <futurehead/secure/ledger.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <unordered_set>

namespace futurehead
{
class channel;
class confirmation_solicitor;
class node;
class vote_generator_session;
class vote_info final
{
public:
	std::chrono::steady_clock::time_point time;
	uint64_t sequence;
	futurehead::block_hash hash;
};
class election_vote_result final
{
public:
	election_vote_result () = default;
	election_vote_result (bool, bool);
	bool replay{ false };
	bool processed{ false };
};
class election final : public std::enable_shared_from_this<futurehead::election>
{
	// Minimum time between broadcasts of the current winner of an election, as a backup to requesting confirmations
	std::chrono::milliseconds base_latency () const;
	std::function<void(std::shared_ptr<futurehead::block>)> confirmation_action;

private: // State management
	enum class state_t
	{
		idle,
		passive, // only listening for incoming votes
		active, // actively request confirmations
		broadcasting, // request confirmations and broadcast the winner
		backtracking, // start an election for unconfirmed dependent blocks
		confirmed, // confirmed but still listening for votes
		expired_confirmed,
		expired_unconfirmed
	};
	static int constexpr passive_duration_factor = 5;
	static int constexpr active_request_count_min = 2;
	static int constexpr active_broadcasting_duration_factor = 30;
	static int constexpr confirmed_duration_factor = 5;
	std::atomic<futurehead::election::state_t> state_m = { state_t::idle };

	// These time points must be protected by this mutex
	std::mutex timepoints_mutex;
	std::chrono::steady_clock::time_point state_start = { std::chrono::steady_clock::now () };
	std::chrono::steady_clock::time_point last_block = { std::chrono::steady_clock::now () };
	std::chrono::steady_clock::time_point last_req = { std::chrono::steady_clock::time_point () };

	bool valid_change (futurehead::election::state_t, futurehead::election::state_t) const;
	bool state_change (futurehead::election::state_t, futurehead::election::state_t);
	void broadcast_block (futurehead::confirmation_solicitor &);
	void send_confirm_req (futurehead::confirmation_solicitor &);
	void activate_dependencies ();
	// Calculate votes for local representatives
	void generate_votes (futurehead::block_hash const &);
	void remove_votes (futurehead::block_hash const &);
	std::atomic<bool> prioritized_m = { false };

public:
	election (futurehead::node &, std::shared_ptr<futurehead::block>, std::function<void(std::shared_ptr<futurehead::block>)> const &, bool);
	futurehead::election_vote_result vote (futurehead::account, uint64_t, futurehead::block_hash);
	futurehead::tally_t tally ();
	// Check if we have vote quorum
	bool have_quorum (futurehead::tally_t const &, futurehead::uint128_t) const;
	void confirm_once (futurehead::election_status_type = futurehead::election_status_type::active_confirmed_quorum);
	// Confirm this block if quorum is met
	void confirm_if_quorum ();
	void log_votes (futurehead::tally_t const &) const;
	bool publish (std::shared_ptr<futurehead::block> block_a);
	size_t last_votes_size ();
	void update_dependent ();
	void adjust_dependent_difficulty ();
	size_t insert_inactive_votes_cache (futurehead::block_hash const &);
	bool prioritized () const;
	void prioritize_election (futurehead::vote_generator_session &);
	// Calculate votes if the current winner matches \p hash_a
	void try_generate_votes (futurehead::block_hash const & hash_a);
	// Erase all blocks from active and, if not confirmed, clear digests from network filters
	void cleanup ();

public: // State transitions
	bool transition_time (futurehead::confirmation_solicitor &);
	void transition_passive ();
	void transition_active ();

private:
	void transition_passive_impl ();
	void transition_active_impl ();

public:
	bool idle () const;
	bool confirmed () const;
	futurehead::node & node;
	std::unordered_map<futurehead::account, futurehead::vote_info> last_votes;
	std::unordered_map<futurehead::block_hash, std::shared_ptr<futurehead::block>> blocks;
	std::chrono::steady_clock::time_point election_start = { std::chrono::steady_clock::now () };
	futurehead::election_status status;
	unsigned confirmation_request_count{ 0 };
	std::unordered_map<futurehead::block_hash, futurehead::uint128_t> last_tally;
	std::unordered_set<futurehead::block_hash> dependent_blocks;
	std::chrono::seconds late_blocks_delay{ 5 };
	uint64_t const height;

	friend class active_transactions;

	friend class election_bisect_dependencies_Test;
	friend class election_dependencies_open_link_Test;
};
}
