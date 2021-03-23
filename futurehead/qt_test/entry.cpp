#include <futurehead/node/common.hpp>

#include <gtest/gtest.h>

#include <QApplication>
QApplication * test_application = nullptr;
namespace futurehead
{
void cleanup_test_directories_on_exit ();
void force_futurehead_test_network ();
}

int main (int argc, char ** argv)
{
	futurehead::force_futurehead_test_network ();
	futurehead::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	QApplication application (argc, argv);
	test_application = &application;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	futurehead::cleanup_test_directories_on_exit ();
	return res;
}
