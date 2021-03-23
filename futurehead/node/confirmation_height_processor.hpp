#pragma once

#include <futurehead/lib/numbers.hpp>
#include <futurehead/node/confirmation_height_bounded.hpp>
#include <futurehead/node/confirmation_height_unbounded.hpp>
#include <futurehead/secure/blockstore.hpp>
#include <futurehead/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace mi = boost::multi_index;
namespace boost
{
class latch;
}
namespace futurehead
{
class ledger;
class logger_mt;
class write_database_queue;

class confirmation_height_processor final
{
public:
	confirmation_height_processor (futurehead::ledger &, futurehead::write_database_queue &, std::chrono::milliseconds, futurehead::logger_mt &, boost::latch & initialized_latch, confirmation_height_mode = confirmation_height_mode::automatic);
	~confirmation_height_processor ();
	void pause ();
	void unpause ();
	void stop ();
	void add (futurehead::block_hash const & hash_a);
	void run (confirmation_height_mode);
	size_t awaiting_processing_size ();
	bool is_processing_block (futurehead::block_hash const &);
	futurehead::block_hash current ();

	void add_cemented_observer (std::function<void(std::shared_ptr<futurehead::block>)> const &);
	void add_block_already_cemented_observer (std::function<void(futurehead::block_hash const &)> const &);

private:
	std::mutex mutex;
	// Hashes which have been added to the confirmation height processor, but not yet processed
	// clang-format off
	class tag_sequence {};
	class tag_hash {};
	boost::multi_index_container<futurehead::block_hash,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequence>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::identity<futurehead::block_hash>>>> awaiting_processing;
	// clang-format on

	// Hashes which have been added and processed, but have not been cemented
	std::unordered_set<futurehead::block_hash> original_hashes_pending;
	bool paused{ false };

	/** This is the last block popped off the confirmation height pending collection */
	futurehead::block_hash original_hash{ 0 };

	futurehead::condition_variable condition;
	std::atomic<bool> stopped{ false };
	// No mutex needed for the observers as these should be set up during initialization of the node
	std::vector<std::function<void(std::shared_ptr<futurehead::block>)>> cemented_observers;
	std::vector<std::function<void(futurehead::block_hash const &)>> block_already_cemented_observers;

	futurehead::ledger & ledger;
	futurehead::write_database_queue & write_database_queue;
	/** The maximum amount of blocks to write at once. This is dynamically modified by the bounded processor based on previous write performance **/
	uint64_t batch_write_size{ 16384 };

	confirmation_height_unbounded unbounded_processor;
	confirmation_height_bounded bounded_processor;
	std::thread thread;

	void set_next_hash ();
	void notify_observers (std::vector<std::shared_ptr<futurehead::block>> const &);
	void notify_observers (futurehead::block_hash const &);

	friend std::unique_ptr<container_info_component> collect_container_info (confirmation_height_processor &, const std::string &);
	friend class confirmation_height_pending_observer_callbacks_Test;
	friend class confirmation_height_dependent_election_Test;
	friend class confirmation_height_dependent_election_after_already_cemented_Test;
	friend class confirmation_height_dynamic_algorithm_no_transition_while_pending_Test;
	friend class confirmation_height_many_accounts_many_confirmations_Test;
	friend class confirmation_height_long_chains_Test;
	friend class confirmation_height_many_accounts_single_confirmation_Test;
};

std::unique_ptr<container_info_component> collect_container_info (confirmation_height_processor &, const std::string &);
}
