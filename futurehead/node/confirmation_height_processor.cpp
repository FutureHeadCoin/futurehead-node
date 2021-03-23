#include <futurehead/lib/logger_mt.hpp>
#include <futurehead/lib/numbers.hpp>
#include <futurehead/lib/threading.hpp>
#include <futurehead/lib/utility.hpp>
#include <futurehead/node/confirmation_height_processor.hpp>
#include <futurehead/node/write_database_queue.hpp>
#include <futurehead/secure/common.hpp>
#include <futurehead/secure/ledger.hpp>

#include <boost/thread/latch.hpp>

#include <numeric>

futurehead::confirmation_height_processor::confirmation_height_processor (futurehead::ledger & ledger_a, futurehead::write_database_queue & write_database_queue_a, std::chrono::milliseconds batch_separate_pending_min_time_a, futurehead::logger_mt & logger_a, boost::latch & latch, confirmation_height_mode mode_a) :
ledger (ledger_a),
write_database_queue (write_database_queue_a),
// clang-format off
unbounded_processor (ledger_a, write_database_queue_a, batch_separate_pending_min_time_a, logger_a, stopped, original_hash, batch_write_size, [this](auto & cemented_blocks) { this->notify_observers (cemented_blocks); }, [this](auto const & block_hash_a) { this->notify_observers (block_hash_a); }, [this]() { return this->awaiting_processing_size (); }),
bounded_processor (ledger_a, write_database_queue_a, batch_separate_pending_min_time_a, logger_a, stopped, original_hash, batch_write_size, [this](auto & cemented_blocks) { this->notify_observers (cemented_blocks); }, [this](auto const & block_hash_a) { this->notify_observers (block_hash_a); }, [this]() { return this->awaiting_processing_size (); }),
// clang-format on
thread ([this, &latch, mode_a]() {
	futurehead::thread_role::set (futurehead::thread_role::name::confirmation_height_processing);
	// Do not start running the processing thread until other threads have finished their operations
	latch.wait ();
	this->run (mode_a);
})
{
}

futurehead::confirmation_height_processor::~confirmation_height_processor ()
{
	stop ();
}

void futurehead::confirmation_height_processor::stop ()
{
	{
		futurehead::lock_guard<std::mutex> guard (mutex);
		stopped = true;
	}
	condition.notify_one ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void futurehead::confirmation_height_processor::run (confirmation_height_mode mode_a)
{
	futurehead::unique_lock<std::mutex> lk (mutex);
	while (!stopped)
	{
		if (!paused && !awaiting_processing.empty ())
		{
			lk.unlock ();
			if (bounded_processor.pending_empty () && unbounded_processor.pending_empty ())
			{
				lk.lock ();
				original_hashes_pending.clear ();
				lk.unlock ();
			}

			set_next_hash ();

			const auto num_blocks_to_use_unbounded = confirmation_height::unbounded_cutoff;
			auto blocks_within_automatic_unbounded_selection = (ledger.cache.block_count < num_blocks_to_use_unbounded || ledger.cache.block_count - num_blocks_to_use_unbounded < ledger.cache.cemented_count);

			// Don't want to mix up pending writes across different processors
			auto valid_unbounded = (mode_a == confirmation_height_mode::automatic && blocks_within_automatic_unbounded_selection && bounded_processor.pending_empty ());
			auto force_unbounded = (!unbounded_processor.pending_empty () || mode_a == confirmation_height_mode::unbounded);
			if (force_unbounded || valid_unbounded)
			{
				debug_assert (bounded_processor.pending_empty ());
				unbounded_processor.process ();
			}
			else
			{
				debug_assert (mode_a == confirmation_height_mode::bounded || mode_a == confirmation_height_mode::automatic);
				debug_assert (unbounded_processor.pending_empty ());
				bounded_processor.process ();
			}

			lk.lock ();
		}
		else
		{
			auto lock_and_cleanup = [&lk, this]() {
				lk.lock ();
				original_hash.clear ();
				original_hashes_pending.clear ();
				bounded_processor.clear_process_vars ();
				unbounded_processor.clear_process_vars ();
			};

			if (!paused)
			{
				lk.unlock ();

				// If there are blocks pending cementing, then make sure we flush out the remaining writes
				if (!bounded_processor.pending_empty ())
				{
					debug_assert (unbounded_processor.pending_empty ());
					{
						auto scoped_write_guard = write_database_queue.wait (futurehead::writer::confirmation_height);
						bounded_processor.cement_blocks (scoped_write_guard);
					}
					lock_and_cleanup ();
				}
				else if (!unbounded_processor.pending_empty ())
				{
					debug_assert (bounded_processor.pending_empty ());
					{
						auto scoped_write_guard = write_database_queue.wait (futurehead::writer::confirmation_height);
						unbounded_processor.cement_blocks (scoped_write_guard);
					}
					lock_and_cleanup ();
				}
				else
				{
					lock_and_cleanup ();
					condition.wait (lk);
				}
			}
			else
			{
				original_hash.clear ();
				condition.wait (lk);
			}
		}
	}
}

// Pausing only affects processing new blocks, not the current one being processed. Currently only used in tests
void futurehead::confirmation_height_processor::pause ()
{
	futurehead::lock_guard<std::mutex> lk (mutex);
	paused = true;
}

void futurehead::confirmation_height_processor::unpause ()
{
	{
		futurehead::lock_guard<std::mutex> lk (mutex);
		paused = false;
	}
	condition.notify_one ();
}

void futurehead::confirmation_height_processor::add (futurehead::block_hash const & hash_a)
{
	{
		futurehead::lock_guard<std::mutex> lk (mutex);
		awaiting_processing.get<tag_sequence> ().emplace_back (hash_a);
	}
	condition.notify_one ();
}

void futurehead::confirmation_height_processor::set_next_hash ()
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	debug_assert (!awaiting_processing.empty ());
	original_hash = awaiting_processing.get<tag_sequence> ().front ();
	original_hashes_pending.insert (original_hash);
	awaiting_processing.get<tag_sequence> ().pop_front ();
}

// Not thread-safe, only call before this processor has begun cementing
void futurehead::confirmation_height_processor::add_cemented_observer (std::function<void(std::shared_ptr<futurehead::block>)> const & callback_a)
{
	cemented_observers.push_back (callback_a);
}

// Not thread-safe, only call before this processor has begun cementing
void futurehead::confirmation_height_processor::add_block_already_cemented_observer (std::function<void(futurehead::block_hash const &)> const & callback_a)
{
	block_already_cemented_observers.push_back (callback_a);
}

void futurehead::confirmation_height_processor::notify_observers (std::vector<std::shared_ptr<futurehead::block>> const & cemented_blocks)
{
	for (auto const & block_callback_data : cemented_blocks)
	{
		for (auto const & observer : cemented_observers)
		{
			observer (block_callback_data);
		}
	}
}

void futurehead::confirmation_height_processor::notify_observers (futurehead::block_hash const & hash_already_cemented_a)
{
	for (auto const & observer : block_already_cemented_observers)
	{
		observer (hash_already_cemented_a);
	}
}

std::unique_ptr<futurehead::container_info_component> futurehead::collect_container_info (confirmation_height_processor & confirmation_height_processor_a, const std::string & name_a)
{
	auto composite = std::make_unique<container_info_composite> (name_a);

	size_t cemented_observers_count = confirmation_height_processor_a.cemented_observers.size ();
	size_t block_already_cemented_observers_count = confirmation_height_processor_a.block_already_cemented_observers.size ();
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "cemented_observers", cemented_observers_count, sizeof (decltype (confirmation_height_processor_a.cemented_observers)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "block_already_cemented_observers", block_already_cemented_observers_count, sizeof (decltype (confirmation_height_processor_a.block_already_cemented_observers)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "awaiting_processing", confirmation_height_processor_a.awaiting_processing_size (), sizeof (decltype (confirmation_height_processor_a.awaiting_processing)::value_type) }));
	composite->add_component (collect_container_info (confirmation_height_processor_a.bounded_processor, "bounded_processor"));
	composite->add_component (collect_container_info (confirmation_height_processor_a.unbounded_processor, "unbounded_processor"));
	return composite;
}

size_t futurehead::confirmation_height_processor::awaiting_processing_size ()
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	return awaiting_processing.size ();
}

bool futurehead::confirmation_height_processor::is_processing_block (futurehead::block_hash const & hash_a)
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	return original_hashes_pending.count (hash_a) > 0 || awaiting_processing.get<tag_hash> ().count (hash_a) > 0;
}

futurehead::block_hash futurehead::confirmation_height_processor::current ()
{
	futurehead::lock_guard<std::mutex> lk (mutex);
	return original_hash;
}
