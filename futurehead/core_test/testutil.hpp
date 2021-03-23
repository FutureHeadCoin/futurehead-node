#pragma once

#include <futurehead/lib/locks.hpp>
#include <futurehead/lib/timer.hpp>

// This test header is unfortunately included in many different files.
// Requires guarding in some cases.
#ifndef IGNORE_GTEST_INCL
#include <futurehead/node/common.hpp>

#include <gtest/gtest.h>
#endif

#include <boost/iostreams/concepts.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>

#define GTEST_TEST_ERROR_CODE(expression, text, actual, expected, fail)                       \
	GTEST_AMBIGUOUS_ELSE_BLOCKER_                                                             \
	if (const ::testing::AssertionResult gtest_ar_ = ::testing::AssertionResult (expression)) \
		;                                                                                     \
	else                                                                                      \
		fail (::testing::internal::GetBoolAssertionFailureMessage (                           \
		gtest_ar_, text, actual, expected)                                                    \
		      .c_str ())

/** Extends gtest with a std::error_code assert that prints the error code message when non-zero */
#define ASSERT_NO_ERROR(condition)                                                      \
	GTEST_TEST_ERROR_CODE (!(condition), #condition, condition.message ().c_str (), "", \
	GTEST_FATAL_FAILURE_)

/** Extends gtest with a std::error_code assert that expects an error */
#define ASSERT_IS_ERROR(condition)                                                            \
	GTEST_TEST_ERROR_CODE ((condition.value () > 0), #condition, "An error was expected", "", \
	GTEST_FATAL_FAILURE_)

/** Asserts that the condition becomes true within the deadline */
#define ASSERT_TIMELY(time, condition)    \
	system.deadline_set (time);           \
	while (!(condition))                  \
	{                                     \
		ASSERT_NO_ERROR (system.poll ()); \
	}

/* Convenience globals for core_test */
namespace futurehead
{
using uint128_t = boost::multiprecision::uint128_t;
class keypair;
class public_key;
class block_hash;
extern futurehead::keypair const & zero_key;
extern futurehead::keypair const & test_genesis_key;
extern std::string const & futurehead_test_genesis;
extern std::string const & genesis_block;
extern futurehead::block_hash const & genesis_hash;
extern futurehead::public_key const & futurehead_test_account;
extern futurehead::public_key const & genesis_account;
extern futurehead::public_key const & burn_account;
extern futurehead::uint128_t const & genesis_amount;

class stringstream_mt_sink : public boost::iostreams::sink
{
public:
	stringstream_mt_sink () = default;
	stringstream_mt_sink (stringstream_mt_sink const & sink)
	{
		futurehead::lock_guard<std::mutex> guard (mutex);
		ss << sink.ss.str ();
	}

	std::streamsize write (const char * string_to_write, std::streamsize size)
	{
		futurehead::lock_guard<std::mutex> guard (mutex);
		ss << std::string (string_to_write, size);
		return size;
	}

	std::string str ()
	{
		futurehead::lock_guard<std::mutex> guard (mutex);
		return ss.str ();
	}

private:
	mutable std::mutex mutex;
	std::stringstream ss;
};

class boost_log_cerr_redirect
{
public:
	boost_log_cerr_redirect (std::streambuf * new_buffer) :
	old (std::cerr.rdbuf (new_buffer))
	{
		console_sink = (boost::log::add_console_log (std::cerr, boost::log::keywords::format = "%Message%"));
	}

	~boost_log_cerr_redirect ()
	{
		std::cerr.rdbuf (old);
		boost::log::core::get ()->remove_sink (console_sink);
	}

private:
	std::streambuf * old;
	boost::shared_ptr<boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>> console_sink;
};

class cout_redirect
{
public:
	cout_redirect (std::streambuf * new_buffer)
	{
		std::cout.rdbuf (new_buffer);
	}

	~cout_redirect ()
	{
		std::cout.rdbuf (old);
	}

private:
	std::streambuf * old{ std::cout.rdbuf () };
};

namespace util
{
	/**
	 * Helper to signal completion of async handlers in tests.
	 * Subclasses implement specific conditions for completion.
	 */
	class completion_signal
	{
	public:
		virtual ~completion_signal ()
		{
			notify ();
		}

		/** Explicitly notify the completion */
		void notify ()
		{
			cv.notify_all ();
		}

	protected:
		futurehead::condition_variable cv;
		std::mutex mutex;
	};

	/**
	 * Signals completion when a count is reached.
	 */
	class counted_completion : public completion_signal
	{
	public:
		/**
		 * Constructor
		 * @param required_count_a When increment() reaches this count within the deadline, await_count_for() will return false.
		 */
		counted_completion (unsigned required_count_a) :
		required_count (required_count_a)
		{
		}

		/**
		 * Wait for increment() to signal completion, or reaching the deadline.
		 * @param deadline_duration_a Deadline as a std::chrono duration
		 * @return true if the count is reached within the deadline
		 */
		template <typename UNIT>
		bool await_count_for (UNIT deadline_duration_a)
		{
			futurehead::timer<UNIT> timer (futurehead::timer_state::started);
			bool error = true;
			while (error && timer.before_deadline (deadline_duration_a))
			{
				error = count < required_count;
				if (error)
				{
					futurehead::unique_lock<std::mutex> lock (mutex);
					cv.wait_for (lock, std::chrono::milliseconds (1));
				}
			}
			return error;
		}

		/** Increments the current count. If the required count is reached, await_count_for() waiters are notified. */
		unsigned increment ()
		{
			auto val (count.fetch_add (1));
			if (val >= required_count)
			{
				notify ();
			}
			return val;
		}

		void increment_required_count ()
		{
			++required_count;
		}

	private:
		std::atomic<unsigned> count{ 0 };
		std::atomic<unsigned> required_count;
	};
}

inline uint16_t get_available_port ()
{
	// Maximum possible sockets which may feasibly be used in 1 test
	constexpr auto max = 200;
	static uint16_t current = 0;
	// Read the TEST_BASE_PORT environment and override the default base port if it exists
	auto base_str = std::getenv ("TEST_BASE_PORT");
	uint16_t base_port = 24000;
	if (base_str)
	{
		base_port = boost::lexical_cast<uint16_t> (base_str);
	}

	uint16_t const available_port = base_port + current;
	++current;
	// Reset port number once we have reached the maximum
	if (current == max)
	{
		current = 0;
	}

	return available_port;
}

#ifndef IGNORE_GTEST_INCL
inline void compare_default_telemetry_response_data_excluding_signature (futurehead::telemetry_data const & telemetry_data_a, futurehead::network_params const & network_params_a, uint64_t bandwidth_limit_a, uint64_t active_difficulty_a)
{
	ASSERT_EQ (telemetry_data_a.block_count, 1);
	ASSERT_EQ (telemetry_data_a.cemented_count, 1);
	ASSERT_EQ (telemetry_data_a.bandwidth_cap, bandwidth_limit_a);
	ASSERT_EQ (telemetry_data_a.peer_count, 1);
	ASSERT_EQ (telemetry_data_a.protocol_version, network_params_a.protocol.telemetry_protocol_version_min);
	ASSERT_EQ (telemetry_data_a.unchecked_count, 0);
	ASSERT_EQ (telemetry_data_a.account_count, 1);
	ASSERT_LT (telemetry_data_a.uptime, 100);
	ASSERT_EQ (telemetry_data_a.genesis_block, network_params_a.ledger.genesis_hash);
	ASSERT_EQ (telemetry_data_a.major_version, futurehead::get_major_node_version ());
	ASSERT_EQ (telemetry_data_a.minor_version, futurehead::get_minor_node_version ());
	ASSERT_EQ (telemetry_data_a.patch_version, futurehead::get_patch_node_version ());
	ASSERT_EQ (telemetry_data_a.pre_release_version, futurehead::get_pre_release_node_version ());
	ASSERT_EQ (telemetry_data_a.maker, 0);
	ASSERT_GT (telemetry_data_a.timestamp, std::chrono::system_clock::now () - std::chrono::seconds (100));
	ASSERT_EQ (telemetry_data_a.active_difficulty, active_difficulty_a);
}

inline void compare_default_telemetry_response_data (futurehead::telemetry_data const & telemetry_data_a, futurehead::network_params const & network_params_a, uint64_t bandwidth_limit_a, uint64_t active_difficulty_a, futurehead::keypair const & node_id_a)
{
	ASSERT_FALSE (telemetry_data_a.validate_signature (futurehead::telemetry_data::size));
	futurehead::telemetry_data telemetry_data_l = telemetry_data_a;
	telemetry_data_l.signature.clear ();
	telemetry_data_l.sign (node_id_a);
	// Signature should be different because uptime/timestamp will have changed.
	ASSERT_NE (telemetry_data_a.signature, telemetry_data_l.signature);
	compare_default_telemetry_response_data_excluding_signature (telemetry_data_a, network_params_a, bandwidth_limit_a, active_difficulty_a);
	ASSERT_EQ (telemetry_data_a.node_id, node_id_a.pub);
}
#endif
}
