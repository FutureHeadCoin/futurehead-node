#pragma once

#include <futurehead/lib/numbers.hpp>
#include <futurehead/lib/utility.hpp>
#include <futurehead/secure/common.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace futurehead
{
class signature_checker;
class active_transactions;
class block_store;
class node_observers;
class stats;
class node_config;
class logger_mt;
class online_reps;
class ledger;
class network_params;

class transaction;
namespace transport
{
	class channel;
}

class vote_processor final
{
public:
	explicit vote_processor (futurehead::signature_checker & checker_a, futurehead::active_transactions & active_a, futurehead::node_observers & observers_a, futurehead::stat & stats_a, futurehead::node_config & config_a, futurehead::node_flags & flags_a, futurehead::logger_mt & logger_a, futurehead::online_reps & online_reps_a, futurehead::ledger & ledger_a, futurehead::network_params & network_params_a);
	/** Returns false if the vote was processed */
	bool vote (std::shared_ptr<futurehead::vote>, std::shared_ptr<futurehead::transport::channel>);
	/** Note: node.active.mutex lock is required */
	futurehead::vote_code vote_blocking (std::shared_ptr<futurehead::vote>, std::shared_ptr<futurehead::transport::channel>, bool = false);
	void verify_votes (std::deque<std::pair<std::shared_ptr<futurehead::vote>, std::shared_ptr<futurehead::transport::channel>>> const &);
	void flush ();
	size_t size ();
	bool empty ();
	void calculate_weights ();
	void stop ();

private:
	void process_loop ();

	futurehead::signature_checker & checker;
	futurehead::active_transactions & active;
	futurehead::node_observers & observers;
	futurehead::stat & stats;
	futurehead::node_config & config;
	futurehead::logger_mt & logger;
	futurehead::online_reps & online_reps;
	futurehead::ledger & ledger;
	futurehead::network_params & network_params;

	size_t max_votes;

	std::deque<std::pair<std::shared_ptr<futurehead::vote>, std::shared_ptr<futurehead::transport::channel>>> votes;
	/** Representatives levels for random early detection */
	std::unordered_set<futurehead::account> representatives_1;
	std::unordered_set<futurehead::account> representatives_2;
	std::unordered_set<futurehead::account> representatives_3;
	futurehead::condition_variable condition;
	std::mutex mutex;
	bool started;
	bool stopped;
	bool is_active;
	std::thread thread;

	friend std::unique_ptr<container_info_component> collect_container_info (vote_processor & vote_processor, const std::string & name);
	friend class vote_processor_weights_Test;
};

std::unique_ptr<container_info_component> collect_container_info (vote_processor & vote_processor, const std::string & name);
}
