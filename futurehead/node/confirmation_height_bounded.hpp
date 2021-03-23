#pragma once

#include <futurehead/lib/numbers.hpp>
#include <futurehead/lib/threading.hpp>
#include <futurehead/secure/blockstore.hpp>

#include <boost/circular_buffer.hpp>

namespace futurehead
{
class ledger;
class read_transaction;
class logger_mt;
class write_database_queue;
class write_guard;

class confirmation_height_bounded final
{
public:
	confirmation_height_bounded (futurehead::ledger &, futurehead::write_database_queue &, std::chrono::milliseconds, futurehead::logger_mt &, std::atomic<bool> &, futurehead::block_hash const &, uint64_t &, std::function<void(std::vector<std::shared_ptr<futurehead::block>> const &)> const &, std::function<void(futurehead::block_hash const &)> const &, std::function<uint64_t ()> const &);
	bool pending_empty () const;
	void clear_process_vars ();
	void process ();
	void cement_blocks (futurehead::write_guard & scoped_write_guard_a);

private:
	class top_and_next_hash final
	{
	public:
		futurehead::block_hash top;
		boost::optional<futurehead::block_hash> next;
		uint64_t next_height;
	};

	class confirmed_info
	{
	public:
		confirmed_info (uint64_t confirmed_height_a, futurehead::block_hash const & iterated_frontier);
		uint64_t confirmed_height;
		futurehead::block_hash iterated_frontier;
	};

	class write_details final
	{
	public:
		write_details (futurehead::account const &, uint64_t, futurehead::block_hash const &, uint64_t, futurehead::block_hash const &);
		futurehead::account account;
		// This is the first block hash (bottom most) which is not cemented
		uint64_t bottom_height;
		futurehead::block_hash bottom_hash;
		// Desired cemented frontier
		uint64_t top_height;
		futurehead::block_hash top_hash;
	};

	/** The maximum number of blocks to be read in while iterating over a long account chain */
	uint64_t const batch_read_size = 65536;

	/** The maximum number of various containers to keep the memory bounded */
	uint32_t const max_items{ 131072 };

	// All of the atomic variables here just track the size for use in collect_container_info.
	// This is so that no mutexes are needed during the algorithm itself, which would otherwise be needed
	// for the sake of a rarely used RPC call for debugging purposes. As such the sizes are not being acted
	// upon in any way (does not synchronize with any other data).
	// This allows the load and stores to use relaxed atomic memory ordering.
	std::deque<write_details> pending_writes;
	futurehead::relaxed_atomic_integral<uint64_t> pending_writes_size{ 0 };
	uint32_t const pending_writes_max_size{ max_items };
	/* Holds confirmation height/cemented frontier in memory for accounts while iterating */
	std::unordered_map<account, confirmed_info> accounts_confirmed_info;
	futurehead::relaxed_atomic_integral<uint64_t> accounts_confirmed_info_size{ 0 };

	class receive_chain_details final
	{
	public:
		receive_chain_details (futurehead::account const &, uint64_t, futurehead::block_hash const &, futurehead::block_hash const &, boost::optional<futurehead::block_hash>, uint64_t, futurehead::block_hash const &);
		futurehead::account account;
		uint64_t height;
		futurehead::block_hash hash;
		futurehead::block_hash top_level;
		boost::optional<futurehead::block_hash> next;
		uint64_t bottom_height;
		futurehead::block_hash bottom_most;
	};

	class preparation_data final
	{
	public:
		futurehead::transaction const & transaction;
		futurehead::block_hash const & top_most_non_receive_block_hash;
		bool already_cemented;
		boost::circular_buffer_space_optimized<futurehead::block_hash> & checkpoints;
		decltype (accounts_confirmed_info.begin ()) account_it;
		futurehead::confirmation_height_info const & confirmation_height_info;
		futurehead::account const & account;
		uint64_t bottom_height;
		futurehead::block_hash const & bottom_most;
		boost::optional<receive_chain_details> & receive_details;
		boost::optional<top_and_next_hash> & next_in_receive_chain;
	};

	class receive_source_pair final
	{
	public:
		receive_source_pair (receive_chain_details const &, const futurehead::block_hash &);

		receive_chain_details receive_details;
		futurehead::block_hash source_hash;
	};

	futurehead::timer<std::chrono::milliseconds> timer;

	top_and_next_hash get_next_block (boost::optional<top_and_next_hash> const &, boost::circular_buffer_space_optimized<futurehead::block_hash> const &, boost::circular_buffer_space_optimized<receive_source_pair> const & receive_source_pairs, boost::optional<receive_chain_details> &);
	futurehead::block_hash get_least_unconfirmed_hash_from_top_level (futurehead::transaction const &, futurehead::block_hash const &, futurehead::account const &, futurehead::confirmation_height_info const &, uint64_t &);
	void prepare_iterated_blocks_for_cementing (preparation_data &);
	bool iterate (futurehead::read_transaction const &, uint64_t, futurehead::block_hash const &, boost::circular_buffer_space_optimized<futurehead::block_hash> &, futurehead::block_hash &, futurehead::block_hash const &, boost::circular_buffer_space_optimized<receive_source_pair> &, futurehead::account const &);

	futurehead::ledger & ledger;
	futurehead::write_database_queue & write_database_queue;
	std::chrono::milliseconds batch_separate_pending_min_time;
	futurehead::logger_mt & logger;
	std::atomic<bool> & stopped;
	futurehead::block_hash const & original_hash;
	uint64_t & batch_write_size;
	std::function<void(std::vector<std::shared_ptr<futurehead::block>> const &)> notify_observers_callback;
	std::function<void(futurehead::block_hash const &)> notify_block_already_cemented_observers_callback;
	std::function<uint64_t ()> awaiting_processing_size_callback;
	futurehead::network_params network_params;

	friend std::unique_ptr<futurehead::container_info_component> collect_container_info (confirmation_height_bounded &, const std::string & name_a);
};

std::unique_ptr<futurehead::container_info_component> collect_container_info (confirmation_height_bounded &, const std::string & name_a);
}