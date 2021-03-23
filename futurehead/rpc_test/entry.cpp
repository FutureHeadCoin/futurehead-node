#include <futurehead/lib/memory.hpp>
#include <futurehead/node/common.hpp>

#include <gtest/gtest.h>
namespace futurehead
{
void cleanup_test_directories_on_exit ();
void force_futurehead_test_network ();
}

int main (int argc, char ** argv)
{
	futurehead::force_futurehead_test_network ();
	futurehead::set_use_memory_pools (false);
	futurehead::node_singleton_memory_pool_purge_guard cleanup_guard;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	futurehead::cleanup_test_directories_on_exit ();
	return res;
}
