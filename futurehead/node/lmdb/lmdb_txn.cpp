#include <futurehead/lib/jsonconfig.hpp>
#include <futurehead/lib/logger_mt.hpp>
#include <futurehead/lib/threading.hpp>
#include <futurehead/lib/utility.hpp>
#include <futurehead/node/lmdb/lmdb_env.hpp>
#include <futurehead/node/lmdb/lmdb_txn.hpp>
#include <futurehead/secure/blockstore.hpp>

#include <boost/format.hpp>

// Some builds (mac) fail due to "Boost.Stacktrace requires `_Unwind_Backtrace` function".
#ifndef _WIN32
#ifdef FUTUREHEAD_STACKTRACE_BACKTRACE
#define BOOST_STACKTRACE_USE_BACKTRACE
#endif
#ifndef _GNU_SOURCE
#define BEFORE_GNU_SOURCE 0
#define _GNU_SOURCE
#else
#define BEFORE_GNU_SOURCE 1
#endif
#endif
// On Windows this include defines min/max macros, so keep below other includes
// to reduce conflicts with other std functions
#include <boost/stacktrace.hpp>
#ifndef _WIN32
#if !BEFORE_GNU_SOURCE
#undef _GNU_SOURCE
#endif
#endif

namespace
{
class matches_txn final
{
public:
	explicit matches_txn (const futurehead::transaction_impl * transaction_impl_a) :
	transaction_impl (transaction_impl_a)
	{
	}

	bool operator() (futurehead::mdb_txn_stats const & mdb_txn_stats)
	{
		return (mdb_txn_stats.transaction_impl == transaction_impl);
	}

private:
	const futurehead::transaction_impl * transaction_impl;
};
}

futurehead::read_mdb_txn::read_mdb_txn (futurehead::mdb_env const & environment_a, futurehead::mdb_txn_callbacks txn_callbacks_a) :
txn_callbacks (txn_callbacks_a)
{
	auto status (mdb_txn_begin (environment_a, nullptr, MDB_RDONLY, &handle));
	release_assert (status == 0);
	txn_callbacks.txn_start (this);
}

futurehead::read_mdb_txn::~read_mdb_txn ()
{
	// This uses commit rather than abort, as it is needed when opening databases with a read only transaction
	auto status (mdb_txn_commit (handle));
	release_assert (status == MDB_SUCCESS);
	txn_callbacks.txn_end (this);
}

void futurehead::read_mdb_txn::reset ()
{
	mdb_txn_reset (handle);
	txn_callbacks.txn_end (this);
}

void futurehead::read_mdb_txn::renew ()
{
	auto status (mdb_txn_renew (handle));
	release_assert (status == 0);
	txn_callbacks.txn_start (this);
}

void * futurehead::read_mdb_txn::get_handle () const
{
	return handle;
}

futurehead::write_mdb_txn::write_mdb_txn (futurehead::mdb_env const & environment_a, futurehead::mdb_txn_callbacks txn_callbacks_a) :
env (environment_a),
txn_callbacks (txn_callbacks_a)
{
	renew ();
}

futurehead::write_mdb_txn::~write_mdb_txn ()
{
	commit ();
}

void futurehead::write_mdb_txn::commit () const
{
	auto status (mdb_txn_commit (handle));
	release_assert (status == MDB_SUCCESS);
	txn_callbacks.txn_end (this);
}

void futurehead::write_mdb_txn::renew ()
{
	auto status (mdb_txn_begin (env, nullptr, 0, &handle));
	release_assert (status == MDB_SUCCESS);
	txn_callbacks.txn_start (this);
}

void * futurehead::write_mdb_txn::get_handle () const
{
	return handle;
}

bool futurehead::write_mdb_txn::contains (futurehead::tables table_a) const
{
	// LMDB locks on every write
	return true;
}

futurehead::mdb_txn_tracker::mdb_txn_tracker (futurehead::logger_mt & logger_a, futurehead::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a) :
logger (logger_a),
txn_tracking_config (txn_tracking_config_a),
block_processor_batch_max_time (block_processor_batch_max_time_a)
{
}

void futurehead::mdb_txn_tracker::serialize_json (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time)
{
	// Copying is cheap compared to generating the stack trace strings, so reduce time holding the mutex
	std::vector<mdb_txn_stats> copy_stats;
	std::vector<bool> are_writes;
	{
		futurehead::lock_guard<std::mutex> guard (mutex);
		copy_stats = stats;
		are_writes.reserve (stats.size ());
		std::transform (stats.cbegin (), stats.cend (), std::back_inserter (are_writes), [](auto & mdb_txn_stat) {
			return mdb_txn_stat.is_write ();
		});
	}

	// Get the time difference now as creating stacktraces (Debug/Windows for instance) can take a while so results won't be as accurate
	std::vector<std::chrono::milliseconds> times_since_start;
	times_since_start.reserve (copy_stats.size ());
	std::transform (copy_stats.cbegin (), copy_stats.cend (), std::back_inserter (times_since_start), [](const auto & stat) {
		return stat.timer.since_start ();
	});
	debug_assert (times_since_start.size () == copy_stats.size ());

	for (size_t i = 0; i < times_since_start.size (); ++i)
	{
		auto const & stat = copy_stats[i];
		auto time_held_open = times_since_start[i];

		if ((are_writes[i] && time_held_open >= min_write_time) || (!are_writes[i] && time_held_open >= min_read_time))
		{
			futurehead::jsonconfig mdb_lock_config;

			mdb_lock_config.put ("thread", stat.thread_name);
			mdb_lock_config.put ("time_held_open", time_held_open.count ());
			mdb_lock_config.put ("write", !!are_writes[i]);

			boost::property_tree::ptree stacktrace_config;
			for (auto frame : *stat.stacktrace)
			{
				futurehead::jsonconfig frame_json;
				frame_json.put ("name", frame.name ());
				frame_json.put ("address", frame.address ());
				frame_json.put ("source_file", frame.source_file ());
				frame_json.put ("source_line", frame.source_line ());
				stacktrace_config.push_back (std::make_pair ("", frame_json.get_tree ()));
			}

			futurehead::jsonconfig stack (stacktrace_config);
			mdb_lock_config.put_child ("stacktrace", stack);
			json.push_back (std::make_pair ("", mdb_lock_config.get_tree ()));
		}
	}
}

void futurehead::mdb_txn_tracker::output_finished (futurehead::mdb_txn_stats const & mdb_txn_stats) const
{
	// Only output them if transactions were held for longer than a certain period of time
	auto is_write = mdb_txn_stats.is_write ();
	auto time_open = mdb_txn_stats.timer.since_start ();

	auto should_ignore = false;
	// Reduce noise in log files by removing any entries from the block processor (if enabled) which are less than the max batch time (+ a few second buffer) because these are expected writes during bootstrapping.
	auto is_below_max_time = time_open <= (block_processor_batch_max_time + std::chrono::seconds (3));
	bool is_blk_processing_thread = mdb_txn_stats.thread_name == futurehead::thread_role::get_string (futurehead::thread_role::name::block_processing);
	if (txn_tracking_config.ignore_writes_below_block_processor_max_time && is_blk_processing_thread && is_write && is_below_max_time)
	{
		should_ignore = true;
	}

	if (!should_ignore && ((is_write && time_open >= txn_tracking_config.min_write_txn_time) || (!is_write && time_open >= txn_tracking_config.min_read_txn_time)))
	{
		debug_assert (mdb_txn_stats.stacktrace);
		logger.always_log (boost::str (boost::format ("%1%ms %2% held on thread %3%\n%4%") % mdb_txn_stats.timer.since_start ().count () % (is_write ? "write lock" : "read") % mdb_txn_stats.thread_name % *mdb_txn_stats.stacktrace));
	}
}

void futurehead::mdb_txn_tracker::add (const futurehead::transaction_impl * transaction_impl)
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	debug_assert (std::find_if (stats.cbegin (), stats.cend (), matches_txn (transaction_impl)) == stats.cend ());
	stats.emplace_back (transaction_impl);
}

/** Can be called without error if transaction does not exist */
void futurehead::mdb_txn_tracker::erase (const futurehead::transaction_impl * transaction_impl)
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	auto it = std::find_if (stats.begin (), stats.end (), matches_txn (transaction_impl));
	if (it != stats.end ())
	{
		output_finished (*it);
		it->timer.stop ();
		stats.erase (it);
	}
}

futurehead::mdb_txn_stats::mdb_txn_stats (const futurehead::transaction_impl * transaction_impl) :
transaction_impl (transaction_impl),
thread_name (futurehead::thread_role::get_string ()),
stacktrace (std::make_shared<boost::stacktrace::stacktrace> ())
{
	timer.start ();
}

bool futurehead::mdb_txn_stats::is_write () const
{
	return (dynamic_cast<const futurehead::write_transaction_impl *> (transaction_impl) != nullptr);
}
