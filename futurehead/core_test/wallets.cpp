#include <futurehead/core_test/testutil.hpp>
#include <futurehead/node/node.hpp>
#include <futurehead/node/testing.hpp>
#include <futurehead/secure/versioning.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (wallets, open_create)
{
	futurehead::system system (1);
	bool error (false);
	futurehead::wallets wallets (error, *system.nodes[0]);
	ASSERT_FALSE (error);
	ASSERT_EQ (1, wallets.items.size ()); // it starts out with a default wallet
	auto id = futurehead::random_wallet_id ();
	ASSERT_EQ (nullptr, wallets.open (id));
	auto wallet (wallets.create (id));
	ASSERT_NE (nullptr, wallet);
	ASSERT_EQ (wallet, wallets.open (id));
}

TEST (wallets, open_existing)
{
	futurehead::system system (1);
	auto id (futurehead::random_wallet_id ());
	{
		bool error (false);
		futurehead::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
		auto wallet (wallets.create (id));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (wallet, wallets.open (id));
		futurehead::raw_key password;
		password.data.clear ();
		system.deadline_set (10s);
		while (password.data == 0)
		{
			ASSERT_NO_ERROR (system.poll ());
			wallet->store.password.value (password);
		}
	}
	{
		bool error (false);
		futurehead::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (2, wallets.items.size ());
		ASSERT_NE (nullptr, wallets.open (id));
	}
}

TEST (wallets, remove)
{
	futurehead::system system (1);
	futurehead::wallet_id one (1);
	{
		bool error (false);
		futurehead::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
		auto wallet (wallets.create (one));
		ASSERT_NE (nullptr, wallet);
		ASSERT_EQ (2, wallets.items.size ());
		wallets.destroy (one);
		ASSERT_EQ (1, wallets.items.size ());
	}
	{
		bool error (false);
		futurehead::wallets wallets (error, *system.nodes[0]);
		ASSERT_FALSE (error);
		ASSERT_EQ (1, wallets.items.size ());
	}
}

TEST (wallets, upgrade)
{
	// Don't test this in rocksdb mode
	auto use_rocksdb_str = std::getenv ("TEST_USE_ROCKSDB");
	if (use_rocksdb_str && boost::lexical_cast<int> (use_rocksdb_str) == 1)
	{
		return;
	}

	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	system.add_node (node_config);
	auto path (futurehead::unique_path ());
	auto id = futurehead::random_wallet_id ();
	futurehead::node_config node_config1 (futurehead::get_available_port (), system.logging);
	node_config1.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	{
		auto node1 (std::make_shared<futurehead::node> (system.io_ctx, path, system.alarm, node_config1, system.work));
		ASSERT_FALSE (node1->init_error ());
		bool error (false);
		futurehead::wallets wallets (error, *node1);
		wallets.create (id);
		auto transaction_source (node1->wallets.env.tx_begin_write ());
		auto tx_source = static_cast<MDB_txn *> (transaction_source.get_handle ());
		auto & mdb_store (dynamic_cast<futurehead::mdb_store &> (node1->store));
		auto transaction_destination (mdb_store.tx_begin_write ());
		auto tx_destination = static_cast<MDB_txn *> (transaction_destination.get_handle ());
		wallets.move_table (id.to_string (), tx_source, tx_destination);
		node1->store.version_put (transaction_destination, 11);

		futurehead::account_info info;
		ASSERT_FALSE (mdb_store.account_get (transaction_destination, futurehead::genesis_account, info));
		auto rep_block = node1->rep_block (futurehead::genesis_account);
		futurehead::account_info_v13 account_info_v13 (info.head, rep_block, info.open_block, info.balance, info.modified, info.block_count, info.epoch ());
		auto status (mdb_put (mdb_store.env.tx (transaction_destination), info.epoch () == futurehead::epoch::epoch_0 ? mdb_store.accounts_v0 : mdb_store.accounts_v1, futurehead::mdb_val (futurehead::test_genesis_key.pub), futurehead::mdb_val (account_info_v13), 0));
		ASSERT_EQ (status, 0);
		mdb_store.confirmation_height_del (transaction_destination, futurehead::genesis_account);
	}
	auto node1 (std::make_shared<futurehead::node> (system.io_ctx, path, system.alarm, node_config1, system.work));
	ASSERT_EQ (1, node1->wallets.items.size ());
	ASSERT_EQ (id, node1->wallets.items.begin ()->first);
	auto transaction_new (node1->wallets.env.tx_begin_write ());
	auto tx_new = static_cast<MDB_txn *> (transaction_new.get_handle ());
	auto transaction_old (node1->store.tx_begin_write ());
	auto tx_old = static_cast<MDB_txn *> (transaction_old.get_handle ());
	MDB_dbi old_handle;
	ASSERT_EQ (MDB_NOTFOUND, mdb_dbi_open (tx_old, id.to_string ().c_str (), 0, &old_handle));
	MDB_dbi new_handle;
	ASSERT_EQ (0, mdb_dbi_open (tx_new, id.to_string ().c_str (), 0, &new_handle));
}

// Keeps breaking whenever we add new DBs
TEST (wallets, DISABLED_wallet_create_max)
{
	futurehead::system system (1);
	bool error (false);
	futurehead::wallets wallets (error, *system.nodes[0]);
	const int nonWalletDbs = 19;
	for (int i = 0; i < system.nodes[0]->config.deprecated_lmdb_max_dbs - nonWalletDbs; i++)
	{
		auto wallet_id = futurehead::random_wallet_id ();
		auto wallet = wallets.create (wallet_id);
		auto existing = wallets.items.find (wallet_id);
		ASSERT_TRUE (existing != wallets.items.end ());
		futurehead::raw_key seed;
		seed.data = 0;
		auto transaction (system.nodes[0]->store.tx_begin_write ());
		existing->second->store.seed_set (transaction, seed);
	}
	auto wallet_id = futurehead::random_wallet_id ();
	wallets.create (wallet_id);
	auto existing = wallets.items.find (wallet_id);
	ASSERT_TRUE (existing == wallets.items.end ());
}

TEST (wallets, reload)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::wallet_id one (1);
	bool error (false);
	ASSERT_FALSE (error);
	ASSERT_EQ (1, node1.wallets.items.size ());
	{
		futurehead::lock_guard<std::mutex> lock_wallet (node1.wallets.mutex);
		futurehead::inactive_node node (node1.application_path);
		auto wallet (node.node->wallets.create (one));
		ASSERT_NE (wallet, nullptr);
	}
	system.deadline_set (5s);
	while (node1.wallets.open (one) == nullptr)
	{
		system.poll ();
	}
	ASSERT_EQ (2, node1.wallets.items.size ());
}

TEST (wallets, vote_minimum)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::keypair key1;
	futurehead::keypair key2;
	futurehead::genesis genesis;
	futurehead::state_block send1 (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, std::numeric_limits<futurehead::uint128_t>::max () - node1.config.vote_minimum.number (), key1.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, node1.process (send1).code);
	futurehead::state_block open1 (key1.pub, 0, key1.pub, node1.config.vote_minimum.number (), send1.hash (), key1.prv, key1.pub, *system.work.generate (key1.pub));
	ASSERT_EQ (futurehead::process_result::progress, node1.process (open1).code);
	// send2 with amount vote_minimum - 1 (not voting representative)
	futurehead::state_block send2 (futurehead::test_genesis_key.pub, send1.hash (), futurehead::test_genesis_key.pub, std::numeric_limits<futurehead::uint128_t>::max () - 2 * node1.config.vote_minimum.number () + 1, key2.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, node1.process (send2).code);
	futurehead::state_block open2 (key2.pub, 0, key2.pub, node1.config.vote_minimum.number () - 1, send2.hash (), key2.prv, key2.pub, *system.work.generate (key2.pub));
	ASSERT_EQ (futurehead::process_result::progress, node1.process (open2).code);
	auto wallet (node1.wallets.items.begin ()->second);
	ASSERT_EQ (0, wallet->representatives.size ());
	wallet->insert_adhoc (futurehead::test_genesis_key.prv);
	wallet->insert_adhoc (key1.prv);
	wallet->insert_adhoc (key2.prv);
	node1.wallets.compute_reps ();
	ASSERT_EQ (2, wallet->representatives.size ());
}

TEST (wallets, exists)
{
	futurehead::system system (1);
	auto & node (*system.nodes[0]);
	futurehead::keypair key1;
	futurehead::keypair key2;
	{
		auto transaction (node.wallets.tx_begin_read ());
		ASSERT_FALSE (node.wallets.exists (transaction, key1.pub));
		ASSERT_FALSE (node.wallets.exists (transaction, key2.pub));
	}
	system.wallet (0)->insert_adhoc (key1.prv);
	{
		auto transaction (node.wallets.tx_begin_read ());
		ASSERT_TRUE (node.wallets.exists (transaction, key1.pub));
		ASSERT_FALSE (node.wallets.exists (transaction, key2.pub));
	}
	system.wallet (0)->insert_adhoc (key2.prv);
	{
		auto transaction (node.wallets.tx_begin_read ());
		ASSERT_TRUE (node.wallets.exists (transaction, key1.pub));
		ASSERT_TRUE (node.wallets.exists (transaction, key2.pub));
	}
}
