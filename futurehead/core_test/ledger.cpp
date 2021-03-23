#include <futurehead/core_test/testutil.hpp>
#include <futurehead/lib/stats.hpp>
#include <futurehead/lib/threading.hpp>
#include <futurehead/node/election.hpp>
#include <futurehead/node/testing.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

// Init returns an error if it can't open files at the path
TEST (ledger, store_error)
{
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, boost::filesystem::path ("///"));
	ASSERT_TRUE (store.init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (store, stats);
}

// Ledger can be initialized and returns a basic query for an empty account
TEST (ledger, empty)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::account account;
	auto transaction (store->tx_begin_read ());
	auto balance (ledger.account_balance (transaction, account));
	ASSERT_TRUE (balance.is_zero ());
}

// Genesis account should have the max balance on empty initialization
TEST (ledger, genesis_balance)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	auto balance (ledger.account_balance (transaction, futurehead::genesis_account));
	ASSERT_EQ (futurehead::genesis_amount, balance);
	auto amount (ledger.amount (transaction, futurehead::genesis_account));
	ASSERT_EQ (futurehead::genesis_amount, amount);
	futurehead::account_info info;
	ASSERT_FALSE (store->account_get (transaction, futurehead::genesis_account, info));
	ASSERT_EQ (1, ledger.cache.account_count);
	// Frontier time should have been updated when genesis balance was added
	ASSERT_GE (futurehead::seconds_since_epoch (), info.modified);
	ASSERT_LT (futurehead::seconds_since_epoch () - info.modified, 10);
	// Genesis block should be confirmed by default
	futurehead::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height_get (transaction, futurehead::genesis_account, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 1);
	ASSERT_EQ (confirmation_height_info.frontier, genesis.hash ());
}

// All nodes in the system should agree on the genesis balance
TEST (system, system_genesis)
{
	futurehead::system system (2);
	for (auto & i : system.nodes)
	{
		auto transaction (i->store.tx_begin_read ());
		ASSERT_EQ (futurehead::genesis_amount, i->ledger.account_balance (transaction, futurehead::genesis_account));
	}
}

TEST (ledger, process_modifies_sideband)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	store->initialize (store->tx_begin_write (), genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (store->tx_begin_write (), send1).code);
	ASSERT_EQ (send1.sideband ().timestamp, store->block_get (store->tx_begin_read (), send1.hash ())->sideband ().timestamp);
}

// Create a send block and publish it.
TEST (ledger, process_send)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	auto transaction (store->tx_begin_write ());
	futurehead::genesis genesis;
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info1));
	futurehead::keypair key2;
	futurehead::send_block send (info1.head, key2.pub, 50, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (info1.head));
	futurehead::block_hash hash1 (send.hash ());
	ASSERT_EQ (futurehead::test_genesis_key.pub, store->frontier_get (transaction, info1.head));
	ASSERT_EQ (1, info1.block_count);
	// This was a valid block, it should progress.
	auto return1 (ledger.process (transaction, send));
	ASSERT_EQ (futurehead::test_genesis_key.pub, send.sideband ().account);
	ASSERT_EQ (2, send.sideband ().height);
	ASSERT_EQ (futurehead::genesis_amount - 50, ledger.amount (transaction, hash1));
	ASSERT_TRUE (store->frontier_get (transaction, info1.head).is_zero ());
	ASSERT_EQ (futurehead::test_genesis_key.pub, store->frontier_get (transaction, hash1));
	ASSERT_EQ (futurehead::process_result::progress, return1.code);
	ASSERT_EQ (futurehead::test_genesis_key.pub, return1.account);
	ASSERT_EQ (futurehead::genesis_amount - 50, return1.amount.number ());
	ASSERT_EQ (50, ledger.account_balance (transaction, futurehead::test_genesis_key.pub));
	ASSERT_EQ (futurehead::genesis_amount - 50, ledger.account_pending (transaction, key2.pub));
	futurehead::account_info info2;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info2));
	ASSERT_EQ (2, info2.block_count);
	auto latest6 (store->block_get (transaction, info2.head));
	ASSERT_NE (nullptr, latest6);
	auto latest7 (dynamic_cast<futurehead::send_block *> (latest6.get ()));
	ASSERT_NE (nullptr, latest7);
	ASSERT_EQ (send, *latest7);
	// Create an open block opening an account accepting the send we just created
	futurehead::open_block open (hash1, key2.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	futurehead::block_hash hash2 (open.hash ());
	// This was a valid block, it should progress.
	auto return2 (ledger.process (transaction, open));
	ASSERT_EQ (futurehead::process_result::progress, return2.code);
	ASSERT_EQ (key2.pub, open.sideband ().account);
	ASSERT_EQ (futurehead::genesis_amount - 50, open.sideband ().balance.number ());
	ASSERT_EQ (1, open.sideband ().height);
	ASSERT_EQ (futurehead::genesis_amount - 50, ledger.amount (transaction, hash2));
	ASSERT_EQ (futurehead::process_result::progress, return2.code);
	ASSERT_EQ (key2.pub, return2.account);
	ASSERT_EQ (futurehead::genesis_amount - 50, return2.amount.number ());
	ASSERT_EQ (key2.pub, store->frontier_get (transaction, hash2));
	ASSERT_EQ (futurehead::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (50, ledger.weight (futurehead::test_genesis_key.pub));
	ASSERT_EQ (futurehead::genesis_amount - 50, ledger.weight (key2.pub));
	futurehead::account_info info3;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info3));
	auto latest2 (store->block_get (transaction, info3.head));
	ASSERT_NE (nullptr, latest2);
	auto latest3 (dynamic_cast<futurehead::send_block *> (latest2.get ()));
	ASSERT_NE (nullptr, latest3);
	ASSERT_EQ (send, *latest3);
	futurehead::account_info info4;
	ASSERT_FALSE (store->account_get (transaction, key2.pub, info4));
	auto latest4 (store->block_get (transaction, info4.head));
	ASSERT_NE (nullptr, latest4);
	auto latest5 (dynamic_cast<futurehead::open_block *> (latest4.get ()));
	ASSERT_NE (nullptr, latest5);
	ASSERT_EQ (open, *latest5);
	ASSERT_FALSE (ledger.rollback (transaction, hash2));
	ASSERT_TRUE (store->frontier_get (transaction, hash2).is_zero ());
	futurehead::account_info info5;
	ASSERT_TRUE (ledger.store.account_get (transaction, key2.pub, info5));
	futurehead::pending_info pending1;
	ASSERT_FALSE (ledger.store.pending_get (transaction, futurehead::pending_key (key2.pub, hash1), pending1));
	ASSERT_EQ (futurehead::test_genesis_key.pub, pending1.source);
	ASSERT_EQ (futurehead::genesis_amount - 50, pending1.amount.number ());
	ASSERT_EQ (0, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (futurehead::genesis_amount - 50, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (50, ledger.account_balance (transaction, futurehead::test_genesis_key.pub));
	ASSERT_EQ (50, ledger.weight (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	futurehead::account_info info6;
	ASSERT_FALSE (ledger.store.account_get (transaction, futurehead::test_genesis_key.pub, info6));
	ASSERT_EQ (hash1, info6.head);
	ASSERT_FALSE (ledger.rollback (transaction, info6.head));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::test_genesis_key.pub));
	ASSERT_EQ (futurehead::test_genesis_key.pub, store->frontier_get (transaction, info1.head));
	ASSERT_TRUE (store->frontier_get (transaction, hash1).is_zero ());
	futurehead::account_info info7;
	ASSERT_FALSE (ledger.store.account_get (transaction, futurehead::test_genesis_key.pub, info7));
	ASSERT_EQ (1, info7.block_count);
	ASSERT_EQ (info1.head, info7.head);
	futurehead::pending_info pending2;
	ASSERT_TRUE (ledger.store.pending_get (transaction, futurehead::pending_key (key2.pub, hash1), pending2));
	ASSERT_EQ (futurehead::genesis_amount, ledger.account_balance (transaction, futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, process_receive)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info1));
	futurehead::keypair key2;
	futurehead::send_block send (info1.head, key2.pub, 50, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (info1.head));
	futurehead::block_hash hash1 (send.hash ());
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send).code);
	futurehead::keypair key3;
	futurehead::open_block open (hash1, key3.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	futurehead::block_hash hash2 (open.hash ());
	auto return1 (ledger.process (transaction, open));
	ASSERT_EQ (futurehead::process_result::progress, return1.code);
	ASSERT_EQ (key2.pub, return1.account);
	ASSERT_EQ (key2.pub, open.sideband ().account);
	ASSERT_EQ (futurehead::genesis_amount - 50, open.sideband ().balance.number ());
	ASSERT_EQ (1, open.sideband ().height);
	ASSERT_EQ (futurehead::genesis_amount - 50, return1.amount.number ());
	ASSERT_EQ (futurehead::genesis_amount - 50, ledger.weight (key3.pub));
	futurehead::send_block send2 (hash1, key2.pub, 25, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (hash1));
	futurehead::block_hash hash3 (send2.hash ());
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send2).code);
	futurehead::receive_block receive (hash2, hash3, key2.prv, key2.pub, *pool.generate (hash2));
	auto hash4 (receive.hash ());
	ASSERT_EQ (key2.pub, store->frontier_get (transaction, hash2));
	auto return2 (ledger.process (transaction, receive));
	ASSERT_EQ (key2.pub, receive.sideband ().account);
	ASSERT_EQ (futurehead::genesis_amount - 25, receive.sideband ().balance.number ());
	ASSERT_EQ (2, receive.sideband ().height);
	ASSERT_EQ (25, ledger.amount (transaction, hash4));
	ASSERT_TRUE (store->frontier_get (transaction, hash2).is_zero ());
	ASSERT_EQ (key2.pub, store->frontier_get (transaction, hash4));
	ASSERT_EQ (futurehead::process_result::progress, return2.code);
	ASSERT_EQ (key2.pub, return2.account);
	ASSERT_EQ (25, return2.amount.number ());
	ASSERT_EQ (hash4, ledger.latest (transaction, key2.pub));
	ASSERT_EQ (25, ledger.account_balance (transaction, futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (futurehead::genesis_amount - 25, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (futurehead::genesis_amount - 25, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, hash4));
	ASSERT_TRUE (store->block_successor (transaction, hash2).is_zero ());
	ASSERT_EQ (key2.pub, store->frontier_get (transaction, hash2));
	ASSERT_TRUE (store->frontier_get (transaction, hash4).is_zero ());
	ASSERT_EQ (25, ledger.account_balance (transaction, futurehead::test_genesis_key.pub));
	ASSERT_EQ (25, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (futurehead::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (futurehead::genesis_amount - 50, ledger.weight (key3.pub));
	ASSERT_EQ (hash2, ledger.latest (transaction, key2.pub));
	futurehead::pending_info pending1;
	ASSERT_FALSE (ledger.store.pending_get (transaction, futurehead::pending_key (key2.pub, hash3), pending1));
	ASSERT_EQ (futurehead::test_genesis_key.pub, pending1.source);
	ASSERT_EQ (25, pending1.amount.number ());
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, rollback_receiver)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info1));
	futurehead::keypair key2;
	futurehead::send_block send (info1.head, key2.pub, 50, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (info1.head));
	futurehead::block_hash hash1 (send.hash ());
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send).code);
	futurehead::keypair key3;
	futurehead::open_block open (hash1, key3.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	futurehead::block_hash hash2 (open.hash ());
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (hash2, ledger.latest (transaction, key2.pub));
	ASSERT_EQ (50, ledger.account_balance (transaction, futurehead::test_genesis_key.pub));
	ASSERT_EQ (futurehead::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (50, ledger.weight (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (futurehead::genesis_amount - 50, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, hash1));
	ASSERT_EQ (futurehead::genesis_amount, ledger.account_balance (transaction, futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	futurehead::account_info info2;
	ASSERT_TRUE (ledger.store.account_get (transaction, key2.pub, info2));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
	futurehead::pending_info pending1;
	ASSERT_TRUE (ledger.store.pending_get (transaction, futurehead::pending_key (key2.pub, info2.head), pending1));
}

TEST (ledger, rollback_representation)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key5;
	futurehead::change_block change1 (genesis.hash (), key5.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, change1).code);
	futurehead::keypair key3;
	futurehead::change_block change2 (change1.hash (), key3.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (change1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, change2).code);
	futurehead::keypair key2;
	futurehead::send_block send1 (change2.hash (), key2.pub, 50, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (change2.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::keypair key4;
	futurehead::open_block open (send1.hash (), key4.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open).code);
	futurehead::send_block send2 (send1.hash (), key2.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send2).code);
	futurehead::receive_block receive1 (open.hash (), send2.hash (), key2.prv, key2.pub, *pool.generate (open.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_EQ (1, ledger.weight (key3.pub));
	ASSERT_EQ (futurehead::genesis_amount - 1, ledger.weight (key4.pub));
	futurehead::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, key2.pub, info1));
	ASSERT_EQ (key4.pub, info1.representative);
	ASSERT_FALSE (ledger.rollback (transaction, receive1.hash ()));
	futurehead::account_info info2;
	ASSERT_FALSE (store->account_get (transaction, key2.pub, info2));
	ASSERT_EQ (key4.pub, info2.representative);
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (futurehead::genesis_amount - 50, ledger.weight (key4.pub));
	ASSERT_FALSE (ledger.rollback (transaction, open.hash ()));
	ASSERT_EQ (1, ledger.weight (key3.pub));
	ASSERT_EQ (0, ledger.weight (key4.pub));
	ledger.rollback (transaction, send1.hash ());
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (key3.pub));
	futurehead::account_info info3;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info3));
	ASSERT_EQ (key3.pub, info3.representative);
	ASSERT_FALSE (ledger.rollback (transaction, change2.hash ()));
	futurehead::account_info info4;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info4));
	ASSERT_EQ (key5.pub, info4.representative);
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (key5.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
}

TEST (ledger, receive_rollback)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::send_block send (genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send).code);
	futurehead::receive_block receive (send.hash (), send.hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive).code);
	ASSERT_FALSE (ledger.rollback (transaction, receive.hash ()));
}

TEST (ledger, process_duplicate)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info1));
	futurehead::keypair key2;
	futurehead::send_block send (info1.head, key2.pub, 50, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (info1.head));
	futurehead::block_hash hash1 (send.hash ());
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (futurehead::process_result::old, ledger.process (transaction, send).code);
	futurehead::open_block open (hash1, 1, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (futurehead::process_result::old, ledger.process (transaction, open).code);
}

TEST (ledger, representative_genesis)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	auto latest (ledger.latest (transaction, futurehead::test_genesis_key.pub));
	ASSERT_FALSE (latest.is_zero ());
	ASSERT_EQ (genesis.open->hash (), ledger.representative (transaction, latest));
}

TEST (ledger, weight)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::genesis_account));
}

TEST (ledger, representative_change)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::keypair key2;
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	futurehead::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info1));
	futurehead::change_block block (info1.head, key2.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (info1.head));
	ASSERT_EQ (futurehead::test_genesis_key.pub, store->frontier_get (transaction, info1.head));
	auto return1 (ledger.process (transaction, block));
	ASSERT_EQ (0, ledger.amount (transaction, block.hash ()));
	ASSERT_TRUE (store->frontier_get (transaction, info1.head).is_zero ());
	ASSERT_EQ (futurehead::test_genesis_key.pub, store->frontier_get (transaction, block.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, return1.code);
	ASSERT_EQ (futurehead::test_genesis_key.pub, return1.account);
	ASSERT_EQ (0, ledger.weight (futurehead::test_genesis_key.pub));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (key2.pub));
	futurehead::account_info info2;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info2));
	ASSERT_EQ (block.hash (), info2.head);
	ASSERT_FALSE (ledger.rollback (transaction, info2.head));
	ASSERT_EQ (futurehead::test_genesis_key.pub, store->frontier_get (transaction, info1.head));
	ASSERT_TRUE (store->frontier_get (transaction, block.hash ()).is_zero ());
	futurehead::account_info info3;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info3));
	ASSERT_EQ (info1.head, info3.head);
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
}

TEST (ledger, send_fork)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::keypair key2;
	futurehead::keypair key3;
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info1));
	futurehead::send_block block (info1.head, key2.pub, 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (info1.head));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block).code);
	futurehead::send_block block2 (info1.head, key3.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (info1.head));
	ASSERT_EQ (futurehead::process_result::fork, ledger.process (transaction, block2).code);
}

TEST (ledger, receive_fork)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::keypair key2;
	futurehead::keypair key3;
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info1));
	futurehead::send_block block (info1.head, key2.pub, 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (info1.head));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block).code);
	futurehead::open_block block2 (block.hash (), key2.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block2).code);
	futurehead::change_block block3 (block2.hash (), key3.pub, key2.prv, key2.pub, *pool.generate (block2.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block3).code);
	futurehead::send_block block4 (block.hash (), key2.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block4).code);
	futurehead::receive_block block5 (block2.hash (), block4.hash (), key2.prv, key2.pub, *pool.generate (block2.hash ()));
	ASSERT_EQ (futurehead::process_result::fork, ledger.process (transaction, block5).code);
}

TEST (ledger, open_fork)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::keypair key2;
	futurehead::keypair key3;
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info1));
	futurehead::send_block block (info1.head, key2.pub, 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (info1.head));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block).code);
	futurehead::open_block block2 (block.hash (), key2.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block2).code);
	futurehead::open_block block3 (block.hash (), key3.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (futurehead::process_result::fork, ledger.process (transaction, block3).code);
}

TEST (system, DISABLED_generate_send_existing)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::thread_runner runner (system.io_ctx, node1.config.io_threads);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (futurehead::genesis_account, stake_preserver.pub, futurehead::genesis_amount / 3 * 2, true));
	futurehead::account_info info1;
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_FALSE (node1.store.account_get (transaction, futurehead::test_genesis_key.pub, info1));
	}
	std::vector<futurehead::account> accounts;
	accounts.push_back (futurehead::test_genesis_key.pub);
	system.generate_send_existing (node1, accounts);
	// Have stake_preserver receive funds after generate_send_existing so it isn't chosen as the destination
	{
		auto transaction (node1.store.tx_begin_write ());
		auto open_block (std::make_shared<futurehead::open_block> (send_block->hash (), futurehead::genesis_account, stake_preserver.pub, stake_preserver.prv, stake_preserver.pub, 0));
		node1.work_generate_blocking (*open_block);
		ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, *open_block).code);
	}
	ASSERT_GT (node1.balance (stake_preserver.pub), node1.balance (futurehead::genesis_account));
	futurehead::account_info info2;
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_FALSE (node1.store.account_get (transaction, futurehead::test_genesis_key.pub, info2));
	}
	ASSERT_NE (info1.head, info2.head);
	system.deadline_set (15s);
	while (info2.block_count < info1.block_count + 2)
	{
		ASSERT_NO_ERROR (system.poll ());
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_FALSE (node1.store.account_get (transaction, futurehead::test_genesis_key.pub, info2));
	}
	ASSERT_EQ (info1.block_count + 2, info2.block_count);
	ASSERT_EQ (info2.balance, futurehead::genesis_amount / 3);
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_NE (node1.ledger.amount (transaction, info2.head), 0);
	}
	system.stop ();
	runner.join ();
}

TEST (system, generate_send_new)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::thread_runner runner (system.io_ctx, node1.config.io_threads);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	{
		auto transaction (node1.store.tx_begin_read ());
		auto iterator1 (node1.store.latest_begin (transaction));
		ASSERT_NE (node1.store.latest_end (), iterator1);
		++iterator1;
		ASSERT_EQ (node1.store.latest_end (), iterator1);
	}
	futurehead::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (futurehead::genesis_account, stake_preserver.pub, futurehead::genesis_amount / 3 * 2, true));
	{
		auto transaction (node1.store.tx_begin_write ());
		auto open_block (std::make_shared<futurehead::open_block> (send_block->hash (), futurehead::genesis_account, stake_preserver.pub, stake_preserver.prv, stake_preserver.pub, 0));
		node1.work_generate_blocking (*open_block);
		ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, *open_block).code);
	}
	ASSERT_GT (node1.balance (stake_preserver.pub), node1.balance (futurehead::genesis_account));
	std::vector<futurehead::account> accounts;
	accounts.push_back (futurehead::test_genesis_key.pub);
	// This indirectly waits for online weight to stabilize, required to prevent intermittent failures
	ASSERT_TIMELY (5s, node1.wallets.reps ().voting > 0);
	system.generate_send_new (node1, accounts);
	futurehead::account new_account (0);
	{
		auto transaction (node1.wallets.tx_begin_read ());
		auto iterator2 (system.wallet (0)->store.begin (transaction));
		if (iterator2->first != futurehead::test_genesis_key.pub)
		{
			new_account = iterator2->first;
		}
		++iterator2;
		ASSERT_NE (system.wallet (0)->store.end (), iterator2);
		if (iterator2->first != futurehead::test_genesis_key.pub)
		{
			new_account = iterator2->first;
		}
		++iterator2;
		ASSERT_EQ (system.wallet (0)->store.end (), iterator2);
		ASSERT_FALSE (new_account.is_zero ());
	}
	system.deadline_set (10s);
	while (node1.balance (new_account) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.stop ();
	runner.join ();
}

TEST (ledger, representation_changes)
{
	futurehead::keypair key1;
	futurehead::rep_weights rep_weights;
	ASSERT_EQ (0, rep_weights.representation_get (key1.pub));
	rep_weights.representation_put (key1.pub, 1);
	ASSERT_EQ (1, rep_weights.representation_get (key1.pub));
	rep_weights.representation_put (key1.pub, 2);
	ASSERT_EQ (2, rep_weights.representation_get (key1.pub));
}

TEST (ledger, representation)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	auto & rep_weights = ledger.cache.rep_weights;
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	ASSERT_EQ (futurehead::genesis_amount, rep_weights.representation_get (futurehead::test_genesis_key.pub));
	futurehead::keypair key2;
	futurehead::send_block block1 (genesis.hash (), key2.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block1).code);
	ASSERT_EQ (futurehead::genesis_amount - 100, rep_weights.representation_get (futurehead::test_genesis_key.pub));
	futurehead::keypair key3;
	futurehead::open_block block2 (block1.hash (), key3.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (futurehead::genesis_amount - 100, rep_weights.representation_get (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key3.pub));
	futurehead::send_block block3 (block1.hash (), key2.pub, futurehead::genesis_amount - 200, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block3).code);
	ASSERT_EQ (futurehead::genesis_amount - 200, rep_weights.representation_get (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key3.pub));
	futurehead::receive_block block4 (block2.hash (), block3.hash (), key2.prv, key2.pub, *pool.generate (block2.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (futurehead::genesis_amount - 200, rep_weights.representation_get (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (200, rep_weights.representation_get (key3.pub));
	futurehead::keypair key4;
	futurehead::change_block block5 (block4.hash (), key4.pub, key2.prv, key2.pub, *pool.generate (block4.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block5).code);
	ASSERT_EQ (futurehead::genesis_amount - 200, rep_weights.representation_get (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (200, rep_weights.representation_get (key4.pub));
	futurehead::keypair key5;
	futurehead::send_block block6 (block5.hash (), key5.pub, 100, key2.prv, key2.pub, *pool.generate (block5.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block6).code);
	ASSERT_EQ (futurehead::genesis_amount - 200, rep_weights.representation_get (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key4.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key5.pub));
	futurehead::keypair key6;
	futurehead::open_block block7 (block6.hash (), key6.pub, key5.pub, key5.prv, key5.pub, *pool.generate (key5.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block7).code);
	ASSERT_EQ (futurehead::genesis_amount - 200, rep_weights.representation_get (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key4.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key5.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key6.pub));
	futurehead::send_block block8 (block6.hash (), key5.pub, 0, key2.prv, key2.pub, *pool.generate (block6.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block8).code);
	ASSERT_EQ (futurehead::genesis_amount - 200, rep_weights.representation_get (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key4.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key5.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key6.pub));
	futurehead::receive_block block9 (block7.hash (), block8.hash (), key5.prv, key5.pub, *pool.generate (block7.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block9).code);
	ASSERT_EQ (futurehead::genesis_amount - 200, rep_weights.representation_get (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key4.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key5.pub));
	ASSERT_EQ (200, rep_weights.representation_get (key6.pub));
}

TEST (ledger, double_open)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key2;
	futurehead::send_block send1 (genesis.hash (), key2.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::open_block open1 (send1.hash (), key2.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open1).code);
	futurehead::open_block open2 (send1.hash (), futurehead::test_genesis_key.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (futurehead::process_result::fork, ledger.process (transaction, open2).code);
}

TEST (ledger, double_receive)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key2;
	futurehead::send_block send1 (genesis.hash (), key2.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::open_block open1 (send1.hash (), key2.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open1).code);
	futurehead::receive_block receive1 (open1.hash (), send1.hash (), key2.prv, key2.pub, *pool.generate (open1.hash ()));
	ASSERT_EQ (futurehead::process_result::unreceivable, ledger.process (transaction, receive1).code);
}

TEST (votes, check_signature)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.online_weight_minimum = std::numeric_limits<futurehead::uint128_t>::max ();
	auto & node1 = *system.add_node (node_config);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	{
		auto transaction (node1.store.tx_begin_write ());
		ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, *send1).code);
	}
	auto election1 = node1.active.insert (send1);
	{
		futurehead::lock_guard<std::mutex> lock (node1.active.mutex);
		ASSERT_EQ (1, election1.election->last_votes.size ());
	}
	auto vote1 (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 1, send1));
	vote1->signature.bytes[0] ^= 1;
	ASSERT_EQ (futurehead::vote_code::invalid, node1.vote_processor.vote_blocking (vote1, std::make_shared<futurehead::transport::channel_udp> (node1.network.udp_channels, futurehead::endpoint (boost::asio::ip::address_v6 (), 0), node1.network_params.protocol.protocol_version)));
	vote1->signature.bytes[0] ^= 1;
	ASSERT_EQ (futurehead::vote_code::vote, node1.vote_processor.vote_blocking (vote1, std::make_shared<futurehead::transport::channel_udp> (node1.network.udp_channels, futurehead::endpoint (boost::asio::ip::address_v6 (), 0), node1.network_params.protocol.protocol_version)));
	ASSERT_EQ (futurehead::vote_code::replay, node1.vote_processor.vote_blocking (vote1, std::make_shared<futurehead::transport::channel_udp> (node1.network.udp_channels, futurehead::endpoint (boost::asio::ip::address_v6 (), 0), node1.network_params.protocol.protocol_version)));
}

TEST (votes, add_one)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, *send1).code);
	auto election1 = node1.active.insert (send1);
	futurehead::unique_lock<std::mutex> lock (node1.active.mutex);
	ASSERT_EQ (1, election1.election->last_votes.size ());
	lock.unlock ();
	auto vote1 (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 1, send1));
	ASSERT_EQ (futurehead::vote_code::vote, node1.active.vote (vote1));
	auto vote2 (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 2, send1));
	ASSERT_EQ (futurehead::vote_code::vote, node1.active.vote (vote2));
	lock.lock ();
	ASSERT_EQ (2, election1.election->last_votes.size ());
	auto existing1 (election1.election->last_votes.find (futurehead::test_genesis_key.pub));
	ASSERT_NE (election1.election->last_votes.end (), existing1);
	ASSERT_EQ (send1->hash (), existing1->second.hash);
	auto winner (*election1.election->tally ().begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (futurehead::genesis_amount - 100, winner.first);
}

TEST (votes, add_two)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, *send1).code);
	auto election1 = node1.active.insert (send1);
	futurehead::unique_lock<std::mutex> lock (node1.active.mutex);
	lock.unlock ();
	futurehead::keypair key2;
	auto send2 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	auto vote2 (std::make_shared<futurehead::vote> (key2.pub, key2.prv, 1, send2));
	ASSERT_EQ (futurehead::vote_code::vote, node1.active.vote (vote2));
	auto vote1 (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 1, send1));
	ASSERT_EQ (futurehead::vote_code::vote, node1.active.vote (vote1));
	lock.lock ();
	ASSERT_EQ (3, election1.election->last_votes.size ());
	ASSERT_NE (election1.election->last_votes.end (), election1.election->last_votes.find (futurehead::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), election1.election->last_votes[futurehead::test_genesis_key.pub].hash);
	ASSERT_NE (election1.election->last_votes.end (), election1.election->last_votes.find (key2.pub));
	ASSERT_EQ (send2->hash (), election1.election->last_votes[key2.pub].hash);
	auto winner (*election1.election->tally ().begin ());
	ASSERT_EQ (*send1, *winner.second);
}

// Higher sequence numbers change the vote
TEST (votes, add_existing)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.online_weight_minimum = std::numeric_limits<futurehead::uint128_t>::max ();
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	{
		auto transaction (node1.store.tx_begin_write ());
		ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, *send1).code);
	}
	auto election1 = node1.active.insert (send1);
	auto vote1 (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 1, send1));
	ASSERT_EQ (futurehead::vote_code::vote, node1.active.vote (vote1));
	// Block is already processed from vote
	ASSERT_TRUE (node1.active.publish (send1));
	futurehead::unique_lock<std::mutex> lock (node1.active.mutex);
	ASSERT_EQ (1, election1.election->last_votes[futurehead::test_genesis_key.pub].sequence);
	futurehead::keypair key2;
	auto send2 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto vote2 (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 2, send2));
	// Pretend we've waited the timeout
	election1.election->last_votes[futurehead::test_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	lock.unlock ();
	ASSERT_EQ (futurehead::vote_code::vote, node1.active.vote (vote2));
	ASSERT_FALSE (node1.active.publish (send2));
	lock.lock ();
	ASSERT_EQ (2, election1.election->last_votes[futurehead::test_genesis_key.pub].sequence);
	// Also resend the old vote, and see if we respect the sequence number
	election1.election->last_votes[futurehead::test_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	lock.unlock ();
	ASSERT_EQ (futurehead::vote_code::replay, node1.active.vote (vote1));
	lock.lock ();
	ASSERT_EQ (2, election1.election->last_votes[futurehead::test_genesis_key.pub].sequence);
	ASSERT_EQ (2, election1.election->last_votes.size ());
	ASSERT_NE (election1.election->last_votes.end (), election1.election->last_votes.find (futurehead::test_genesis_key.pub));
	ASSERT_EQ (send2->hash (), election1.election->last_votes[futurehead::test_genesis_key.pub].hash);
	{
		auto transaction (node1.store.tx_begin_read ());
		auto winner (*election1.election->tally ().begin ());
		ASSERT_EQ (*send2, *winner.second);
	}
}

// Lower sequence numbers are ignored
TEST (votes, add_old)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, *send1).code);
	auto election1 = node1.active.insert (send1);
	auto vote1 (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 2, send1));
	auto channel (std::make_shared<futurehead::transport::channel_udp> (node1.network.udp_channels, node1.network.endpoint (), node1.network_params.protocol.protocol_version));
	node1.vote_processor.vote_blocking (vote1, channel);
	futurehead::keypair key2;
	auto send2 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto vote2 (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 1, send2));
	{
		futurehead::lock_guard<std::mutex> lock (node1.active.mutex);
		election1.election->last_votes[futurehead::test_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	}
	node1.vote_processor.vote_blocking (vote2, channel);
	ASSERT_EQ (2, election1.election->last_votes_size ());
	futurehead::lock_guard<std::mutex> lock (node1.active.mutex);
	ASSERT_NE (election1.election->last_votes.end (), election1.election->last_votes.find (futurehead::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), election1.election->last_votes[futurehead::test_genesis_key.pub].hash);
	auto winner (*election1.election->tally ().begin ());
	ASSERT_EQ (*send1, *winner.second);
}

// Lower sequence numbers are accepted for different accounts
TEST (votes, add_old_different_account)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto send2 (std::make_shared<futurehead::send_block> (send1->hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	ASSERT_EQ (futurehead::process_result::progress, node1.process (*send1).code);
	ASSERT_EQ (futurehead::process_result::progress, node1.process (*send2).code);
	futurehead::blocks_confirm (node1, { send1, send2 });
	auto election1 = node1.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election1);
	auto election2 = node1.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election2);
	ASSERT_EQ (1, election1->last_votes_size ());
	ASSERT_EQ (1, election2->last_votes_size ());
	auto vote1 (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 2, send1));
	auto channel (std::make_shared<futurehead::transport::channel_udp> (node1.network.udp_channels, node1.network.endpoint (), node1.network_params.protocol.protocol_version));
	auto vote_result1 (node1.vote_processor.vote_blocking (vote1, channel));
	ASSERT_EQ (futurehead::vote_code::vote, vote_result1);
	ASSERT_EQ (2, election1->last_votes_size ());
	ASSERT_EQ (1, election2->last_votes_size ());
	auto vote2 (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 1, send2));
	auto vote_result2 (node1.vote_processor.vote_blocking (vote2, channel));
	ASSERT_EQ (futurehead::vote_code::vote, vote_result2);
	ASSERT_EQ (2, election1->last_votes_size ());
	ASSERT_EQ (2, election2->last_votes_size ());
	futurehead::unique_lock<std::mutex> lock (node1.active.mutex);
	ASSERT_NE (election1->last_votes.end (), election1->last_votes.find (futurehead::test_genesis_key.pub));
	ASSERT_NE (election2->last_votes.end (), election2->last_votes.find (futurehead::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), election1->last_votes[futurehead::test_genesis_key.pub].hash);
	ASSERT_EQ (send2->hash (), election2->last_votes[futurehead::test_genesis_key.pub].hash);
	auto winner1 (*election1->tally ().begin ());
	ASSERT_EQ (*send1, *winner1.second);
	auto winner2 (*election2->tally ().begin ());
	ASSERT_EQ (*send2, *winner2.second);
}

// The voting cooldown is respected
TEST (votes, add_cooldown)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, *send1).code);
	auto election1 = node1.active.insert (send1);
	auto vote1 (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 1, send1));
	auto channel (std::make_shared<futurehead::transport::channel_udp> (node1.network.udp_channels, node1.network.endpoint (), node1.network_params.protocol.protocol_version));
	node1.vote_processor.vote_blocking (vote1, channel);
	futurehead::keypair key2;
	auto send2 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto vote2 (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 2, send2));
	node1.vote_processor.vote_blocking (vote2, channel);
	futurehead::unique_lock<std::mutex> lock (node1.active.mutex);
	ASSERT_EQ (2, election1.election->last_votes.size ());
	ASSERT_NE (election1.election->last_votes.end (), election1.election->last_votes.find (futurehead::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), election1.election->last_votes[futurehead::test_genesis_key.pub].hash);
	auto winner (*election1.election->tally ().begin ());
	ASSERT_EQ (*send1, *winner.second);
}

// Query for block successor
TEST (ledger, successor)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::keypair key1;
	futurehead::genesis genesis;
	futurehead::send_block send1 (genesis.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0);
	node1.work_generate_blocking (send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, send1).code);
	ASSERT_EQ (send1, *node1.ledger.successor (transaction, futurehead::qualified_root (genesis.hash (), futurehead::root (0))));
	ASSERT_EQ (*genesis.open, *node1.ledger.successor (transaction, genesis.open->qualified_root ()));
	ASSERT_EQ (nullptr, node1.ledger.successor (transaction, futurehead::qualified_root (0)));
}

TEST (ledger, fail_change_old)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::change_block block (genesis.hash (), key1.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (futurehead::process_result::progress, result1.code);
	auto result2 (ledger.process (transaction, block));
	ASSERT_EQ (futurehead::process_result::old, result2.code);
}

TEST (ledger, fail_change_gap_previous)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::change_block block (1, key1.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (futurehead::root (1)));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (futurehead::process_result::gap_previous, result1.code);
}

TEST (ledger, fail_change_bad_signature)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::change_block block (genesis.hash (), key1.pub, futurehead::keypair ().prv, 0, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (futurehead::process_result::bad_signature, result1.code);
}

TEST (ledger, fail_change_fork)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::change_block block1 (genesis.hash (), key1.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (futurehead::process_result::progress, result1.code);
	futurehead::keypair key2;
	futurehead::change_block block2 (genesis.hash (), key2.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (futurehead::process_result::fork, result2.code);
}

TEST (ledger, fail_send_old)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block (genesis.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (futurehead::process_result::progress, result1.code);
	auto result2 (ledger.process (transaction, block));
	ASSERT_EQ (futurehead::process_result::old, result2.code);
}

TEST (ledger, fail_send_gap_previous)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block (1, key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (futurehead::root (1)));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (futurehead::process_result::gap_previous, result1.code);
}

TEST (ledger, fail_send_bad_signature)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block (genesis.hash (), key1.pub, 1, futurehead::keypair ().prv, 0, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (futurehead::process_result::bad_signature, result1.code);
}

TEST (ledger, fail_send_negative_spend)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block1 (genesis.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block1).code);
	futurehead::keypair key2;
	futurehead::send_block block2 (block1.hash (), key2.pub, 2, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block1.hash ()));
	ASSERT_EQ (futurehead::process_result::negative_spend, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_send_fork)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block1 (genesis.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block1).code);
	futurehead::keypair key2;
	futurehead::send_block block2 (genesis.hash (), key2.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::fork, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_old)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block1 (genesis.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block1).code);
	futurehead::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (futurehead::process_result::old, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_gap_source)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::open_block block2 (1, 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (futurehead::process_result::gap_source, result2.code);
}

TEST (ledger, fail_open_bad_signature)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block1 (genesis.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block1).code);
	futurehead::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	block2.signature.clear ();
	ASSERT_EQ (futurehead::process_result::bad_signature, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_fork_previous)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block1 (genesis.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block1).code);
	futurehead::send_block block2 (block1.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block2).code);
	futurehead::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block3).code);
	futurehead::open_block block4 (block2.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (futurehead::process_result::fork, ledger.process (transaction, block4).code);
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, fail_open_account_mismatch)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block1 (genesis.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block1).code);
	futurehead::keypair badkey;
	futurehead::open_block block2 (block1.hash (), 1, badkey.pub, badkey.prv, badkey.pub, *pool.generate (badkey.pub));
	ASSERT_NE (futurehead::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, fail_receive_old)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block1 (genesis.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block1).code);
	futurehead::send_block block2 (block1.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block2).code);
	futurehead::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block3).code);
	futurehead::receive_block block4 (block3.hash (), block2.hash (), key1.prv, key1.pub, *pool.generate (block3.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (futurehead::process_result::old, ledger.process (transaction, block4).code);
}

TEST (ledger, fail_receive_gap_source)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block1 (genesis.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (futurehead::process_result::progress, result1.code);
	futurehead::send_block block2 (block1.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (futurehead::process_result::progress, result2.code);
	futurehead::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (futurehead::process_result::progress, result3.code);
	futurehead::receive_block block4 (block3.hash (), 1, key1.prv, key1.pub, *pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (futurehead::process_result::gap_source, result4.code);
}

TEST (ledger, fail_receive_overreceive)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block1 (genesis.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (futurehead::process_result::progress, result1.code);
	futurehead::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block2));
	ASSERT_EQ (futurehead::process_result::progress, result3.code);
	futurehead::receive_block block3 (block2.hash (), block1.hash (), key1.prv, key1.pub, *pool.generate (block2.hash ()));
	auto result4 (ledger.process (transaction, block3));
	ASSERT_EQ (futurehead::process_result::unreceivable, result4.code);
}

TEST (ledger, fail_receive_bad_signature)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block1 (genesis.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (futurehead::process_result::progress, result1.code);
	futurehead::send_block block2 (block1.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (futurehead::process_result::progress, result2.code);
	futurehead::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (futurehead::process_result::progress, result3.code);
	futurehead::receive_block block4 (block3.hash (), block2.hash (), futurehead::keypair ().prv, 0, *pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (futurehead::process_result::bad_signature, result4.code);
}

TEST (ledger, fail_receive_gap_previous_opened)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block1 (genesis.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (futurehead::process_result::progress, result1.code);
	futurehead::send_block block2 (block1.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (futurehead::process_result::progress, result2.code);
	futurehead::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (futurehead::process_result::progress, result3.code);
	futurehead::receive_block block4 (1, block2.hash (), key1.prv, key1.pub, *pool.generate (futurehead::root (1)));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (futurehead::process_result::gap_previous, result4.code);
}

TEST (ledger, fail_receive_gap_previous_unopened)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block1 (genesis.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (futurehead::process_result::progress, result1.code);
	futurehead::send_block block2 (block1.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (futurehead::process_result::progress, result2.code);
	futurehead::receive_block block3 (1, block2.hash (), key1.prv, key1.pub, *pool.generate (futurehead::root (1)));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (futurehead::process_result::gap_previous, result3.code);
}

TEST (ledger, fail_receive_fork_previous)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block1 (genesis.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (futurehead::process_result::progress, result1.code);
	futurehead::send_block block2 (block1.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (futurehead::process_result::progress, result2.code);
	futurehead::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (futurehead::process_result::progress, result3.code);
	futurehead::keypair key2;
	futurehead::send_block block4 (block3.hash (), key1.pub, 1, key1.prv, key1.pub, *pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (futurehead::process_result::progress, result4.code);
	futurehead::receive_block block5 (block3.hash (), block2.hash (), key1.prv, key1.pub, *pool.generate (block3.hash ()));
	auto result5 (ledger.process (transaction, block5));
	ASSERT_EQ (futurehead::process_result::fork, result5.code);
}

TEST (ledger, fail_receive_received_source)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::send_block block1 (genesis.hash (), key1.pub, 2, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (futurehead::process_result::progress, result1.code);
	futurehead::send_block block2 (block1.hash (), key1.pub, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (futurehead::process_result::progress, result2.code);
	futurehead::send_block block6 (block2.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block2.hash ()));
	auto result6 (ledger.process (transaction, block6));
	ASSERT_EQ (futurehead::process_result::progress, result6.code);
	futurehead::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (futurehead::process_result::progress, result3.code);
	futurehead::keypair key2;
	futurehead::send_block block4 (block3.hash (), key1.pub, 1, key1.prv, key1.pub, *pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (futurehead::process_result::progress, result4.code);
	futurehead::receive_block block5 (block4.hash (), block2.hash (), key1.prv, key1.pub, *pool.generate (block4.hash ()));
	auto result5 (ledger.process (transaction, block5));
	ASSERT_EQ (futurehead::process_result::progress, result5.code);
	futurehead::receive_block block7 (block3.hash (), block2.hash (), key1.prv, key1.pub, *pool.generate (block3.hash ()));
	auto result7 (ledger.process (transaction, block7));
	ASSERT_EQ (futurehead::process_result::fork, result7.code);
}

TEST (ledger, latest_empty)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::keypair key;
	auto transaction (store->tx_begin_read ());
	auto latest (ledger.latest (transaction, key.pub));
	ASSERT_TRUE (latest.is_zero ());
}

TEST (ledger, latest_root)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key;
	ASSERT_EQ (key.pub, ledger.latest_root (transaction, key.pub));
	auto hash1 (ledger.latest (transaction, futurehead::test_genesis_key.pub));
	futurehead::send_block send (hash1, 0, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (hash1));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (send.hash (), ledger.latest_root (transaction, futurehead::test_genesis_key.pub));
}

TEST (ledger, change_representative_move_representation)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::keypair key1;
	auto transaction (store->tx_begin_write ());
	futurehead::genesis genesis;
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	auto hash1 (genesis.hash ());
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::test_genesis_key.pub));
	futurehead::send_block send (hash1, key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (hash1));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (0, ledger.weight (futurehead::test_genesis_key.pub));
	futurehead::keypair key2;
	futurehead::change_block change (send.hash (), key2.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, change).code);
	futurehead::keypair key3;
	futurehead::open_block open (send.hash (), key3.pub, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (key3.pub));
}

TEST (ledger, send_open_receive_rollback)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	auto transaction (store->tx_begin_write ());
	futurehead::genesis genesis;
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info1));
	futurehead::keypair key1;
	futurehead::send_block send1 (info1.head, key1.pub, futurehead::genesis_amount - 50, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (info1.head));
	auto return1 (ledger.process (transaction, send1));
	ASSERT_EQ (futurehead::process_result::progress, return1.code);
	futurehead::send_block send2 (send1.hash (), key1.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	auto return2 (ledger.process (transaction, send2));
	ASSERT_EQ (futurehead::process_result::progress, return2.code);
	futurehead::keypair key2;
	futurehead::open_block open (send2.hash (), key2.pub, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto return4 (ledger.process (transaction, open));
	ASSERT_EQ (futurehead::process_result::progress, return4.code);
	futurehead::receive_block receive (open.hash (), send1.hash (), key1.prv, key1.pub, *pool.generate (open.hash ()));
	auto return5 (ledger.process (transaction, receive));
	ASSERT_EQ (futurehead::process_result::progress, return5.code);
	futurehead::keypair key3;
	ASSERT_EQ (100, ledger.weight (key2.pub));
	ASSERT_EQ (futurehead::genesis_amount - 100, ledger.weight (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	futurehead::change_block change1 (send2.hash (), key3.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send2.hash ()));
	auto return6 (ledger.process (transaction, change1));
	ASSERT_EQ (futurehead::process_result::progress, return6.code);
	ASSERT_EQ (100, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (futurehead::test_genesis_key.pub));
	ASSERT_EQ (futurehead::genesis_amount - 100, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, receive.hash ()));
	ASSERT_EQ (50, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (futurehead::test_genesis_key.pub));
	ASSERT_EQ (futurehead::genesis_amount - 100, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, open.hash ()));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (futurehead::test_genesis_key.pub));
	ASSERT_EQ (futurehead::genesis_amount - 100, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, change1.hash ()));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	ASSERT_EQ (futurehead::genesis_amount - 100, ledger.weight (futurehead::test_genesis_key.pub));
	ASSERT_FALSE (ledger.rollback (transaction, send2.hash ()));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	ASSERT_EQ (futurehead::genesis_amount - 50, ledger.weight (futurehead::test_genesis_key.pub));
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	ASSERT_EQ (futurehead::genesis_amount - 0, ledger.weight (futurehead::test_genesis_key.pub));
}

TEST (ledger, bootstrap_rep_weight)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::account_info info1;
	futurehead::keypair key2;
	futurehead::genesis genesis;
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	{
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, genesis, ledger.cache);
		ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info1));
		futurehead::send_block send (info1.head, key2.pub, std::numeric_limits<futurehead::uint128_t>::max () - 50, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (info1.head));
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send).code);
	}
	ASSERT_EQ (2, ledger.cache.block_count);
	{
		ledger.bootstrap_weight_max_blocks = 3;
		ledger.bootstrap_weights[key2.pub] = 1000;
		ASSERT_EQ (1000, ledger.weight (key2.pub));
	}
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, info1));
		futurehead::send_block send (info1.head, key2.pub, std::numeric_limits<futurehead::uint128_t>::max () - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (info1.head));
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send).code);
	}
	ASSERT_EQ (3, ledger.cache.block_count);
	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (0, ledger.weight (key2.pub));
	}
}

TEST (ledger, block_destination_source)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair dest;
	futurehead::uint128_t balance (futurehead::genesis_amount);
	balance -= futurehead::Gxrb_ratio;
	futurehead::send_block block1 (genesis.hash (), dest.pub, balance, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	balance -= futurehead::Gxrb_ratio;
	futurehead::send_block block2 (block1.hash (), futurehead::genesis_account, balance, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block1.hash ()));
	balance += futurehead::Gxrb_ratio;
	futurehead::receive_block block3 (block2.hash (), block2.hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block2.hash ()));
	balance -= futurehead::Gxrb_ratio;
	futurehead::state_block block4 (futurehead::genesis_account, block3.hash (), futurehead::genesis_account, balance, dest.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block3.hash ()));
	balance -= futurehead::Gxrb_ratio;
	futurehead::state_block block5 (futurehead::genesis_account, block4.hash (), futurehead::genesis_account, balance, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block4.hash ()));
	balance += futurehead::Gxrb_ratio;
	futurehead::state_block block6 (futurehead::genesis_account, block5.hash (), futurehead::genesis_account, balance, block5.hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block5.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block1).code);
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block3).code);
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block5).code);
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block6).code);
	ASSERT_EQ (balance, ledger.balance (transaction, block6.hash ()));
	ASSERT_EQ (dest.pub, ledger.block_destination (transaction, block1));
	ASSERT_TRUE (ledger.block_source (transaction, block1).is_zero ());
	ASSERT_EQ (futurehead::genesis_account, ledger.block_destination (transaction, block2));
	ASSERT_TRUE (ledger.block_source (transaction, block2).is_zero ());
	ASSERT_TRUE (ledger.block_destination (transaction, block3).is_zero ());
	ASSERT_EQ (block2.hash (), ledger.block_source (transaction, block3));
	ASSERT_EQ (dest.pub, ledger.block_destination (transaction, block4));
	ASSERT_TRUE (ledger.block_source (transaction, block4).is_zero ());
	ASSERT_EQ (futurehead::genesis_account, ledger.block_destination (transaction, block5));
	ASSERT_TRUE (ledger.block_source (transaction, block5).is_zero ());
	ASSERT_TRUE (ledger.block_destination (transaction, block6).is_zero ());
	ASSERT_EQ (block5.hash (), ledger.block_source (transaction, block6));
}

TEST (ledger, state_account)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_EQ (futurehead::genesis_account, ledger.account (transaction, send1.hash ()));
}

TEST (ledger, state_send_receive)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.weight (futurehead::genesis_account));
	ASSERT_TRUE (store->pending_exists (transaction, futurehead::pending_key (futurehead::genesis_account, send1.hash ())));
	ASSERT_EQ (2, send2->sideband ().height);
	ASSERT_TRUE (send2->sideband ().details.is_send);
	ASSERT_FALSE (send2->sideband ().details.is_receive);
	ASSERT_FALSE (send2->sideband ().details.is_epoch);
	futurehead::state_block receive1 (futurehead::genesis_account, send1.hash (), futurehead::genesis_account, futurehead::genesis_amount, send1.hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store->block_exists (transaction, receive1.hash ()));
	auto receive2 (store->block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (futurehead::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::genesis_account));
	ASSERT_FALSE (store->pending_exists (transaction, futurehead::pending_key (futurehead::genesis_account, send1.hash ())));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
	ASSERT_EQ (3, receive2->sideband ().height);
	ASSERT_FALSE (receive2->sideband ().details.is_send);
	ASSERT_TRUE (receive2->sideband ().details.is_receive);
	ASSERT_FALSE (receive2->sideband ().details.is_epoch);
}

TEST (ledger, state_receive)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::send_block send1 (genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.weight (futurehead::genesis_account));
	futurehead::state_block receive1 (futurehead::genesis_account, send1.hash (), futurehead::genesis_account, futurehead::genesis_amount, send1.hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store->block_exists (transaction, receive1.hash ()));
	auto receive2 (store->block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (futurehead::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::genesis_account));
	ASSERT_EQ (3, receive2->sideband ().height);
	ASSERT_FALSE (receive2->sideband ().details.is_send);
	ASSERT_TRUE (receive2->sideband ().details.is_receive);
	ASSERT_FALSE (receive2->sideband ().details.is_epoch);
}

TEST (ledger, state_rep_change)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair rep;
	futurehead::state_block change1 (futurehead::genesis_account, genesis.hash (), rep.pub, futurehead::genesis_amount, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, change1).code);
	ASSERT_TRUE (store->block_exists (transaction, change1.hash ()));
	auto change2 (store->block_get (transaction, change1.hash ()));
	ASSERT_NE (nullptr, change2);
	ASSERT_EQ (change1, *change2);
	ASSERT_EQ (futurehead::genesis_amount, ledger.balance (transaction, change1.hash ()));
	ASSERT_EQ (0, ledger.amount (transaction, change1.hash ()));
	ASSERT_EQ (0, ledger.weight (futurehead::genesis_account));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (rep.pub));
	ASSERT_EQ (2, change2->sideband ().height);
	ASSERT_FALSE (change2->sideband ().details.is_send);
	ASSERT_FALSE (change2->sideband ().details.is_receive);
	ASSERT_FALSE (change2->sideband ().details.is_epoch);
}

TEST (ledger, state_open)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair destination;
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.weight (futurehead::genesis_account));
	ASSERT_TRUE (store->pending_exists (transaction, futurehead::pending_key (destination.pub, send1.hash ())));
	futurehead::state_block open1 (destination.pub, 0, futurehead::genesis_account, futurehead::Gxrb_ratio, send1.hash (), destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_FALSE (store->pending_exists (transaction, futurehead::pending_key (destination.pub, send1.hash ())));
	ASSERT_TRUE (store->block_exists (transaction, open1.hash ()));
	auto open2 (store->block_get (transaction, open1.hash ()));
	ASSERT_NE (nullptr, open2);
	ASSERT_EQ (open1, *open2);
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.balance (transaction, open1.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, open1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::genesis_account));
	ASSERT_EQ (ledger.cache.account_count, store->account_count (transaction));
	ASSERT_EQ (1, open2->sideband ().height);
	ASSERT_FALSE (open2->sideband ().details.is_send);
	ASSERT_TRUE (open2->sideband ().details.is_receive);
	ASSERT_FALSE (open2->sideband ().details.is_epoch);
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, send_after_state_fail)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::send_block send2 (send1.hash (), futurehead::genesis_account, futurehead::genesis_amount - (2 * futurehead::Gxrb_ratio), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::block_position, ledger.process (transaction, send2).code);
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, receive_after_state_fail)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::receive_block receive1 (send1.hash (), send1.hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::block_position, ledger.process (transaction, receive1).code);
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, change_after_state_fail)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::keypair rep;
	futurehead::change_block change1 (send1.hash (), rep.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::block_position, ledger.process (transaction, change1).code);
}

TEST (ledger, state_unreceivable_fail)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::send_block send1 (genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.weight (futurehead::genesis_account));
	futurehead::state_block receive1 (futurehead::genesis_account, send1.hash (), futurehead::genesis_account, futurehead::genesis_amount, 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::gap_source, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_receive_bad_amount_fail)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::send_block send1 (genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.weight (futurehead::genesis_account));
	futurehead::state_block receive1 (futurehead::genesis_account, send1.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, send1.hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::balance_mismatch, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_no_link_amount_fail)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::keypair rep;
	futurehead::state_block change1 (futurehead::genesis_account, send1.hash (), rep.pub, futurehead::genesis_amount, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::balance_mismatch, ledger.process (transaction, change1).code);
}

TEST (ledger, state_receive_wrong_account_fail)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.weight (futurehead::genesis_account));
	futurehead::keypair key;
	futurehead::state_block receive1 (key.pub, 0, futurehead::genesis_account, futurehead::Gxrb_ratio, send1.hash (), key.prv, key.pub, *pool.generate (key.pub));
	ASSERT_EQ (futurehead::process_result::unreceivable, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_open_state_fork)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair destination;
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::state_block open1 (destination.pub, 0, futurehead::genesis_account, futurehead::Gxrb_ratio, send1.hash (), destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open1).code);
	futurehead::open_block open2 (send1.hash (), futurehead::genesis_account, destination.pub, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::fork, ledger.process (transaction, open2).code);
	ASSERT_EQ (open1.root (), open2.root ());
}

TEST (ledger, state_state_open_fork)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair destination;
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::open_block open1 (send1.hash (), futurehead::genesis_account, destination.pub, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open1).code);
	futurehead::state_block open2 (destination.pub, 0, futurehead::genesis_account, futurehead::Gxrb_ratio, send1.hash (), destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::fork, ledger.process (transaction, open2).code);
	ASSERT_EQ (open1.root (), open2.root ());
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, state_open_previous_fail)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair destination;
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::state_block open1 (destination.pub, 1, futurehead::genesis_account, futurehead::Gxrb_ratio, send1.hash (), destination.prv, destination.pub, *pool.generate (1));
	ASSERT_EQ (futurehead::process_result::gap_previous, ledger.process (transaction, open1).code);
}

TEST (ledger, state_open_source_fail)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair destination;
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::state_block open1 (destination.pub, 0, futurehead::genesis_account, 0, 0, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::gap_source, ledger.process (transaction, open1).code);
}

TEST (ledger, state_send_change)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair rep;
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), rep.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (0, ledger.weight (futurehead::genesis_account));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.weight (rep.pub));
	ASSERT_EQ (2, send2->sideband ().height);
	ASSERT_TRUE (send2->sideband ().details.is_send);
	ASSERT_FALSE (send2->sideband ().details.is_receive);
	ASSERT_FALSE (send2->sideband ().details.is_epoch);
}

TEST (ledger, state_receive_change)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.weight (futurehead::genesis_account));
	futurehead::keypair rep;
	futurehead::state_block receive1 (futurehead::genesis_account, send1.hash (), rep.pub, futurehead::genesis_amount, send1.hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store->block_exists (transaction, receive1.hash ()));
	auto receive2 (store->block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (futurehead::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (0, ledger.weight (futurehead::genesis_account));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (rep.pub));
	ASSERT_EQ (3, receive2->sideband ().height);
	ASSERT_FALSE (receive2->sideband ().details.is_send);
	ASSERT_TRUE (receive2->sideband ().details.is_receive);
	ASSERT_FALSE (receive2->sideband ().details.is_epoch);
}

TEST (ledger, state_open_old)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair destination;
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::open_block open1 (send1.hash (), futurehead::genesis_account, destination.pub, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.balance (transaction, open1.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, open1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::genesis_account));
}

TEST (ledger, state_receive_old)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair destination;
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::state_block send2 (futurehead::genesis_account, send1.hash (), futurehead::genesis_account, futurehead::genesis_amount - (2 * futurehead::Gxrb_ratio), destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send2).code);
	futurehead::open_block open1 (send1.hash (), futurehead::genesis_account, destination.pub, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open1).code);
	futurehead::receive_block receive1 (open1.hash (), send2.hash (), destination.prv, destination.pub, *pool.generate (open1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_EQ (2 * futurehead::Gxrb_ratio, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::genesis_account));
}

TEST (ledger, state_rollback_send)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.account_balance (transaction, futurehead::genesis_account));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.weight (futurehead::genesis_account));
	futurehead::pending_info info;
	ASSERT_FALSE (store->pending_get (transaction, futurehead::pending_key (futurehead::genesis_account, send1.hash ()), info));
	ASSERT_EQ (futurehead::genesis_account, info.source);
	ASSERT_EQ (futurehead::Gxrb_ratio, info.amount.number ());
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount, ledger.account_balance (transaction, futurehead::genesis_account));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::genesis_account));
	ASSERT_FALSE (store->pending_exists (transaction, futurehead::pending_key (futurehead::genesis_account, send1.hash ())));
	ASSERT_TRUE (store->block_successor (transaction, genesis.hash ()).is_zero ());
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, state_rollback_receive)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::state_block receive1 (futurehead::genesis_account, send1.hash (), futurehead::genesis_account, futurehead::genesis_amount, send1.hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (store->pending_exists (transaction, futurehead::pending_key (futurehead::genesis_account, receive1.hash ())));
	ASSERT_FALSE (ledger.rollback (transaction, receive1.hash ()));
	futurehead::pending_info info;
	ASSERT_FALSE (store->pending_get (transaction, futurehead::pending_key (futurehead::genesis_account, send1.hash ()), info));
	ASSERT_EQ (futurehead::genesis_account, info.source);
	ASSERT_EQ (futurehead::Gxrb_ratio, info.amount.number ());
	ASSERT_FALSE (store->block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.account_balance (transaction, futurehead::genesis_account));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.weight (futurehead::genesis_account));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, state_rollback_received_send)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key;
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::state_block receive1 (key.pub, 0, key.pub, futurehead::Gxrb_ratio, send1.hash (), key.prv, key.pub, *pool.generate (key.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (store->pending_exists (transaction, futurehead::pending_key (futurehead::genesis_account, receive1.hash ())));
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_FALSE (store->pending_exists (transaction, futurehead::pending_key (futurehead::genesis_account, send1.hash ())));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount, ledger.account_balance (transaction, futurehead::genesis_account));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::genesis_account));
	ASSERT_EQ (0, ledger.account_balance (transaction, key.pub));
	ASSERT_EQ (0, ledger.weight (key.pub));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, state_rep_change_rollback)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair rep;
	futurehead::state_block change1 (futurehead::genesis_account, genesis.hash (), rep.pub, futurehead::genesis_amount, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, change1).code);
	ASSERT_FALSE (ledger.rollback (transaction, change1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, change1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount, ledger.account_balance (transaction, futurehead::genesis_account));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::genesis_account));
	ASSERT_EQ (0, ledger.weight (rep.pub));
}

TEST (ledger, state_open_rollback)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair destination;
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::state_block open1 (destination.pub, 0, futurehead::genesis_account, futurehead::Gxrb_ratio, send1.hash (), destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_FALSE (ledger.rollback (transaction, open1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, open1.hash ()));
	ASSERT_EQ (0, ledger.account_balance (transaction, destination.pub));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.weight (futurehead::genesis_account));
	futurehead::pending_info info;
	ASSERT_FALSE (store->pending_get (transaction, futurehead::pending_key (destination.pub, send1.hash ()), info));
	ASSERT_EQ (futurehead::genesis_account, info.source);
	ASSERT_EQ (futurehead::Gxrb_ratio, info.amount.number ());
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, state_send_change_rollback)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair rep;
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), rep.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount, ledger.account_balance (transaction, futurehead::genesis_account));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (futurehead::genesis_account));
	ASSERT_EQ (0, ledger.weight (rep.pub));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, state_receive_change_rollback)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::keypair rep;
	futurehead::state_block receive1 (futurehead::genesis_account, send1.hash (), rep.pub, futurehead::genesis_amount, send1.hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (ledger.rollback (transaction, receive1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.account_balance (transaction, futurehead::genesis_account));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.weight (futurehead::genesis_account));
	ASSERT_EQ (0, ledger.weight (rep.pub));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, epoch_blocks_v1_general)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair destination;
	futurehead::state_block epoch1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount, ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_FALSE (epoch1.sideband ().details.is_send);
	ASSERT_FALSE (epoch1.sideband ().details.is_receive);
	ASSERT_TRUE (epoch1.sideband ().details.is_epoch);
	futurehead::state_block epoch2 (futurehead::genesis_account, epoch1.hash (), futurehead::genesis_account, futurehead::genesis_amount, ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (futurehead::process_result::block_position, ledger.process (transaction, epoch2).code);
	futurehead::account_info genesis_info;
	ASSERT_FALSE (ledger.store.account_get (transaction, futurehead::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch (), futurehead::epoch::epoch_1);
	ASSERT_FALSE (ledger.rollback (transaction, epoch1.hash ()));
	ASSERT_FALSE (ledger.store.account_get (transaction, futurehead::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch (), futurehead::epoch::epoch_0);
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_FALSE (ledger.store.account_get (transaction, futurehead::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch (), futurehead::epoch::epoch_1);
	ASSERT_FALSE (epoch1.sideband ().details.is_send);
	ASSERT_FALSE (epoch1.sideband ().details.is_receive);
	ASSERT_TRUE (epoch1.sideband ().details.is_epoch);
	futurehead::change_block change1 (epoch1.hash (), futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (futurehead::process_result::block_position, ledger.process (transaction, change1).code);
	futurehead::state_block send1 (futurehead::genesis_account, epoch1.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (send1.sideband ().details.is_send);
	ASSERT_FALSE (send1.sideband ().details.is_receive);
	ASSERT_FALSE (send1.sideband ().details.is_epoch);
	futurehead::open_block open1 (send1.hash (), futurehead::genesis_account, destination.pub, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::unreceivable, ledger.process (transaction, open1).code);
	futurehead::state_block epoch3 (destination.pub, 0, futurehead::genesis_account, 0, ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::representative_mismatch, ledger.process (transaction, epoch3).code);
	futurehead::state_block epoch4 (destination.pub, 0, 0, 0, ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch4).code);
	ASSERT_FALSE (epoch4.sideband ().details.is_send);
	ASSERT_FALSE (epoch4.sideband ().details.is_receive);
	ASSERT_TRUE (epoch4.sideband ().details.is_epoch);
	futurehead::receive_block receive1 (epoch4.hash (), send1.hash (), destination.prv, destination.pub, *pool.generate (epoch4.hash ()));
	ASSERT_EQ (futurehead::process_result::block_position, ledger.process (transaction, receive1).code);
	futurehead::state_block receive2 (destination.pub, epoch4.hash (), destination.pub, futurehead::Gxrb_ratio, send1.hash (), destination.prv, destination.pub, *pool.generate (epoch4.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive2).code);
	ASSERT_EQ (0, ledger.balance (transaction, epoch4.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.balance (transaction, receive2.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, receive2.hash ()));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.weight (futurehead::genesis_account));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.weight (destination.pub));
	ASSERT_FALSE (receive2.sideband ().details.is_send);
	ASSERT_TRUE (receive2.sideband ().details.is_receive);
	ASSERT_FALSE (receive2.sideband ().details.is_epoch);
}

TEST (ledger, epoch_blocks_v2_general)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair destination;
	futurehead::state_block epoch1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount, ledger.epoch_link (futurehead::epoch::epoch_2), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	// Trying to upgrade from epoch 0 to epoch 2. It is a requirement epoch upgrades are sequential unless the account is unopened
	ASSERT_EQ (futurehead::process_result::block_position, ledger.process (transaction, epoch1).code);
	// Set it to the first epoch and it should now succeed
	epoch1 = futurehead::state_block (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount, ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, epoch1.work);
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch1).code);
	futurehead::state_block epoch2 (futurehead::genesis_account, epoch1.hash (), futurehead::genesis_account, futurehead::genesis_amount, ledger.epoch_link (futurehead::epoch::epoch_2), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch2).code);
	futurehead::state_block epoch3 (futurehead::genesis_account, epoch2.hash (), futurehead::genesis_account, futurehead::genesis_amount, ledger.epoch_link (futurehead::epoch::epoch_2), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (epoch2.hash ()));
	ASSERT_EQ (futurehead::process_result::block_position, ledger.process (transaction, epoch3).code);
	futurehead::account_info genesis_info;
	ASSERT_FALSE (ledger.store.account_get (transaction, futurehead::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch (), futurehead::epoch::epoch_2);
	ASSERT_FALSE (ledger.rollback (transaction, epoch1.hash ()));
	ASSERT_FALSE (ledger.store.account_get (transaction, futurehead::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch (), futurehead::epoch::epoch_0);
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_FALSE (ledger.store.account_get (transaction, futurehead::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch (), futurehead::epoch::epoch_1);
	futurehead::change_block change1 (epoch1.hash (), futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (futurehead::process_result::block_position, ledger.process (transaction, change1).code);
	futurehead::state_block send1 (futurehead::genesis_account, epoch1.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::open_block open1 (send1.hash (), futurehead::genesis_account, destination.pub, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::unreceivable, ledger.process (transaction, open1).code);
	futurehead::state_block epoch4 (destination.pub, 0, 0, 0, ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch4).code);
	futurehead::state_block epoch5 (destination.pub, epoch4.hash (), futurehead::genesis_account, 0, ledger.epoch_link (futurehead::epoch::epoch_2), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (epoch4.hash ()));
	ASSERT_EQ (futurehead::process_result::representative_mismatch, ledger.process (transaction, epoch5).code);
	futurehead::state_block epoch6 (destination.pub, epoch4.hash (), 0, 0, ledger.epoch_link (futurehead::epoch::epoch_2), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (epoch4.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch6).code);
	futurehead::receive_block receive1 (epoch6.hash (), send1.hash (), destination.prv, destination.pub, *pool.generate (epoch6.hash ()));
	ASSERT_EQ (futurehead::process_result::block_position, ledger.process (transaction, receive1).code);
	futurehead::state_block receive2 (destination.pub, epoch6.hash (), destination.pub, futurehead::Gxrb_ratio, send1.hash (), destination.prv, destination.pub, *pool.generate (epoch6.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive2).code);
	ASSERT_EQ (0, ledger.balance (transaction, epoch6.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.balance (transaction, receive2.hash ()));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.amount (transaction, receive2.hash ()));
	ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.weight (futurehead::genesis_account));
	ASSERT_EQ (futurehead::Gxrb_ratio, ledger.weight (destination.pub));
}

TEST (ledger, epoch_blocks_receive_upgrade)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair destination;
	futurehead::state_block send1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::state_block epoch1 (futurehead::genesis_account, send1.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch1).code);
	futurehead::state_block send2 (futurehead::genesis_account, epoch1.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio * 2, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send2).code);
	futurehead::open_block open1 (send1.hash (), destination.pub, destination.pub, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open1).code);
	futurehead::receive_block receive1 (open1.hash (), send2.hash (), destination.prv, destination.pub, *pool.generate (open1.hash ()));
	ASSERT_EQ (futurehead::process_result::unreceivable, ledger.process (transaction, receive1).code);
	futurehead::state_block receive2 (destination.pub, open1.hash (), destination.pub, futurehead::Gxrb_ratio * 2, send2.hash (), destination.prv, destination.pub, *pool.generate (open1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive2).code);
	futurehead::account_info destination_info;
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch (), futurehead::epoch::epoch_1);
	ASSERT_FALSE (ledger.rollback (transaction, receive2.hash ()));
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch (), futurehead::epoch::epoch_0);
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive2).code);
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch (), futurehead::epoch::epoch_1);
	futurehead::keypair destination2;
	futurehead::state_block send3 (destination.pub, receive2.hash (), destination.pub, futurehead::Gxrb_ratio, destination2.pub, destination.prv, destination.pub, *pool.generate (receive2.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send3).code);
	futurehead::open_block open2 (send3.hash (), destination2.pub, destination2.pub, destination2.prv, destination2.pub, *pool.generate (destination2.pub));
	ASSERT_EQ (futurehead::process_result::unreceivable, ledger.process (transaction, open2).code);
	// Upgrade to epoch 2 and send to destination. Try to create an open block from an epoch 2 source block.
	futurehead::keypair destination3;
	futurehead::state_block epoch2 (futurehead::genesis_account, send2.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio * 2, ledger.epoch_link (futurehead::epoch::epoch_2), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send2.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch2).code);
	futurehead::state_block send4 (futurehead::genesis_account, epoch2.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio * 3, destination3.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (epoch2.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send4).code);
	futurehead::open_block open3 (send4.hash (), destination3.pub, destination3.pub, destination3.prv, destination3.pub, *pool.generate (destination3.pub));
	ASSERT_EQ (futurehead::process_result::unreceivable, ledger.process (transaction, open3).code);
	// Send it to an epoch 1 account
	futurehead::state_block send5 (futurehead::genesis_account, send4.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio * 4, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send4.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send5).code);
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch (), futurehead::epoch::epoch_1);
	futurehead::state_block receive3 (destination.pub, send3.hash (), destination.pub, futurehead::Gxrb_ratio * 2, send5.hash (), destination.prv, destination.pub, *pool.generate (send3.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive3).code);
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch (), futurehead::epoch::epoch_2);
	// Upgrade an unopened account straight to epoch 2
	futurehead::keypair destination4;
	futurehead::state_block send6 (futurehead::genesis_account, send5.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio * 5, destination4.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send5.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send6).code);
	futurehead::state_block epoch4 (destination4.pub, 0, 0, 0, ledger.epoch_link (futurehead::epoch::epoch_2), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (destination4.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch4).code);
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, epoch_blocks_fork)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair destination;
	futurehead::send_block send1 (genesis.hash (), futurehead::account (0), futurehead::genesis_amount, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	futurehead::state_block epoch1 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount, ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::fork, ledger.process (transaction, epoch1).code);
	futurehead::state_block epoch2 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount, ledger.epoch_link (futurehead::epoch::epoch_2), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::fork, ledger.process (transaction, epoch2).code);
	futurehead::state_block epoch3 (futurehead::genesis_account, send1.hash (), futurehead::genesis_account, futurehead::genesis_amount, ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch3).code);
	futurehead::state_block epoch4 (futurehead::genesis_account, send1.hash (), futurehead::genesis_account, futurehead::genesis_amount, ledger.epoch_link (futurehead::epoch::epoch_2), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (futurehead::process_result::fork, ledger.process (transaction, epoch2).code);
}

TEST (ledger, successor_epoch)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::keypair key1;
	futurehead::genesis genesis;
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::send_block send1 (genesis.hash (), key1.pub, futurehead::genesis_amount - 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	futurehead::state_block open (key1.pub, 0, key1.pub, 1, send1.hash (), key1.prv, key1.pub, *pool.generate (key1.pub));
	futurehead::state_block change (key1.pub, open.hash (), key1.pub, 1, 0, key1.prv, key1.pub, *pool.generate (open.hash ()));
	auto open_hash = open.hash ();
	futurehead::send_block send2 (send1.hash (), reinterpret_cast<futurehead::account const &> (open_hash), futurehead::genesis_amount - 2, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send1.hash ()));
	futurehead::state_block epoch_open (reinterpret_cast<futurehead::account const &> (open_hash), 0, 0, 0, node1.ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (open.hash ()));
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, send1).code);
	ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, open).code);
	ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, change).code);
	ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, send2).code);
	ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, epoch_open).code);
	ASSERT_EQ (change, *node1.ledger.successor (transaction, change.qualified_root ()));
	ASSERT_EQ (epoch_open, *node1.ledger.successor (transaction, epoch_open.qualified_root ()));
}

TEST (ledger, epoch_open_pending)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	futurehead::state_block epoch_open (key1.pub, 0, 0, 0, node1.ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (key1.pub));
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (futurehead::process_result::block_position, node1.ledger.process (transaction, epoch_open).code);
}

TEST (ledger, block_hash_account_conflict)
{
	futurehead::block_builder builder;
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	futurehead::keypair key2;
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());

	/*
	 * Generate a send block whose destination is a block hash already
	 * in the ledger and not an account
	 */
	std::shared_ptr<futurehead::state_block> send1 = builder.state ()
	                                           .account (futurehead::genesis_account)
	                                           .previous (genesis.hash ())
	                                           .representative (futurehead::genesis_account)
	                                           .balance (futurehead::genesis_amount - 100)
	                                           .link (key1.pub)
	                                           .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                           .work (*pool.generate (genesis.hash ()))
	                                           .build ();

	std::shared_ptr<futurehead::state_block> receive1 = builder.state ()
	                                              .account (key1.pub)
	                                              .previous (0)
	                                              .representative (futurehead::genesis_account)
	                                              .balance (100)
	                                              .link (send1->hash ())
	                                              .sign (key1.prv, key1.pub)
	                                              .work (*pool.generate (key1.pub))
	                                              .build ();

	/*
	 * Note that the below link is a block hash when this is intended
	 * to represent a send state block. This can generally never be
	 * received , except by epoch blocks, which can sign an open block
	 * for arbitrary accounts.
	 */
	std::shared_ptr<futurehead::state_block> send2 = builder.state ()
	                                           .account (key1.pub)
	                                           .previous (receive1->hash ())
	                                           .representative (futurehead::genesis_account)
	                                           .balance (90)
	                                           .link (receive1->hash ())
	                                           .sign (key1.prv, key1.pub)
	                                           .work (*pool.generate (receive1->hash ()))
	                                           .build ();

	/*
	 * Generate an epoch open for the account with the same value as the block hash
	 */
	auto receive1_hash = receive1->hash ();
	std::shared_ptr<futurehead::state_block> open_epoch1 = builder.state ()
	                                                 .account (reinterpret_cast<futurehead::account const &> (receive1_hash))
	                                                 .previous (0)
	                                                 .representative (0)
	                                                 .balance (0)
	                                                 .link (node1.ledger.epoch_link (futurehead::epoch::epoch_1))
	                                                 .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                                 .work (*pool.generate (receive1->hash ()))
	                                                 .build ();

	node1.work_generate_blocking (*send1);
	node1.work_generate_blocking (*receive1);
	node1.work_generate_blocking (*send2);
	node1.work_generate_blocking (*open_epoch1);
	ASSERT_EQ (futurehead::process_result::progress, node1.process (*send1).code);
	ASSERT_EQ (futurehead::process_result::progress, node1.process (*receive1).code);
	ASSERT_EQ (futurehead::process_result::progress, node1.process (*send2).code);
	ASSERT_EQ (futurehead::process_result::progress, node1.process (*open_epoch1).code);
	futurehead::blocks_confirm (node1, { send1, receive1, send2, open_epoch1 });
	auto election1 = node1.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election1);
	auto election2 = node1.active.election (receive1->qualified_root ());
	ASSERT_NE (nullptr, election2);
	auto election3 = node1.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election3);
	auto election4 = node1.active.election (open_epoch1->qualified_root ());
	ASSERT_NE (nullptr, election4);
	futurehead::lock_guard<std::mutex> lock (node1.active.mutex);
	auto winner1 (*election1->tally ().begin ());
	auto winner2 (*election2->tally ().begin ());
	auto winner3 (*election3->tally ().begin ());
	auto winner4 (*election4->tally ().begin ());
	ASSERT_EQ (*send1, *winner1.second);
	ASSERT_EQ (*receive1, *winner2.second);
	ASSERT_EQ (*send2, *winner3.second);
	ASSERT_EQ (*open_epoch1, *winner4.second);
}

TEST (ledger, could_fit)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair destination;
	// Test legacy and state change blocks could_fit
	futurehead::change_block change1 (genesis.hash (), futurehead::genesis_account, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	futurehead::state_block change2 (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_TRUE (ledger.could_fit (transaction, change1));
	ASSERT_TRUE (ledger.could_fit (transaction, change2));
	// Test legacy and state send
	futurehead::keypair key1;
	futurehead::send_block send1 (change1.hash (), key1.pub, futurehead::genesis_amount - 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (change1.hash ()));
	futurehead::state_block send2 (futurehead::genesis_account, change1.hash (), futurehead::genesis_account, futurehead::genesis_amount - 1, key1.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (change1.hash ()));
	ASSERT_FALSE (ledger.could_fit (transaction, send1));
	ASSERT_FALSE (ledger.could_fit (transaction, send2));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, change1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, change1));
	ASSERT_TRUE (ledger.could_fit (transaction, change2));
	ASSERT_TRUE (ledger.could_fit (transaction, send1));
	ASSERT_TRUE (ledger.could_fit (transaction, send2));
	// Test legacy and state open
	futurehead::open_block open1 (send2.hash (), futurehead::genesis_account, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	futurehead::state_block open2 (key1.pub, 0, futurehead::genesis_account, 1, send2.hash (), key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_FALSE (ledger.could_fit (transaction, open1));
	ASSERT_FALSE (ledger.could_fit (transaction, open2));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send2).code);
	ASSERT_TRUE (ledger.could_fit (transaction, send1));
	ASSERT_TRUE (ledger.could_fit (transaction, send2));
	ASSERT_TRUE (ledger.could_fit (transaction, open1));
	ASSERT_TRUE (ledger.could_fit (transaction, open2));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, open1));
	ASSERT_TRUE (ledger.could_fit (transaction, open2));
	// Create another send to receive
	futurehead::state_block send3 (futurehead::genesis_account, send2.hash (), futurehead::genesis_account, futurehead::genesis_amount - 2, key1.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send2.hash ()));
	// Test legacy and state receive
	futurehead::receive_block receive1 (open1.hash (), send3.hash (), key1.prv, key1.pub, *pool.generate (open1.hash ()));
	futurehead::state_block receive2 (key1.pub, open1.hash (), futurehead::genesis_account, 2, send3.hash (), key1.prv, key1.pub, *pool.generate (open1.hash ()));
	ASSERT_FALSE (ledger.could_fit (transaction, receive1));
	ASSERT_FALSE (ledger.could_fit (transaction, receive2));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send3).code);
	ASSERT_TRUE (ledger.could_fit (transaction, receive1));
	ASSERT_TRUE (ledger.could_fit (transaction, receive2));
	// Test epoch (state)
	futurehead::state_block epoch1 (key1.pub, receive1.hash (), futurehead::genesis_account, 2, ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (receive1.hash ()));
	ASSERT_FALSE (ledger.could_fit (transaction, epoch1));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, receive1));
	ASSERT_TRUE (ledger.could_fit (transaction, receive2));
	ASSERT_TRUE (ledger.could_fit (transaction, epoch1));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, epoch1));
}

TEST (ledger, unchecked_epoch)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::keypair destination;
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto open1 (std::make_shared<futurehead::state_block> (destination.pub, 0, destination.pub, futurehead::Gxrb_ratio, send1->hash (), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	auto epoch1 (std::make_shared<futurehead::state_block> (destination.pub, open1->hash (), destination.pub, futurehead::Gxrb_ratio, node1.ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*epoch1);
	node1.block_processor.add (epoch1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		ASSERT_EQ (unchecked_count, node1.ledger.cache.unchecked_count);
		auto blocks (node1.store.unchecked_get (transaction, epoch1->previous ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, futurehead::signature_verification::valid_epoch);
	}
	node1.block_processor.add (send1);
	node1.block_processor.add (open1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, epoch1->hash ()));
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
		ASSERT_EQ (unchecked_count, node1.ledger.cache.unchecked_count);
		futurehead::account_info info;
		ASSERT_FALSE (node1.store.account_get (transaction, destination.pub, info));
		ASSERT_EQ (info.epoch (), futurehead::epoch::epoch_1);
	}
}

TEST (ledger, unchecked_epoch_invalid)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto & node1 (*system.add_node (node_config));
	futurehead::genesis genesis;
	futurehead::keypair destination;
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto open1 (std::make_shared<futurehead::state_block> (destination.pub, 0, destination.pub, futurehead::Gxrb_ratio, send1->hash (), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	// Epoch block with account own signature
	auto epoch1 (std::make_shared<futurehead::state_block> (destination.pub, open1->hash (), destination.pub, futurehead::Gxrb_ratio, node1.ledger.epoch_link (futurehead::epoch::epoch_1), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*epoch1);
	// Pseudo epoch block (send subtype, destination - epoch link)
	auto epoch2 (std::make_shared<futurehead::state_block> (destination.pub, open1->hash (), destination.pub, futurehead::Gxrb_ratio - 1, node1.ledger.epoch_link (futurehead::epoch::epoch_1), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*epoch2);
	node1.block_processor.add (epoch1);
	node1.block_processor.add (epoch2);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 2);
		ASSERT_EQ (unchecked_count, node1.ledger.cache.unchecked_count);
		auto blocks (node1.store.unchecked_get (transaction, epoch1->previous ()));
		ASSERT_EQ (blocks.size (), 2);
		ASSERT_EQ (blocks[0].verified, futurehead::signature_verification::valid);
		ASSERT_EQ (blocks[1].verified, futurehead::signature_verification::valid);
	}
	node1.block_processor.add (send1);
	node1.block_processor.add (open1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_FALSE (node1.store.block_exists (transaction, epoch1->hash ()));
		ASSERT_TRUE (node1.store.block_exists (transaction, epoch2->hash ()));
		ASSERT_TRUE (node1.active.empty ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
		ASSERT_EQ (unchecked_count, node1.ledger.cache.unchecked_count);
		futurehead::account_info info;
		ASSERT_FALSE (node1.store.account_get (transaction, destination.pub, info));
		ASSERT_NE (info.epoch (), futurehead::epoch::epoch_1);
		auto epoch2_store (node1.store.block_get (transaction, epoch2->hash ()));
		ASSERT_NE (nullptr, epoch2_store);
		ASSERT_EQ (futurehead::epoch::epoch_0, epoch2_store->sideband ().details.epoch);
		ASSERT_TRUE (epoch2_store->sideband ().details.is_send);
		ASSERT_FALSE (epoch2_store->sideband ().details.is_epoch);
		ASSERT_FALSE (epoch2_store->sideband ().details.is_receive);
	}
}

TEST (ledger, unchecked_open)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::keypair destination;
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto open1 (std::make_shared<futurehead::open_block> (send1->hash (), destination.pub, destination.pub, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	// Invalid signature for open block
	auto open2 (std::make_shared<futurehead::open_block> (send1->hash (), futurehead::test_genesis_key.pub, destination.pub, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open2);
	open2->signature.bytes[0] ^= 1;
	node1.block_processor.add (open1);
	node1.block_processor.add (open2);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		ASSERT_EQ (unchecked_count, node1.ledger.cache.unchecked_count);
		auto blocks (node1.store.unchecked_get (transaction, open1->source ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, futurehead::signature_verification::valid);
	}
	node1.block_processor.add (send1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, open1->hash ()));
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
		ASSERT_EQ (unchecked_count, node1.ledger.cache.unchecked_count);
	}
}

TEST (ledger, unchecked_receive)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::keypair destination;
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount - futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto send2 (std::make_shared<futurehead::state_block> (futurehead::genesis_account, send1->hash (), futurehead::genesis_account, futurehead::genesis_amount - 2 * futurehead::Gxrb_ratio, destination.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto open1 (std::make_shared<futurehead::open_block> (send1->hash (), destination.pub, destination.pub, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	auto receive1 (std::make_shared<futurehead::receive_block> (open1->hash (), send2->hash (), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*receive1);
	node1.block_processor.add (send1);
	node1.block_processor.add (receive1);
	node1.block_processor.flush ();
	// Previous block for receive1 is unknown, signature cannot be validated
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		ASSERT_EQ (unchecked_count, node1.ledger.cache.unchecked_count);
		auto blocks (node1.store.unchecked_get (transaction, receive1->previous ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, futurehead::signature_verification::unknown);
	}
	node1.block_processor.add (open1);
	node1.block_processor.flush ();
	// Previous block for receive1 is known, signature was validated
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		ASSERT_EQ (unchecked_count, node1.ledger.cache.unchecked_count);
		auto blocks (node1.store.unchecked_get (transaction, receive1->source ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, futurehead::signature_verification::valid);
	}
	node1.block_processor.add (send2);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, receive1->hash ()));
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
		ASSERT_EQ (unchecked_count, node1.ledger.cache.unchecked_count);
	}
}

TEST (ledger, confirmation_height_not_updated)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	auto transaction (store->tx_begin_write ());
	futurehead::genesis genesis;
	store->initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::account_info account_info;
	ASSERT_FALSE (store->account_get (transaction, futurehead::test_genesis_key.pub, account_info));
	futurehead::keypair key;
	futurehead::send_block send1 (account_info.head, key.pub, 50, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (account_info.head));
	futurehead::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height_get (transaction, futurehead::genesis_account, confirmation_height_info));
	ASSERT_EQ (1, confirmation_height_info.height);
	ASSERT_EQ (genesis.hash (), confirmation_height_info.frontier);
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_FALSE (store->confirmation_height_get (transaction, futurehead::genesis_account, confirmation_height_info));
	ASSERT_EQ (1, confirmation_height_info.height);
	ASSERT_EQ (genesis.hash (), confirmation_height_info.frontier);
	futurehead::open_block open1 (send1.hash (), futurehead::genesis_account, key.pub, key.prv, key.pub, *pool.generate (key.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_FALSE (store->confirmation_height_get (transaction, key.pub, confirmation_height_info));
	ASSERT_EQ (0, confirmation_height_info.height);
	ASSERT_EQ (futurehead::block_hash (0), confirmation_height_info.frontier);
}

TEST (ledger, zero_rep)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::block_builder builder;
	auto block1 = builder.state ()
	              .account (futurehead::test_genesis_key.pub)
	              .previous (genesis.hash ())
	              .representative (0)
	              .balance (futurehead::genesis_amount)
	              .link (0)
	              .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	              .work (*system.work.generate (genesis.hash ()))
	              .build ();
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, *block1).code);
	ASSERT_EQ (0, node1.ledger.cache.rep_weights.representation_get (futurehead::test_genesis_key.pub));
	ASSERT_EQ (futurehead::genesis_amount, node1.ledger.cache.rep_weights.representation_get (0));
	auto block2 = builder.state ()
	              .account (futurehead::test_genesis_key.pub)
	              .previous (block1->hash ())
	              .representative (futurehead::test_genesis_key.pub)
	              .balance (futurehead::genesis_amount)
	              .link (0)
	              .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	              .work (*system.work.generate (block1->hash ()))
	              .build ();
	ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, *block2).code);
	ASSERT_EQ (futurehead::genesis_amount, node1.ledger.cache.rep_weights.representation_get (futurehead::test_genesis_key.pub));
	ASSERT_EQ (0, node1.ledger.cache.rep_weights.representation_get (0));
}

TEST (ledger, work_validation)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	futurehead::genesis genesis;
	store->initialize (store->tx_begin_write (), genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::block_builder builder;
	auto gen = futurehead::test_genesis_key;
	futurehead::keypair key;

	// With random work the block doesn't pass, then modifies the block with sufficient work and ensures a correct result
	auto process_block = [&store, &ledger, &pool](futurehead::block & block_a, futurehead::block_details const details_a) {
		auto threshold = futurehead::work_threshold (block_a.work_version (), details_a);
		// Rarely failed with random work, so modify until it doesn't have enough difficulty
		while (block_a.difficulty () >= threshold)
		{
			block_a.block_work_set (block_a.block_work () + 1);
		}
		EXPECT_EQ (futurehead::process_result::insufficient_work, ledger.process (store->tx_begin_write (), block_a).code);
		block_a.block_work_set (*pool.generate (block_a.root (), threshold));
		EXPECT_EQ (futurehead::process_result::progress, ledger.process (store->tx_begin_write (), block_a).code);
	};

	std::error_code ec;

	auto send = *builder.send ()
	             .previous (futurehead::genesis_hash)
	             .destination (gen.pub)
	             .balance (futurehead::genesis_amount - 1)
	             .sign (gen.prv, gen.pub)
	             .work (0)
	             .build (ec);
	ASSERT_FALSE (ec);

	auto receive = *builder.receive ()
	                .previous (send.hash ())
	                .source (send.hash ())
	                .sign (gen.prv, gen.pub)
	                .work (0)
	                .build (ec);
	ASSERT_FALSE (ec);

	auto change = *builder.change ()
	               .previous (receive.hash ())
	               .representative (key.pub)
	               .sign (gen.prv, gen.pub)
	               .work (0)
	               .build (ec);
	ASSERT_FALSE (ec);

	auto state = *builder.state ()
	              .account (gen.pub)
	              .previous (change.hash ())
	              .representative (gen.pub)
	              .balance (futurehead::genesis_amount - 1)
	              .link (key.pub)
	              .sign (gen.prv, gen.pub)
	              .work (0)
	              .build (ec);
	ASSERT_FALSE (ec);

	auto open = *builder.open ()
	             .account (key.pub)
	             .source (state.hash ())
	             .representative (key.pub)
	             .sign (key.prv, key.pub)
	             .work (0)
	             .build (ec);
	ASSERT_FALSE (ec);

	auto epoch = *builder.state ()
	              .account (key.pub)
	              .previous (open.hash ())
	              .balance (1)
	              .representative (key.pub)
	              .link (ledger.epoch_link (futurehead::epoch::epoch_1))
	              .sign (gen.prv, gen.pub)
	              .work (0)
	              .build (ec);
	ASSERT_FALSE (ec);

	process_block (send, {});
	process_block (receive, {});
	process_block (change, {});
	process_block (state, futurehead::block_details (futurehead::epoch::epoch_0, true, false, false));
	process_block (open, {});
	process_block (epoch, futurehead::block_details (futurehead::epoch::epoch_1, false, false, true));
}

TEST (ledger, epoch_2_started_flag)
{
	futurehead::system system (2);

	auto & node1 = *system.nodes[0];
	ASSERT_FALSE (node1.ledger.cache.epoch_2_started.load ());
	ASSERT_NE (nullptr, system.upgrade_genesis_epoch (node1, futurehead::epoch::epoch_1));
	ASSERT_FALSE (node1.ledger.cache.epoch_2_started.load ());
	ASSERT_NE (nullptr, system.upgrade_genesis_epoch (node1, futurehead::epoch::epoch_2));
	ASSERT_TRUE (node1.ledger.cache.epoch_2_started.load ());

	auto & node2 = *system.nodes[1];
	futurehead::keypair key;
	auto epoch1 = system.upgrade_genesis_epoch (node2, futurehead::epoch::epoch_1);
	ASSERT_NE (nullptr, epoch1);
	ASSERT_FALSE (node2.ledger.cache.epoch_2_started.load ());
	futurehead::state_block send (futurehead::test_genesis_key.pub, epoch1->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 1, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (epoch1->hash ()));
	ASSERT_EQ (futurehead::process_result::progress, node2.process (send).code);
	ASSERT_FALSE (node2.ledger.cache.epoch_2_started.load ());
	futurehead::state_block epoch2 (key.pub, 0, 0, 0, node2.ledger.epoch_link (futurehead::epoch::epoch_2), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (key.pub));
	ASSERT_EQ (futurehead::process_result::progress, node2.process (epoch2).code);
	ASSERT_TRUE (node2.ledger.cache.epoch_2_started.load ());

	// Ensure state is kept on ledger initialization
	futurehead::stat stats;
	futurehead::ledger ledger (node1.store, stats);
	ASSERT_TRUE (ledger.cache.epoch_2_started.load ());
}

TEST (ledger, epoch_2_upgrade_callback)
{
	futurehead::genesis genesis;
	futurehead::stat stats;
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	bool cb_hit = false;
	futurehead::ledger ledger (*store, stats, futurehead::generate_cache (), [&cb_hit]() {
		cb_hit = true;
	});
	{
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, genesis, ledger.cache);
	}
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	upgrade_epoch (pool, ledger, futurehead::epoch::epoch_1);
	ASSERT_FALSE (cb_hit);
	auto latest = upgrade_epoch (pool, ledger, futurehead::epoch::epoch_2);
	ASSERT_TRUE (cb_hit);
}

TEST (ledger, can_vote)
{
	futurehead::block_builder builder;
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (*store, stats);
	auto transaction (store->tx_begin_write ());
	futurehead::genesis genesis;
	store->initialize (transaction, genesis, ledger.cache);
	ASSERT_TRUE (ledger.can_vote (transaction, *genesis.open));
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::keypair key1;
	std::shared_ptr<futurehead::state_block> send1 = builder.state ()
	                                           .account (futurehead::genesis_account)
	                                           .previous (genesis.hash ())
	                                           .representative (futurehead::genesis_account)
	                                           .balance (futurehead::genesis_amount - 100)
	                                           .link (key1.pub)
	                                           .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                           .work (*pool.generate (genesis.hash ()))
	                                           .build ();
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, *send1).code);
	ASSERT_TRUE (ledger.can_vote (transaction, *send1));
	std::shared_ptr<futurehead::state_block> send2 = builder.state ()
	                                           .account (futurehead::genesis_account)
	                                           .previous (send1->hash ())
	                                           .representative (futurehead::genesis_account)
	                                           .balance (futurehead::genesis_amount - 200)
	                                           .link (key1.pub)
	                                           .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                           .work (*pool.generate (send1->hash ()))
	                                           .build ();
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, *send2).code);
	ASSERT_FALSE (ledger.can_vote (transaction, *send2));
	std::shared_ptr<futurehead::state_block> receive1 = builder.state ()
	                                              .account (key1.pub)
	                                              .previous (0)
	                                              .representative (futurehead::genesis_account)
	                                              .balance (100)
	                                              .link (send1->hash ())
	                                              .sign (key1.prv, key1.pub)
	                                              .work (*pool.generate (key1.pub))
	                                              .build ();
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, *receive1).code);
	ASSERT_FALSE (ledger.can_vote (transaction, *receive1));
	futurehead::confirmation_height_info height;
	ASSERT_FALSE (ledger.store.confirmation_height_get (transaction, futurehead::genesis_account, height));
	height.height += 1;
	ledger.store.confirmation_height_put (transaction, futurehead::genesis_account, height);
	ASSERT_TRUE (ledger.can_vote (transaction, *receive1));
	std::shared_ptr<futurehead::state_block> receive2 = builder.state ()
	                                              .account (key1.pub)
	                                              .previous (receive1->hash ())
	                                              .representative (futurehead::genesis_account)
	                                              .balance (200)
	                                              .link (send2->hash ())
	                                              .sign (key1.prv, key1.pub)
	                                              .work (*pool.generate (receive1->hash ()))
	                                              .build ();
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, *receive2).code);
	ASSERT_FALSE (ledger.can_vote (transaction, *receive2));
	ASSERT_FALSE (ledger.store.confirmation_height_get (transaction, key1.pub, height));
	height.height += 1;
	ledger.store.confirmation_height_put (transaction, key1.pub, height);
	ASSERT_FALSE (ledger.can_vote (transaction, *receive2));
	ASSERT_FALSE (ledger.store.confirmation_height_get (transaction, futurehead::genesis_account, height));
	height.height += 1;
	ledger.store.confirmation_height_put (transaction, futurehead::genesis_account, height);
	ASSERT_TRUE (ledger.can_vote (transaction, *receive2));
}

TEST (ledger, backtrack)
{
	futurehead::genesis genesis;
	futurehead::stat stats;
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	bool cb_hit = false;
	futurehead::ledger ledger (*store, stats, futurehead::generate_cache (), [&cb_hit]() {
		cb_hit = true;
	});
	{
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, genesis, ledger.cache);
	}
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	std::vector<std::shared_ptr<futurehead::block>> blocks;
	blocks.push_back (nullptr); // idx == height
	blocks.push_back (genesis.open);
	auto amount = futurehead::genesis_amount;
	for (auto i = 0; i < 300; ++i)
	{
		futurehead::block_builder builder;
		std::error_code ec;
		auto latest = blocks.back ();
		blocks.push_back (builder.state ()
		                  .previous (latest->hash ())
		                  .account (futurehead::test_genesis_key.pub)
		                  .representative (futurehead::test_genesis_key.pub)
		                  .balance (--amount)
		                  .link (futurehead::test_genesis_key.pub)
		                  .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
		                  .work (*pool.generate (latest->hash ()))
		                  .build (ec));
		ASSERT_FALSE (ec);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (store->tx_begin_write (), *blocks.back ()).code);
	}
	ASSERT_EQ (302, blocks.size ());
	ASSERT_EQ (301, blocks[301]->sideband ().height);
	auto transaction (store->tx_begin_read ());
	auto block_100 = ledger.backtrack (transaction, blocks[300], 200);
	ASSERT_NE (nullptr, block_100);
	ASSERT_EQ (*block_100, *blocks[100]);
	ASSERT_NE (nullptr, ledger.backtrack (transaction, blocks[10], 10));
	ASSERT_NE (ledger.backtrack (transaction, blocks[10], 1), ledger.backtrack (transaction, blocks[11], 2));
	ASSERT_EQ (ledger.backtrack (transaction, blocks[1], 0), ledger.backtrack (transaction, blocks[1], 1));
	ASSERT_NE (ledger.backtrack (transaction, blocks[2], 0), ledger.backtrack (transaction, blocks[2], 1));
	ASSERT_EQ (nullptr, ledger.backtrack (transaction, nullptr, 0));
	ASSERT_EQ (nullptr, ledger.backtrack (transaction, nullptr, 10));
}
