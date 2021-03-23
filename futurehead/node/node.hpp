#pragma once

#include <futurehead/lib/alarm.hpp>
#include <futurehead/lib/stats.hpp>
#include <futurehead/lib/work.hpp>
#include <futurehead/lib/worker.hpp>
#include <futurehead/node/active_transactions.hpp>
#include <futurehead/node/blockprocessor.hpp>
#include <futurehead/node/bootstrap/bootstrap.hpp>
#include <futurehead/node/bootstrap/bootstrap_attempt.hpp>
#include <futurehead/node/bootstrap/bootstrap_server.hpp>
#include <futurehead/node/confirmation_height_processor.hpp>
#include <futurehead/node/distributed_work_factory.hpp>
#include <futurehead/node/election.hpp>
#include <futurehead/node/gap_cache.hpp>
#include <futurehead/node/network.hpp>
#include <futurehead/node/node_observers.hpp>
#include <futurehead/node/nodeconfig.hpp>
#include <futurehead/node/online_reps.hpp>
#include <futurehead/node/payment_observer_processor.hpp>
#include <futurehead/node/portmapping.hpp>
#include <futurehead/node/repcrawler.hpp>
#include <futurehead/node/request_aggregator.hpp>
#include <futurehead/node/signatures.hpp>
#include <futurehead/node/telemetry.hpp>
#include <futurehead/node/vote_processor.hpp>
#include <futurehead/node/wallet.hpp>
#include <futurehead/node/write_database_queue.hpp>
#include <futurehead/secure/ledger.hpp>
#include <futurehead/secure/utility.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/program_options.hpp>
#include <boost/thread/latch.hpp>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace futurehead
{
namespace websocket
{
	class listener;
}
class node;
class telemetry;
class work_pool;
class block_arrival_info final
{
public:
	std::chrono::steady_clock::time_point arrival;
	futurehead::block_hash hash;
};
// This class tracks blocks that are probably live because they arrived in a UDP packet
// This gives a fairly reliable way to differentiate between blocks being inserted via bootstrap or new, live blocks.
class block_arrival final
{
public:
	// Return `true' to indicated an error if the block has already been inserted
	bool add (futurehead::block_hash const &);
	bool recent (futurehead::block_hash const &);
	// clang-format off
	class tag_sequence {};
	class tag_hash {};
	boost::multi_index_container<futurehead::block_arrival_info,
		boost::multi_index::indexed_by<
			boost::multi_index::sequenced<boost::multi_index::tag<tag_sequence>>,
			boost::multi_index::hashed_unique<boost::multi_index::tag<tag_hash>,
				boost::multi_index::member<futurehead::block_arrival_info, futurehead::block_hash, &futurehead::block_arrival_info::hash>>>>
	arrival;
	// clang-format on
	std::mutex mutex;
	static size_t constexpr arrival_size_min = 8 * 1024;
	static std::chrono::seconds constexpr arrival_time_min = std::chrono::seconds (300);
};

std::unique_ptr<container_info_component> collect_container_info (block_arrival & block_arrival, const std::string & name);

std::unique_ptr<container_info_component> collect_container_info (rep_crawler & rep_crawler, const std::string & name);

class node final : public std::enable_shared_from_this<futurehead::node>
{
public:
	node (boost::asio::io_context &, uint16_t, boost::filesystem::path const &, futurehead::alarm &, futurehead::logging const &, futurehead::work_pool &, futurehead::node_flags = futurehead::node_flags (), unsigned seq = 0);
	node (boost::asio::io_context &, boost::filesystem::path const &, futurehead::alarm &, futurehead::node_config const &, futurehead::work_pool &, futurehead::node_flags = futurehead::node_flags (), unsigned seq = 0);
	~node ();
	template <typename T>
	void background (T action_a)
	{
		alarm.io_ctx.post (action_a);
	}
	bool copy_with_compaction (boost::filesystem::path const &);
	void keepalive (std::string const &, uint16_t);
	void start ();
	void stop ();
	std::shared_ptr<futurehead::node> shared ();
	int store_version ();
	void receive_confirmed (futurehead::transaction const &, std::shared_ptr<futurehead::block>, futurehead::block_hash const &);
	void process_confirmed_data (futurehead::transaction const &, std::shared_ptr<futurehead::block>, futurehead::block_hash const &, futurehead::account &, futurehead::uint128_t &, bool &, futurehead::account &);
	void process_confirmed (futurehead::election_status const &, uint64_t = 0);
	void process_active (std::shared_ptr<futurehead::block>);
	futurehead::process_return process (futurehead::block &);
	futurehead::process_return process_local (std::shared_ptr<futurehead::block>, bool const = false);
	void keepalive_preconfigured (std::vector<std::string> const &);
	futurehead::block_hash latest (futurehead::account const &);
	futurehead::uint128_t balance (futurehead::account const &);
	std::shared_ptr<futurehead::block> block (futurehead::block_hash const &);
	std::pair<futurehead::uint128_t, futurehead::uint128_t> balance_pending (futurehead::account const &);
	futurehead::uint128_t weight (futurehead::account const &);
	futurehead::block_hash rep_block (futurehead::account const &);
	futurehead::uint128_t minimum_principal_weight ();
	futurehead::uint128_t minimum_principal_weight (futurehead::uint128_t const &);
	void ongoing_rep_calculation ();
	void ongoing_bootstrap ();
	void ongoing_store_flush ();
	void ongoing_peer_store ();
	void ongoing_unchecked_cleanup ();
	void backup_wallet ();
	void search_pending ();
	void bootstrap_wallet ();
	void unchecked_cleanup ();
	int price (futurehead::uint128_t const &, int);
	// The default difficulty updates to base only when the first epoch_2 block is processed
	uint64_t default_difficulty (futurehead::work_version const) const;
	uint64_t max_work_generate_difficulty (futurehead::work_version const) const;
	bool local_work_generation_enabled () const;
	bool work_generation_enabled () const;
	bool work_generation_enabled (std::vector<std::pair<std::string, uint16_t>> const &) const;
	boost::optional<uint64_t> work_generate_blocking (futurehead::block &, uint64_t);
	boost::optional<uint64_t> work_generate_blocking (futurehead::work_version const, futurehead::root const &, uint64_t, boost::optional<futurehead::account> const & = boost::none);
	void work_generate (futurehead::work_version const, futurehead::root const &, uint64_t, std::function<void(boost::optional<uint64_t>)>, boost::optional<futurehead::account> const & = boost::none, bool const = false);
	void add_initial_peers ();
	void block_confirm (std::shared_ptr<futurehead::block>);
	bool block_confirmed (futurehead::block_hash const &);
	bool block_confirmed_or_being_confirmed (futurehead::transaction const &, futurehead::block_hash const &);
	void process_fork (futurehead::transaction const &, std::shared_ptr<futurehead::block>, uint64_t);
	void do_rpc_callback (boost::asio::ip::tcp::resolver::iterator i_a, std::string const &, uint16_t, std::shared_ptr<std::string>, std::shared_ptr<std::string>, std::shared_ptr<boost::asio::ip::tcp::resolver>);
	futurehead::uint128_t delta () const;
	void ongoing_online_weight_calculation ();
	void ongoing_online_weight_calculation_queue ();
	bool online () const;
	bool init_error () const;
	bool epoch_upgrader (futurehead::private_key const &, futurehead::epoch, uint64_t, uint64_t);
	std::pair<uint64_t, decltype (futurehead::ledger::bootstrap_weights)> get_bootstrap_weights () const;
	futurehead::worker worker;
	futurehead::write_database_queue write_database_queue;
	boost::asio::io_context & io_ctx;
	boost::latch node_initialized_latch;
	futurehead::network_params network_params;
	futurehead::node_config config;
	futurehead::stat stats;
	std::shared_ptr<futurehead::websocket::listener> websocket_server;
	futurehead::node_flags flags;
	futurehead::alarm & alarm;
	futurehead::work_pool & work;
	futurehead::distributed_work_factory distributed_work;
	futurehead::logger_mt logger;
	std::unique_ptr<futurehead::block_store> store_impl;
	futurehead::block_store & store;
	std::unique_ptr<futurehead::wallets_store> wallets_store_impl;
	futurehead::wallets_store & wallets_store;
	futurehead::gap_cache gap_cache;
	futurehead::ledger ledger;
	futurehead::signature_checker checker;
	futurehead::network network;
	std::shared_ptr<futurehead::telemetry> telemetry;
	futurehead::bootstrap_initiator bootstrap_initiator;
	futurehead::bootstrap_listener bootstrap;
	boost::filesystem::path application_path;
	futurehead::node_observers observers;
	futurehead::port_mapping port_mapping;
	futurehead::vote_processor vote_processor;
	futurehead::rep_crawler rep_crawler;
	unsigned warmed_up;
	futurehead::block_processor block_processor;
	std::thread block_processor_thread;
	futurehead::block_arrival block_arrival;
	futurehead::online_reps online_reps;
	futurehead::votes_cache votes_cache;
	futurehead::keypair node_id;
	futurehead::block_uniquer block_uniquer;
	futurehead::vote_uniquer vote_uniquer;
	futurehead::confirmation_height_processor confirmation_height_processor;
	futurehead::active_transactions active;
	futurehead::request_aggregator aggregator;
	futurehead::payment_observer_processor payment_observer_processor;
	futurehead::wallets wallets;
	const std::chrono::steady_clock::time_point startup_time;
	std::chrono::seconds unchecked_cutoff = std::chrono::seconds (7 * 24 * 60 * 60); // Week
	std::atomic<bool> unresponsive_work_peers{ false };
	std::atomic<bool> stopped{ false };
	static double constexpr price_max = 16.0;
	static double constexpr free_cutoff = 1024.0;
	// For tests only
	unsigned node_seq;
	// For tests only
	boost::optional<uint64_t> work_generate_blocking (futurehead::block &);
	// For tests only
	boost::optional<uint64_t> work_generate_blocking (futurehead::root const &, uint64_t);
	// For tests only
	boost::optional<uint64_t> work_generate_blocking (futurehead::root const &);

private:
	void long_inactivity_cleanup ();
	void epoch_upgrader_impl (futurehead::private_key const &, futurehead::epoch, uint64_t, uint64_t);
	futurehead::locked<std::future<void>> epoch_upgrading;
};

std::unique_ptr<container_info_component> collect_container_info (node & node, const std::string & name);

futurehead::node_flags const & inactive_node_flag_defaults ();

class inactive_node final
{
public:
	inactive_node (boost::filesystem::path const & path_a, futurehead::node_flags const & node_flags_a = futurehead::inactive_node_flag_defaults ());
	~inactive_node ();
	std::shared_ptr<boost::asio::io_context> io_context;
	futurehead::alarm alarm;
	futurehead::work_pool work;
	std::shared_ptr<futurehead::node> node;
};
std::unique_ptr<futurehead::inactive_node> default_inactive_node (boost::filesystem::path const &, boost::program_options::variables_map const &);
}
