#include <futurehead/crypto_lib/random_pool.hpp>
#include <futurehead/lib/config.hpp>
#include <futurehead/lib/threading.hpp>
#include <futurehead/node/common.hpp>
#include <futurehead/node/testing.hpp>
#include <futurehead/qt/qt.hpp>

#include <boost/format.hpp>

#include <thread>

int main (int argc, char ** argv)
{
	futurehead::network_constants::set_active_network (futurehead::futurehead_networks::futurehead_test_network);
	futurehead::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	QApplication application (argc, argv);
	QCoreApplication::setOrganizationName ("Futurehead");
	QCoreApplication::setOrganizationDomain ("futurehead.org");
	QCoreApplication::setApplicationName ("Futurehead Wallet");
	futurehead_qt::eventloop_processor processor;
	const uint16_t count (16);
	futurehead::system system (count);
	futurehead::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
	std::unique_ptr<QTabWidget> client_tabs (new QTabWidget);
	std::vector<std::unique_ptr<futurehead_qt::wallet>> guis;
	for (auto i (0); i < count; ++i)
	{
		auto wallet (system.nodes[i]->wallets.create (futurehead::random_wallet_id ()));
		futurehead::keypair key;
		wallet->insert_adhoc (key.prv);
		guis.push_back (std::make_unique<futurehead_qt::wallet> (application, processor, *system.nodes[i], wallet, key.pub));
		client_tabs->addTab (guis.back ()->client_window, boost::str (boost::format ("Wallet %1%") % i).c_str ());
	}
	client_tabs->show ();
	QObject::connect (&application, &QApplication::aboutToQuit, [&]() {
		system.stop ();
	});
	int result;
	try
	{
		result = application.exec ();
	}
	catch (...)
	{
		result = -1;
		debug_assert (false);
	}
	runner.join ();
	return result;
}
