#pragma once

#include <futurehead/lib/config.hpp>
#include <futurehead/lib/locks.hpp>
#include <futurehead/lib/numbers.hpp>
#include <futurehead/lib/utility.hpp>

#include <boost/optional.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <memory>

namespace futurehead
{
enum class work_version
{
	unspecified,
	work_1
};
std::string to_string (futurehead::work_version const version_a);

class block;
class block_details;
bool work_validate_entry (futurehead::block const &);
bool work_validate_entry (futurehead::work_version const, futurehead::root const &, uint64_t const);

uint64_t work_difficulty (futurehead::work_version const, futurehead::root const &, uint64_t const);

uint64_t work_threshold_base (futurehead::work_version const);
uint64_t work_threshold_entry (futurehead::work_version const);
// Ledger threshold
uint64_t work_threshold (futurehead::work_version const, futurehead::block_details const);

namespace work_v1
{
	uint64_t value (futurehead::root const & root_a, uint64_t work_a);
	uint64_t threshold_base ();
	uint64_t threshold_entry ();
	uint64_t threshold (futurehead::block_details const);
}

double normalized_multiplier (double const, uint64_t const);
double denormalized_multiplier (double const, uint64_t const);
class opencl_work;
class work_item final
{
public:
	work_item (futurehead::work_version const version_a, futurehead::root const & item_a, uint64_t difficulty_a, std::function<void(boost::optional<uint64_t> const &)> const & callback_a) :
	version (version_a), item (item_a), difficulty (difficulty_a), callback (callback_a)
	{
	}
	futurehead::work_version const version;
	futurehead::root const item;
	uint64_t const difficulty;
	std::function<void(boost::optional<uint64_t> const &)> const callback;
};
class work_pool final
{
public:
	work_pool (unsigned, std::chrono::nanoseconds = std::chrono::nanoseconds (0), std::function<boost::optional<uint64_t> (futurehead::work_version const, futurehead::root const &, uint64_t, std::atomic<int> &)> = nullptr);
	~work_pool ();
	void loop (uint64_t);
	void stop ();
	void cancel (futurehead::root const &);
	void generate (futurehead::work_version const, futurehead::root const &, uint64_t, std::function<void(boost::optional<uint64_t> const &)>);
	boost::optional<uint64_t> generate (futurehead::work_version const, futurehead::root const &, uint64_t);
	// For tests only
	boost::optional<uint64_t> generate (futurehead::root const &);
	boost::optional<uint64_t> generate (futurehead::root const &, uint64_t);
	size_t size ();
	futurehead::network_constants network_constants;
	std::atomic<int> ticket;
	bool done;
	std::vector<boost::thread> threads;
	std::list<futurehead::work_item> pending;
	std::mutex mutex;
	futurehead::condition_variable producer_condition;
	std::chrono::nanoseconds pow_rate_limiter;
	std::function<boost::optional<uint64_t> (futurehead::work_version const, futurehead::root const &, uint64_t, std::atomic<int> &)> opencl;
	futurehead::observer_set<bool> work_observers;
};

std::unique_ptr<container_info_component> collect_container_info (work_pool & work_pool, const std::string & name);
}
