#pragma once

#include <futurehead/node/bootstrap/bootstrap_bulk_pull.hpp>
#include <futurehead/node/bootstrap/bootstrap_connections.hpp>
#include <futurehead/node/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <queue>

namespace mi = boost::multi_index;

namespace futurehead
{
class node;

class bootstrap_connections;
namespace transport
{
	class channel_tcp;
}
enum class bootstrap_mode
{
	legacy,
	lazy,
	wallet_lazy
};
enum class sync_result
{
	success,
	error,
	fork
};
class cached_pulls final
{
public:
	std::chrono::steady_clock::time_point time;
	futurehead::uint512_union account_head;
	futurehead::block_hash new_head;
};
class pulls_cache final
{
public:
	void add (futurehead::pull_info const &);
	void update_pull (futurehead::pull_info &);
	void remove (futurehead::pull_info const &);
	std::mutex pulls_cache_mutex;
	class account_head_tag
	{
	};
	// clang-format off
	boost::multi_index_container<futurehead::cached_pulls,
	mi::indexed_by<
		mi::ordered_non_unique<
			mi::member<futurehead::cached_pulls, std::chrono::steady_clock::time_point, &futurehead::cached_pulls::time>>,
		mi::hashed_unique<mi::tag<account_head_tag>,
			mi::member<futurehead::cached_pulls, futurehead::uint512_union, &futurehead::cached_pulls::account_head>>>>
	cache;
	// clang-format on
	constexpr static size_t cache_size_max = 10000;
};
class bootstrap_attempts final
{
public:
	void add (std::shared_ptr<futurehead::bootstrap_attempt>);
	void remove (uint64_t);
	void clear ();
	std::shared_ptr<futurehead::bootstrap_attempt> find (uint64_t);
	size_t size ();
	std::atomic<uint64_t> incremental{ 0 };
	std::mutex bootstrap_attempts_mutex;
	std::map<uint64_t, std::shared_ptr<futurehead::bootstrap_attempt>> attempts;
};

class bootstrap_initiator final
{
public:
	explicit bootstrap_initiator (futurehead::node &);
	~bootstrap_initiator ();
	void bootstrap (futurehead::endpoint const &, bool add_to_peers = true, bool frontiers_confirmed = false, std::string id_a = "");
	void bootstrap (bool force = false, std::string id_a = "");
	void bootstrap_lazy (futurehead::hash_or_account const &, bool force = false, bool confirmed = true, std::string id_a = "");
	void bootstrap_wallet (std::deque<futurehead::account> &);
	void run_bootstrap ();
	void lazy_requeue (futurehead::block_hash const &, futurehead::block_hash const &, bool);
	void notify_listeners (bool);
	void add_observer (std::function<void(bool)> const &);
	bool in_progress ();
	std::shared_ptr<futurehead::bootstrap_connections> connections;
	std::shared_ptr<futurehead::bootstrap_attempt> new_attempt ();
	bool has_new_attempts ();
	std::shared_ptr<futurehead::bootstrap_attempt> current_attempt ();
	std::shared_ptr<futurehead::bootstrap_attempt> current_lazy_attempt ();
	std::shared_ptr<futurehead::bootstrap_attempt> current_wallet_attempt ();
	futurehead::pulls_cache cache;
	futurehead::bootstrap_attempts attempts;
	void stop ();

private:
	futurehead::node & node;
	std::shared_ptr<futurehead::bootstrap_attempt> find_attempt (futurehead::bootstrap_mode);
	void remove_attempt (std::shared_ptr<futurehead::bootstrap_attempt>);
	void stop_attempts ();
	std::vector<std::shared_ptr<futurehead::bootstrap_attempt>> attempts_list;
	std::atomic<bool> stopped{ false };
	std::mutex mutex;
	futurehead::condition_variable condition;
	std::mutex observers_mutex;
	std::vector<std::function<void(bool)>> observers;
	std::vector<boost::thread> bootstrap_initiator_threads;

	friend std::unique_ptr<container_info_component> collect_container_info (bootstrap_initiator & bootstrap_initiator, const std::string & name);
};

std::unique_ptr<container_info_component> collect_container_info (bootstrap_initiator & bootstrap_initiator, const std::string & name);
class bootstrap_limits final
{
public:
	static constexpr double bootstrap_connection_scale_target_blocks = 10000.0;
	static constexpr double bootstrap_connection_warmup_time_sec = 5.0;
	static constexpr double bootstrap_minimum_blocks_per_sec = 10.0;
	static constexpr double bootstrap_minimum_elapsed_seconds_blockrate = 0.02;
	static constexpr double bootstrap_minimum_frontier_blocks_per_sec = 1000.0;
	static constexpr double bootstrap_minimum_termination_time_sec = 30.0;
	static constexpr unsigned bootstrap_max_new_connections = 32;
	static constexpr size_t bootstrap_max_confirm_frontiers = 70;
	static constexpr double required_frontier_confirmation_ratio = 0.8;
	static constexpr unsigned frontier_confirmation_blocks_limit = 128 * 1024;
	static constexpr unsigned requeued_pulls_limit = 256;
	static constexpr unsigned requeued_pulls_limit_test = 2;
	static constexpr unsigned requeued_pulls_processed_blocks_factor = 4096;
	static constexpr unsigned bulk_push_cost_limit = 200;
	static constexpr std::chrono::seconds lazy_flush_delay_sec = std::chrono::seconds (5);
	static constexpr unsigned lazy_destinations_request_limit = 256 * 1024;
	static constexpr uint64_t lazy_batch_pull_count_resize_blocks_limit = 4 * 1024 * 1024;
	static constexpr double lazy_batch_pull_count_resize_ratio = 2.0;
	static constexpr size_t lazy_blocks_restart_limit = 1024 * 1024;
};
}
