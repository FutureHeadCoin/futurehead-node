#include <futurehead/crypto_lib/random_pool.hpp>
#include <futurehead/lib/blocks.hpp>
#include <futurehead/lib/jsonconfig.hpp>
#include <futurehead/lib/logger_mt.hpp>
#include <futurehead/lib/timer.hpp>
#include <futurehead/lib/work.hpp>
#include <futurehead/node/logging.hpp>
#include <futurehead/node/openclconfig.hpp>
#include <futurehead/node/openclwork.hpp>
#include <futurehead/secure/common.hpp>
#include <futurehead/secure/utility.hpp>

#include <gtest/gtest.h>

#include <future>

TEST (work, one)
{
	futurehead::network_constants network_constants;
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::change_block block (1, 1, futurehead::keypair ().prv, 3, 4);
	block.block_work_set (*pool.generate (block.root ()));
	ASSERT_LT (futurehead::work_threshold_base (block.work_version ()), block.difficulty ());
}

TEST (work, disabled)
{
	futurehead::network_constants network_constants;
	futurehead::work_pool pool (0);
	auto result (pool.generate (futurehead::block_hash ()));
	ASSERT_FALSE (result.is_initialized ());
}

TEST (work, validate)
{
	futurehead::network_constants network_constants;
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::send_block send_block (1, 1, 2, futurehead::keypair ().prv, 4, 6);
	ASSERT_LT (send_block.difficulty (), futurehead::work_threshold_base (send_block.work_version ()));
	send_block.block_work_set (*pool.generate (send_block.root ()));
	ASSERT_LT (futurehead::work_threshold_base (send_block.work_version ()), send_block.difficulty ());
}

TEST (work, cancel)
{
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	auto iterations (0);
	auto done (false);
	while (!done)
	{
		futurehead::root key (1);
		pool.generate (
		futurehead::work_version::work_1, key, futurehead::network_constants ().publish_thresholds.base, [&done](boost::optional<uint64_t> work_a) {
			done = !work_a;
		});
		pool.cancel (key);
		++iterations;
		ASSERT_LT (iterations, 200);
	}
}

TEST (work, cancel_many)
{
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::root key1 (1);
	futurehead::root key2 (2);
	futurehead::root key3 (1);
	futurehead::root key4 (1);
	futurehead::root key5 (3);
	futurehead::root key6 (1);
	futurehead::network_constants constants;
	pool.generate (futurehead::work_version::work_1, key1, constants.publish_thresholds.base, [](boost::optional<uint64_t>) {});
	pool.generate (futurehead::work_version::work_1, key2, constants.publish_thresholds.base, [](boost::optional<uint64_t>) {});
	pool.generate (futurehead::work_version::work_1, key3, constants.publish_thresholds.base, [](boost::optional<uint64_t>) {});
	pool.generate (futurehead::work_version::work_1, key4, constants.publish_thresholds.base, [](boost::optional<uint64_t>) {});
	pool.generate (futurehead::work_version::work_1, key5, constants.publish_thresholds.base, [](boost::optional<uint64_t>) {});
	pool.generate (futurehead::work_version::work_1, key6, constants.publish_thresholds.base, [](boost::optional<uint64_t>) {});
	pool.cancel (key1);
}

TEST (work, opencl)
{
	futurehead::logging logging;
	logging.init (futurehead::unique_path ());
	futurehead::logger_mt logger;
	bool error (false);
	futurehead::opencl_environment environment (error);
	ASSERT_FALSE (error);
	if (!environment.platforms.empty () && !environment.platforms.begin ()->devices.empty ())
	{
		futurehead::opencl_config config (0, 0, 16 * 1024);
		auto opencl (futurehead::opencl_work::create (true, config, logger));
		if (opencl != nullptr)
		{
			// 0 threads, should add 1 for managing OpenCL
			futurehead::work_pool pool (0, std::chrono::nanoseconds (0), [&opencl](futurehead::work_version const version_a, futurehead::root const & root_a, uint64_t difficulty_a, std::atomic<int> & ticket_a) {
				return opencl->generate_work (version_a, root_a, difficulty_a);
			});
			ASSERT_NE (nullptr, pool.opencl);
			futurehead::root root;
			uint64_t difficulty (0xff00000000000000);
			uint64_t difficulty_add (0x000f000000000000);
			for (auto i (0); i < 16; ++i)
			{
				futurehead::random_pool::generate_block (root.bytes.data (), root.bytes.size ());
				auto result (*pool.generate (futurehead::work_version::work_1, root, difficulty));
				ASSERT_GE (futurehead::work_difficulty (futurehead::work_version::work_1, root, result), difficulty);
				difficulty += difficulty_add;
			}
		}
		else
		{
			std::cerr << "Error starting OpenCL test" << std::endl;
		}
	}
	else
	{
		std::cout << "Device with OpenCL support not found. Skipping OpenCL test" << std::endl;
	}
}

TEST (work, opencl_config)
{
	futurehead::opencl_config config1;
	config1.platform = 1;
	config1.device = 2;
	config1.threads = 3;
	futurehead::jsonconfig tree;
	config1.serialize_json (tree);
	futurehead::opencl_config config2;
	ASSERT_FALSE (config2.deserialize_json (tree));
	ASSERT_EQ (1, config2.platform);
	ASSERT_EQ (2, config2.device);
	ASSERT_EQ (3, config2.threads);
}

TEST (work, difficulty)
{
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::root root (1);
	uint64_t difficulty1 (0xff00000000000000);
	uint64_t difficulty2 (0xfff0000000000000);
	uint64_t difficulty3 (0xffff000000000000);
	uint64_t result_difficulty1 (0);
	do
	{
		auto work1 = *pool.generate (futurehead::work_version::work_1, root, difficulty1);
		result_difficulty1 = futurehead::work_difficulty (futurehead::work_version::work_1, root, work1);
	} while (result_difficulty1 > difficulty2);
	ASSERT_GT (result_difficulty1, difficulty1);
	uint64_t result_difficulty2 (0);
	do
	{
		auto work2 = *pool.generate (futurehead::work_version::work_1, root, difficulty2);
		result_difficulty2 = futurehead::work_difficulty (futurehead::work_version::work_1, root, work2);
	} while (result_difficulty2 > difficulty3);
	ASSERT_GT (result_difficulty2, difficulty2);
}

TEST (work, eco_pow)
{
	auto work_func = [](std::promise<std::chrono::nanoseconds> & promise, std::chrono::nanoseconds interval) {
		futurehead::work_pool pool (1, interval);
		constexpr auto num_iterations = 5;

		futurehead::timer<std::chrono::nanoseconds> timer;
		timer.start ();
		for (int i = 0; i < num_iterations; ++i)
		{
			futurehead::root root (1);
			uint64_t difficulty1 (0xff00000000000000);
			uint64_t difficulty2 (0xfff0000000000000);
			uint64_t result_difficulty (0);
			do
			{
				auto work = *pool.generate (futurehead::work_version::work_1, root, difficulty1);
				result_difficulty = futurehead::work_difficulty (futurehead::work_version::work_1, root, work);
			} while (result_difficulty > difficulty2);
			ASSERT_GT (result_difficulty, difficulty1);
		}

		promise.set_value_at_thread_exit (timer.stop ());
	};

	std::promise<std::chrono::nanoseconds> promise1;
	std::future<std::chrono::nanoseconds> future1 = promise1.get_future ();
	std::promise<std::chrono::nanoseconds> promise2;
	std::future<std::chrono::nanoseconds> future2 = promise2.get_future ();

	std::thread thread1 (work_func, std::ref (promise1), std::chrono::nanoseconds (0));
	std::thread thread2 (work_func, std::ref (promise2), std::chrono::milliseconds (10));

	thread1.join ();
	thread2.join ();

	// Confirm that the eco pow rate limiter is working.
	// It's possible under some unlucky circumstances that this fails to the random nature of valid work generation.
	ASSERT_LT (future1.get (), future2.get ());
}
