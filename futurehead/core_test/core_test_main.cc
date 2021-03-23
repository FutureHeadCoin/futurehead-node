#include "gtest/gtest.h"

#include <futurehead/node/common.hpp>
#include <futurehead/node/logging.hpp>

#include <boost/filesystem/path.hpp>

namespace futurehead
{
void cleanup_test_directories_on_exit ();
void force_futurehead_test_network ();
boost::filesystem::path unique_path ();
}

GTEST_API_ int main (int argc, char ** argv)
{
	printf ("Running main() from core_test_main.cc\n");
	futurehead::force_futurehead_test_network ();
	futurehead::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	// Setting up logging so that there aren't any piped to standard output.
	futurehead::logging logging;
	logging.init (futurehead::unique_path ());
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	futurehead::cleanup_test_directories_on_exit ();
	return res;
}
