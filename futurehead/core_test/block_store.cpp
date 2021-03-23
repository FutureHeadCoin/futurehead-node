#include <futurehead/core_test/testutil.hpp>
#include <futurehead/crypto_lib/random_pool.hpp>
#include <futurehead/lib/lmdbconfig.hpp>
#include <futurehead/lib/stats.hpp>
#include <futurehead/lib/utility.hpp>
#include <futurehead/lib/work.hpp>
#include <futurehead/node/common.hpp>
#include <futurehead/secure/ledger.hpp>
#include <futurehead/secure/utility.hpp>
#include <futurehead/secure/versioning.hpp>

#include <boost/filesystem.hpp>

#if FUTUREHEAD_ROCKSDB
#include <futurehead/node/rocksdb/rocksdb.hpp>
#endif

#include <futurehead/lib/logger_mt.hpp>
#include <futurehead/node/lmdb/lmdb.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <unordered_set>

#include <stdlib.h>

namespace
{
void modify_account_info_to_v13 (futurehead::mdb_store & store, futurehead::transaction const & transaction_a, futurehead::account const & account_a, futurehead::block_hash const & rep_block);
void modify_account_info_to_v14 (futurehead::mdb_store & store, futurehead::transaction const & transaction_a, futurehead::account const & account_a, uint64_t confirmation_height, futurehead::block_hash const & rep_block);
void modify_genesis_account_info_to_v5 (futurehead::mdb_store & store, futurehead::transaction const & transaction_a);
void modify_confirmation_height_to_v15 (futurehead::mdb_store & store, futurehead::transaction const & transaction, futurehead::account const & account, uint64_t confirmation_height);
void write_sideband_v12 (futurehead::mdb_store & store_a, futurehead::transaction & transaction_a, futurehead::block & block_a, futurehead::block_hash const & successor_a, MDB_dbi db_a);
void write_sideband_v14 (futurehead::mdb_store & store_a, futurehead::transaction & transaction_a, futurehead::block const & block_a, MDB_dbi db_a);
void write_sideband_v15 (futurehead::mdb_store & store_a, futurehead::transaction & transaction_a, futurehead::block const & block_a);
}

TEST (block_store, construction)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
}

TEST (block_store, block_details)
{
	futurehead::block_details details_send (futurehead::epoch::epoch_0, true, false, false);
	ASSERT_TRUE (details_send.is_send);
	ASSERT_FALSE (details_send.is_receive);
	ASSERT_FALSE (details_send.is_epoch);
	ASSERT_EQ (futurehead::epoch::epoch_0, details_send.epoch);

	futurehead::block_details details_receive (futurehead::epoch::epoch_1, false, true, false);
	ASSERT_FALSE (details_receive.is_send);
	ASSERT_TRUE (details_receive.is_receive);
	ASSERT_FALSE (details_receive.is_epoch);
	ASSERT_EQ (futurehead::epoch::epoch_1, details_receive.epoch);

	futurehead::block_details details_epoch (futurehead::epoch::epoch_2, false, false, true);
	ASSERT_FALSE (details_epoch.is_send);
	ASSERT_FALSE (details_epoch.is_receive);
	ASSERT_TRUE (details_epoch.is_epoch);
	ASSERT_EQ (futurehead::epoch::epoch_2, details_epoch.epoch);

	futurehead::block_details details_none (futurehead::epoch::unspecified, false, false, false);
	ASSERT_FALSE (details_none.is_send);
	ASSERT_FALSE (details_none.is_receive);
	ASSERT_FALSE (details_none.is_epoch);
	ASSERT_EQ (futurehead::epoch::unspecified, details_none.epoch);
}

TEST (block_store, block_details_serialization)
{
	futurehead::block_details details1;
	details1.epoch = futurehead::epoch::epoch_2;
	details1.is_epoch = false;
	details1.is_receive = true;
	details1.is_send = false;
	std::vector<uint8_t> vector;
	{
		futurehead::vectorstream stream1 (vector);
		details1.serialize (stream1);
	}
	futurehead::bufferstream stream2 (vector.data (), vector.size ());
	futurehead::block_details details2;
	ASSERT_FALSE (details2.deserialize (stream2));
	ASSERT_EQ (details1, details2);
}

TEST (block_store, sideband_serialization)
{
	futurehead::block_sideband sideband1;
	sideband1.account = 1;
	sideband1.balance = 2;
	sideband1.height = 3;
	sideband1.successor = 4;
	sideband1.timestamp = 5;
	std::vector<uint8_t> vector;
	{
		futurehead::vectorstream stream1 (vector);
		sideband1.serialize (stream1, futurehead::block_type::receive);
	}
	futurehead::bufferstream stream2 (vector.data (), vector.size ());
	futurehead::block_sideband sideband2;
	ASSERT_FALSE (sideband2.deserialize (stream2, futurehead::block_type::receive));
	ASSERT_EQ (sideband1.account, sideband2.account);
	ASSERT_EQ (sideband1.balance, sideband2.balance);
	ASSERT_EQ (sideband1.height, sideband2.height);
	ASSERT_EQ (sideband1.successor, sideband2.successor);
	ASSERT_EQ (sideband1.timestamp, sideband2.timestamp);
}

TEST (block_store, add_item)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::open_block block (0, 1, 0, futurehead::keypair ().prv, 0, 0);
	block.sideband_set ({});
	auto hash1 (block.hash ());
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	ASSERT_FALSE (store->block_exists (transaction, hash1));
	store->block_put (transaction, hash1, block);
	auto latest2 (store->block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
	ASSERT_TRUE (store->block_exists (transaction, hash1));
	ASSERT_FALSE (store->block_exists (transaction, hash1.number () - 1));
	store->block_del (transaction, hash1, block.type ());
	auto latest3 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest3);
}

TEST (block_store, clear_successor)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::open_block block1 (0, 1, 0, futurehead::keypair ().prv, 0, 0);
	block1.sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block_put (transaction, block1.hash (), block1);
	futurehead::open_block block2 (0, 2, 0, futurehead::keypair ().prv, 0, 0);
	block2.sideband_set ({});
	store->block_put (transaction, block2.hash (), block2);
	auto block2_store (store->block_get (transaction, block1.hash ()));
	ASSERT_NE (nullptr, block2_store);
	ASSERT_EQ (0, block2_store->sideband ().successor.number ());
	auto modified_sideband = block2_store->sideband ();
	modified_sideband.successor = block2.hash ();
	block1.sideband_set (modified_sideband);
	store->block_put (transaction, block1.hash (), block1);
	{
		auto block1_store (store->block_get (transaction, block1.hash ()));
		ASSERT_NE (nullptr, block1_store);
		ASSERT_EQ (block2.hash (), block1_store->sideband ().successor);
	}
	store->block_successor_clear (transaction, block1.hash ());
	{
		auto block1_store (store->block_get (transaction, block1.hash ()));
		ASSERT_NE (nullptr, block1_store);
		ASSERT_EQ (0, block1_store->sideband ().successor.number ());
	}
}

TEST (block_store, add_nonempty_block)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::keypair key1;
	futurehead::open_block block (0, 1, 0, futurehead::keypair ().prv, 0, 0);
	block.sideband_set ({});
	auto hash1 (block.hash ());
	block.signature = futurehead::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	store->block_put (transaction, hash1, block);
	auto latest2 (store->block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_two_items)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::keypair key1;
	futurehead::open_block block (0, 1, 1, futurehead::keypair ().prv, 0, 0);
	block.sideband_set ({});
	auto hash1 (block.hash ());
	block.signature = futurehead::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	futurehead::open_block block2 (0, 1, 3, futurehead::keypair ().prv, 0, 0);
	block2.sideband_set ({});
	block2.hashables.account = 3;
	auto hash2 (block2.hash ());
	block2.signature = futurehead::sign_message (key1.prv, key1.pub, hash2);
	auto latest2 (store->block_get (transaction, hash2));
	ASSERT_EQ (nullptr, latest2);
	store->block_put (transaction, hash1, block);
	store->block_put (transaction, hash2, block2);
	auto latest3 (store->block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest3);
	ASSERT_EQ (block, *latest3);
	auto latest4 (store->block_get (transaction, hash2));
	ASSERT_NE (nullptr, latest4);
	ASSERT_EQ (block2, *latest4);
	ASSERT_FALSE (*latest3 == *latest4);
}

TEST (block_store, add_receive)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::keypair key1;
	futurehead::keypair key2;
	futurehead::open_block block1 (0, 1, 0, futurehead::keypair ().prv, 0, 0);
	block1.sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block_put (transaction, block1.hash (), block1);
	futurehead::receive_block block (block1.hash (), 1, futurehead::keypair ().prv, 2, 3);
	block.sideband_set ({});
	futurehead::block_hash hash1 (block.hash ());
	auto latest1 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	store->block_put (transaction, hash1, block);
	auto latest2 (store->block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_pending)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::keypair key1;
	futurehead::pending_key key2 (0, 0);
	futurehead::pending_info pending1;
	auto transaction (store->tx_begin_write ());
	ASSERT_TRUE (store->pending_get (transaction, key2, pending1));
	store->pending_put (transaction, key2, pending1);
	futurehead::pending_info pending2;
	ASSERT_FALSE (store->pending_get (transaction, key2, pending2));
	ASSERT_EQ (pending1, pending2);
	store->pending_del (transaction, key2);
	ASSERT_TRUE (store->pending_get (transaction, key2, pending2));
}

TEST (block_store, pending_iterator)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	ASSERT_EQ (store->pending_end (), store->pending_begin (transaction));
	store->pending_put (transaction, futurehead::pending_key (1, 2), { 2, 3, futurehead::epoch::epoch_1 });
	auto current (store->pending_begin (transaction));
	ASSERT_NE (store->pending_end (), current);
	futurehead::pending_key key1 (current->first);
	ASSERT_EQ (futurehead::account (1), key1.account);
	ASSERT_EQ (futurehead::block_hash (2), key1.hash);
	futurehead::pending_info pending (current->second);
	ASSERT_EQ (futurehead::account (2), pending.source);
	ASSERT_EQ (futurehead::amount (3), pending.amount);
	ASSERT_EQ (futurehead::epoch::epoch_1, pending.epoch);
}

/**
 * Regression test for Issue 1164
 * This reconstructs the situation where a key is larger in pending than the account being iterated in pending_v1, leaving
 * iteration order up to the value, causing undefined behavior.
 * After the bugfix, the value is compared only if the keys are equal.
 */
TEST (block_store, pending_iterator_comparison)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::stat stats;
	auto transaction (store->tx_begin_write ());
	// Populate pending
	store->pending_put (transaction, futurehead::pending_key (futurehead::account (3), futurehead::block_hash (1)), futurehead::pending_info (futurehead::account (10), futurehead::amount (1), futurehead::epoch::epoch_0));
	store->pending_put (transaction, futurehead::pending_key (futurehead::account (3), futurehead::block_hash (4)), futurehead::pending_info (futurehead::account (10), futurehead::amount (0), futurehead::epoch::epoch_0));
	// Populate pending_v1
	store->pending_put (transaction, futurehead::pending_key (futurehead::account (2), futurehead::block_hash (2)), futurehead::pending_info (futurehead::account (10), futurehead::amount (2), futurehead::epoch::epoch_1));
	store->pending_put (transaction, futurehead::pending_key (futurehead::account (2), futurehead::block_hash (3)), futurehead::pending_info (futurehead::account (10), futurehead::amount (3), futurehead::epoch::epoch_1));

	// Iterate account 3 (pending)
	{
		size_t count = 0;
		futurehead::account begin (3);
		futurehead::account end (begin.number () + 1);
		for (auto i (store->pending_begin (transaction, futurehead::pending_key (begin, 0))), n (store->pending_begin (transaction, futurehead::pending_key (end, 0))); i != n; ++i, ++count)
		{
			futurehead::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}

	// Iterate account 2 (pending_v1)
	{
		size_t count = 0;
		futurehead::account begin (2);
		futurehead::account end (begin.number () + 1);
		for (auto i (store->pending_begin (transaction, futurehead::pending_key (begin, 0))), n (store->pending_begin (transaction, futurehead::pending_key (end, 0))); i != n; ++i, ++count)
		{
			futurehead::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}
}

TEST (block_store, genesis)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::genesis genesis;
	auto hash (genesis.hash ());
	futurehead::ledger_cache ledger_cache;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger_cache);
	futurehead::account_info info;
	ASSERT_FALSE (store->account_get (transaction, futurehead::genesis_account, info));
	ASSERT_EQ (hash, info.head);
	auto block1 (store->block_get (transaction, info.head));
	ASSERT_NE (nullptr, block1);
	auto receive1 (dynamic_cast<futurehead::open_block *> (block1.get ()));
	ASSERT_NE (nullptr, receive1);
	ASSERT_LE (info.modified, futurehead::seconds_since_epoch ());
	ASSERT_EQ (info.block_count, 1);
	// Genesis block should be confirmed by default
	futurehead::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height_get (transaction, futurehead::genesis_account, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 1);
	ASSERT_EQ (confirmation_height_info.frontier, hash);
	auto test_pub_text (futurehead::test_genesis_key.pub.to_string ());
	auto test_pub_account (futurehead::test_genesis_key.pub.to_account ());
	auto test_prv_text (futurehead::test_genesis_key.prv.data.to_string ());
	ASSERT_EQ (futurehead::genesis_account, futurehead::test_genesis_key.pub);
}

TEST (bootstrap, simple)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto block1 (std::make_shared<futurehead::send_block> (0, 1, 2, futurehead::keypair ().prv, 4, 5));
	auto transaction (store->tx_begin_write ());
	auto block2 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store->unchecked_put (transaction, block1->previous (), block1);
	auto block3 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_FALSE (block3.empty ());
	ASSERT_EQ (*block1, *(block3[0].block));
	store->unchecked_del (transaction, futurehead::unchecked_key (block1->previous (), block1->hash ()));
	auto block4 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block4.empty ());
}

TEST (unchecked, multiple)
{
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store.init_error ());
	auto block1 (std::make_shared<futurehead::send_block> (4, 1, 2, futurehead::keypair ().prv, 4, 5));
	auto transaction (store.tx_begin_write ());
	auto block2 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store.unchecked_put (transaction, block1->previous (), block1);
	store.unchecked_put (transaction, block1->source (), block1);
	auto block3 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_FALSE (block3.empty ());
	auto block4 (store.unchecked_get (transaction, block1->source ()));
	ASSERT_FALSE (block4.empty ());
}

TEST (unchecked, double_put)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto block1 (std::make_shared<futurehead::send_block> (4, 1, 2, futurehead::keypair ().prv, 4, 5));
	auto transaction (store->tx_begin_write ());
	auto block2 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store->unchecked_put (transaction, block1->previous (), block1);
	store->unchecked_put (transaction, block1->previous (), block1);
	auto block3 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_EQ (block3.size (), 1);
}

TEST (unchecked, multiple_get)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto block1 (std::make_shared<futurehead::send_block> (4, 1, 2, futurehead::keypair ().prv, 4, 5));
	auto block2 (std::make_shared<futurehead::send_block> (3, 1, 2, futurehead::keypair ().prv, 4, 5));
	auto block3 (std::make_shared<futurehead::send_block> (5, 1, 2, futurehead::keypair ().prv, 4, 5));
	{
		auto transaction (store->tx_begin_write ());
		store->unchecked_put (transaction, block1->previous (), block1); // unchecked1
		store->unchecked_put (transaction, block1->hash (), block1); // unchecked2
		store->unchecked_put (transaction, block2->previous (), block2); // unchecked3
		store->unchecked_put (transaction, block1->previous (), block2); // unchecked1
		store->unchecked_put (transaction, block1->hash (), block2); // unchecked2
		store->unchecked_put (transaction, block3->previous (), block3);
		store->unchecked_put (transaction, block3->hash (), block3); // unchecked4
		store->unchecked_put (transaction, block1->previous (), block3); // unchecked1
	}
	auto transaction (store->tx_begin_read ());
	auto unchecked_count (store->unchecked_count (transaction));
	ASSERT_EQ (unchecked_count, 8);
	std::vector<futurehead::block_hash> unchecked1;
	auto unchecked1_blocks (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_EQ (unchecked1_blocks.size (), 3);
	for (auto & i : unchecked1_blocks)
	{
		unchecked1.push_back (i.block->hash ());
	}
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block1->hash ()) != unchecked1.end ());
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block2->hash ()) != unchecked1.end ());
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block3->hash ()) != unchecked1.end ());
	std::vector<futurehead::block_hash> unchecked2;
	auto unchecked2_blocks (store->unchecked_get (transaction, block1->hash ()));
	ASSERT_EQ (unchecked2_blocks.size (), 2);
	for (auto & i : unchecked2_blocks)
	{
		unchecked2.push_back (i.block->hash ());
	}
	ASSERT_TRUE (std::find (unchecked2.begin (), unchecked2.end (), block1->hash ()) != unchecked2.end ());
	ASSERT_TRUE (std::find (unchecked2.begin (), unchecked2.end (), block2->hash ()) != unchecked2.end ());
	auto unchecked3 (store->unchecked_get (transaction, block2->previous ()));
	ASSERT_EQ (unchecked3.size (), 1);
	ASSERT_EQ (unchecked3[0].block->hash (), block2->hash ());
	auto unchecked4 (store->unchecked_get (transaction, block3->hash ()));
	ASSERT_EQ (unchecked4.size (), 1);
	ASSERT_EQ (unchecked4[0].block->hash (), block3->hash ());
	auto unchecked5 (store->unchecked_get (transaction, block2->hash ()));
	ASSERT_EQ (unchecked5.size (), 0);
}

TEST (block_store, empty_accounts)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_read ());
	auto begin (store->latest_begin (transaction));
	auto end (store->latest_end ());
	ASSERT_EQ (end, begin);
}

TEST (block_store, one_block)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::open_block block1 (0, 1, 0, futurehead::keypair ().prv, 0, 0);
	block1.sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block_put (transaction, block1.hash (), block1);
	ASSERT_TRUE (store->block_exists (transaction, block1.hash ()));
}

TEST (block_store, empty_bootstrap)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_read ());
	auto begin (store->unchecked_begin (transaction));
	auto end (store->unchecked_end ());
	ASSERT_EQ (end, begin);
}

TEST (block_store, one_bootstrap)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto block1 (std::make_shared<futurehead::send_block> (0, 1, 2, futurehead::keypair ().prv, 4, 5));
	auto transaction (store->tx_begin_write ());
	store->unchecked_put (transaction, block1->hash (), block1);
	store->flush (transaction);
	auto begin (store->unchecked_begin (transaction));
	auto end (store->unchecked_end ());
	ASSERT_NE (end, begin);
	auto hash1 (begin->first.key ());
	ASSERT_EQ (block1->hash (), hash1);
	auto blocks (store->unchecked_get (transaction, hash1));
	ASSERT_EQ (1, blocks.size ());
	auto block2 (blocks[0].block);
	ASSERT_EQ (*block1, *block2);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, unchecked_begin_search)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::keypair key0;
	futurehead::send_block block1 (0, 1, 2, key0.prv, key0.pub, 3);
	futurehead::send_block block2 (5, 6, 7, key0.prv, key0.pub, 8);
}

TEST (block_store, frontier_retrieval)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::account account1 (0);
	futurehead::account_info info1 (0, 0, 0, 0, 0, 0, futurehead::epoch::epoch_0);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, account1, { 0, futurehead::block_hash (0) });
	store->account_put (transaction, account1, info1);
	futurehead::account_info info2;
	store->account_get (transaction, account1, info2);
	ASSERT_EQ (info1, info2);
}

TEST (block_store, one_account)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::account account (0);
	futurehead::block_hash hash (0);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, account, { 20, futurehead::block_hash (15) });
	store->account_put (transaction, account, { hash, account, hash, 42, 100, 200, futurehead::epoch::epoch_0 });
	auto begin (store->latest_begin (transaction));
	auto end (store->latest_end ());
	ASSERT_NE (end, begin);
	ASSERT_EQ (account, futurehead::account (begin->first));
	futurehead::account_info info (begin->second);
	ASSERT_EQ (hash, info.head);
	ASSERT_EQ (42, info.balance.number ());
	ASSERT_EQ (100, info.modified);
	ASSERT_EQ (200, info.block_count);
	futurehead::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height_get (transaction, account, confirmation_height_info));
	ASSERT_EQ (20, confirmation_height_info.height);
	ASSERT_EQ (futurehead::block_hash (15), confirmation_height_info.frontier);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, two_block)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::open_block block1 (0, 1, 1, futurehead::keypair ().prv, 0, 0);
	block1.sideband_set ({});
	block1.hashables.account = 1;
	std::vector<futurehead::block_hash> hashes;
	std::vector<futurehead::open_block> blocks;
	hashes.push_back (block1.hash ());
	blocks.push_back (block1);
	auto transaction (store->tx_begin_write ());
	store->block_put (transaction, hashes[0], block1);
	futurehead::open_block block2 (0, 1, 2, futurehead::keypair ().prv, 0, 0);
	block2.sideband_set ({});
	hashes.push_back (block2.hash ());
	blocks.push_back (block2);
	store->block_put (transaction, hashes[1], block2);
	ASSERT_TRUE (store->block_exists (transaction, block1.hash ()));
	ASSERT_TRUE (store->block_exists (transaction, block2.hash ()));
}

TEST (block_store, two_account)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::account account1 (1);
	futurehead::block_hash hash1 (2);
	futurehead::account account2 (3);
	futurehead::block_hash hash2 (4);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, account1, { 20, futurehead::block_hash (10) });
	store->account_put (transaction, account1, { hash1, account1, hash1, 42, 100, 300, futurehead::epoch::epoch_0 });
	store->confirmation_height_put (transaction, account2, { 30, futurehead::block_hash (20) });
	store->account_put (transaction, account2, { hash2, account2, hash2, 84, 200, 400, futurehead::epoch::epoch_0 });
	auto begin (store->latest_begin (transaction));
	auto end (store->latest_end ());
	ASSERT_NE (end, begin);
	ASSERT_EQ (account1, futurehead::account (begin->first));
	futurehead::account_info info1 (begin->second);
	ASSERT_EQ (hash1, info1.head);
	ASSERT_EQ (42, info1.balance.number ());
	ASSERT_EQ (100, info1.modified);
	ASSERT_EQ (300, info1.block_count);
	futurehead::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height_get (transaction, account1, confirmation_height_info));
	ASSERT_EQ (20, confirmation_height_info.height);
	ASSERT_EQ (futurehead::block_hash (10), confirmation_height_info.frontier);
	++begin;
	ASSERT_NE (end, begin);
	ASSERT_EQ (account2, futurehead::account (begin->first));
	futurehead::account_info info2 (begin->second);
	ASSERT_EQ (hash2, info2.head);
	ASSERT_EQ (84, info2.balance.number ());
	ASSERT_EQ (200, info2.modified);
	ASSERT_EQ (400, info2.block_count);
	ASSERT_FALSE (store->confirmation_height_get (transaction, account2, confirmation_height_info));
	ASSERT_EQ (30, confirmation_height_info.height);
	ASSERT_EQ (futurehead::block_hash (20), confirmation_height_info.frontier);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, latest_find)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::account account1 (1);
	futurehead::block_hash hash1 (2);
	futurehead::account account2 (3);
	futurehead::block_hash hash2 (4);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, account1, { 0, futurehead::block_hash (0) });
	store->account_put (transaction, account1, { hash1, account1, hash1, 100, 0, 300, futurehead::epoch::epoch_0 });
	store->confirmation_height_put (transaction, account2, { 0, futurehead::block_hash (0) });
	store->account_put (transaction, account2, { hash2, account2, hash2, 200, 0, 400, futurehead::epoch::epoch_0 });
	auto first (store->latest_begin (transaction));
	auto second (store->latest_begin (transaction));
	++second;
	auto find1 (store->latest_begin (transaction, 1));
	ASSERT_EQ (first, find1);
	auto find2 (store->latest_begin (transaction, 3));
	ASSERT_EQ (second, find2);
	auto find3 (store->latest_begin (transaction, 2));
	ASSERT_EQ (second, find3);
}

TEST (mdb_block_store, bad_path)
{
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, boost::filesystem::path ("///"));
	ASSERT_TRUE (store.init_error ());
}

TEST (block_store, DISABLED_already_open) // File can be shared
{
	auto path (futurehead::unique_path ());
	boost::filesystem::create_directories (path.parent_path ());
	futurehead::set_secure_perm_directory (path.parent_path ());
	std::ofstream file;
	file.open (path.string ().c_str ());
	ASSERT_TRUE (file.is_open ());
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, path);
	ASSERT_TRUE (store->init_error ());
}

TEST (block_store, roots)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::send_block send_block (0, 1, 2, futurehead::keypair ().prv, 4, 5);
	ASSERT_EQ (send_block.hashables.previous, send_block.root ());
	futurehead::change_block change_block (0, 1, futurehead::keypair ().prv, 3, 4);
	ASSERT_EQ (change_block.hashables.previous, change_block.root ());
	futurehead::receive_block receive_block (0, 1, futurehead::keypair ().prv, 3, 4);
	ASSERT_EQ (receive_block.hashables.previous, receive_block.root ());
	futurehead::open_block open_block (0, 1, 2, futurehead::keypair ().prv, 4, 5);
	ASSERT_EQ (open_block.hashables.account, open_block.root ());
}

TEST (block_store, pending_exists)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::pending_key two (2, 0);
	futurehead::pending_info pending;
	auto transaction (store->tx_begin_write ());
	store->pending_put (transaction, two, pending);
	futurehead::pending_key one (1, 0);
	ASSERT_FALSE (store->pending_exists (transaction, one));
}

TEST (block_store, latest_exists)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::account two (2);
	futurehead::account_info info;
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, two, { 0, futurehead::block_hash (0) });
	store->account_put (transaction, two, info);
	futurehead::account one (1);
	ASSERT_FALSE (store->account_exists (transaction, one));
}

TEST (block_store, large_iteration)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	std::unordered_set<futurehead::account> accounts1;
	for (auto i (0); i < 1000; ++i)
	{
		auto transaction (store->tx_begin_write ());
		futurehead::account account;
		futurehead::random_pool::generate_block (account.bytes.data (), account.bytes.size ());
		accounts1.insert (account);
		store->confirmation_height_put (transaction, account, { 0, futurehead::block_hash (0) });
		store->account_put (transaction, account, futurehead::account_info ());
	}
	std::unordered_set<futurehead::account> accounts2;
	futurehead::account previous (0);
	auto transaction (store->tx_begin_read ());
	for (auto i (store->latest_begin (transaction, 0)), n (store->latest_end ()); i != n; ++i)
	{
		futurehead::account current (i->first);
		ASSERT_GT (current.number (), previous.number ());
		accounts2.insert (current);
		previous = current;
	}
	ASSERT_EQ (accounts1, accounts2);
}

TEST (block_store, frontier)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	futurehead::block_hash hash (100);
	futurehead::account account (200);
	ASSERT_TRUE (store->frontier_get (transaction, hash).is_zero ());
	store->frontier_put (transaction, hash, account);
	ASSERT_EQ (account, store->frontier_get (transaction, hash));
	store->frontier_del (transaction, hash);
	ASSERT_TRUE (store->frontier_get (transaction, hash).is_zero ());
}

TEST (block_store, block_replace)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::send_block send1 (0, 0, 0, futurehead::keypair ().prv, 0, 1);
	send1.sideband_set ({});
	futurehead::send_block send2 (0, 0, 0, futurehead::keypair ().prv, 0, 2);
	send2.sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block_put (transaction, 0, send1);
	store->block_put (transaction, 0, send2);
	auto block3 (store->block_get (transaction, 0));
	ASSERT_NE (nullptr, block3);
	ASSERT_EQ (2, block3->block_work ());
}

TEST (block_store, block_count)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->block_count (transaction).sum ());
		futurehead::open_block block (0, 1, 0, futurehead::keypair ().prv, 0, 0);
		block.sideband_set ({});
		auto hash1 (block.hash ());
		store->block_put (transaction, hash1, block);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (1, store->block_count (transaction).sum ());
}

TEST (block_store, account_count)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->account_count (transaction));
		futurehead::account account (200);
		store->confirmation_height_put (transaction, account, { 0, futurehead::block_hash (0) });
		store->account_put (transaction, account, futurehead::account_info ());
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (1, store->account_count (transaction));
}

TEST (block_store, cemented_count_cache)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	futurehead::genesis genesis;
	futurehead::ledger_cache ledger_cache;
	store->initialize (transaction, genesis, ledger_cache);
	ASSERT_EQ (1, ledger_cache.cemented_count);
}

TEST (block_store, sequence_increment)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::keypair key1;
	futurehead::keypair key2;
	auto block1 (std::make_shared<futurehead::open_block> (0, 1, 0, futurehead::keypair ().prv, 0, 0));
	auto transaction (store->tx_begin_write ());
	auto vote1 (store->vote_generate (transaction, key1.pub, key1.prv, block1));
	ASSERT_EQ (1, vote1->sequence);
	auto vote2 (store->vote_generate (transaction, key1.pub, key1.prv, block1));
	ASSERT_EQ (2, vote2->sequence);
	auto vote3 (store->vote_generate (transaction, key2.pub, key2.prv, block1));
	ASSERT_EQ (1, vote3->sequence);
	auto vote4 (store->vote_generate (transaction, key2.pub, key2.prv, block1));
	ASSERT_EQ (2, vote4->sequence);
	vote1->sequence = 20;
	auto seq5 (store->vote_max (transaction, vote1));
	ASSERT_EQ (20, seq5->sequence);
	vote3->sequence = 30;
	auto seq6 (store->vote_max (transaction, vote3));
	ASSERT_EQ (30, seq6->sequence);
	auto vote5 (store->vote_generate (transaction, key1.pub, key1.prv, block1));
	ASSERT_EQ (21, vote5->sequence);
	auto vote6 (store->vote_generate (transaction, key2.pub, key2.prv, block1));
	ASSERT_EQ (31, vote6->sequence);
}

TEST (mdb_block_store, upgrade_v2_v3)
{
	futurehead::keypair key1;
	futurehead::keypair key2;
	futurehead::block_hash change_hash;
	auto path (futurehead::unique_path ());
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		ASSERT_TRUE (!store.init_error ());
		auto transaction (store.tx_begin_write ());
		futurehead::genesis genesis;
		auto hash (genesis.hash ());
		futurehead::stat stats;
		futurehead::ledger ledger (store, stats);
		store.initialize (transaction, genesis, ledger.cache);
		futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
		futurehead::change_block change (hash, key1.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (hash));
		change_hash = change.hash ();
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, change).code);
		ASSERT_EQ (0, ledger.weight (futurehead::test_genesis_key.pub));
		ASSERT_EQ (futurehead::genesis_amount, ledger.weight (key1.pub));
		store.version_put (transaction, 2);
		ledger.cache.rep_weights.representation_put (key1.pub, 7);
		ASSERT_EQ (7, ledger.weight (key1.pub));
		ASSERT_EQ (2, store.version_get (transaction));
		ledger.cache.rep_weights.representation_put (key2.pub, 6);
		ASSERT_EQ (6, ledger.weight (key2.pub));
		futurehead::account_info info;
		ASSERT_FALSE (store.account_get (transaction, futurehead::test_genesis_key.pub, info));
		auto rep_block = ledger.representative (transaction, ledger.latest (transaction, futurehead::test_genesis_key.pub));
		futurehead::account_info_v5 info_old (info.head, rep_block, info.open_block, info.balance, info.modified);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, futurehead::mdb_val (futurehead::test_genesis_key.pub), futurehead::mdb_val (sizeof (info_old), &info_old), 0));
		ASSERT_EQ (status, 0);
		store.confirmation_height_del (transaction, futurehead::genesis_account);
	}
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path);
	futurehead::stat stats;
	futurehead::ledger ledger (store, stats);
	auto transaction (store.tx_begin_write ());
	ASSERT_TRUE (!store.init_error ());
	ASSERT_LT (2, store.version_get (transaction));
	ASSERT_EQ (futurehead::genesis_amount, ledger.weight (key1.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	futurehead::account_info info;
	ASSERT_FALSE (store.account_get (transaction, futurehead::test_genesis_key.pub, info));
	ASSERT_EQ (change_hash, ledger.representative (transaction, ledger.latest (transaction, futurehead::test_genesis_key.pub)));
}

TEST (mdb_block_store, upgrade_v3_v4)
{
	futurehead::keypair key1;
	futurehead::keypair key2;
	futurehead::keypair key3;
	auto path (futurehead::unique_path ());
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 3);
		futurehead::pending_info_v3 info (key1.pub, 100, key2.pub);
		auto status (mdb_put (store.env.tx (transaction), store.pending_v0, futurehead::mdb_val (key3.pub), futurehead::mdb_val (sizeof (info), &info), 0));
		ASSERT_EQ (0, status);
	}
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path);
	futurehead::stat stats;
	futurehead::ledger ledger (store, stats);
	auto transaction (store.tx_begin_write ());
	ASSERT_FALSE (store.init_error ());
	ASSERT_LT (3, store.version_get (transaction));
	futurehead::pending_key key (key2.pub, reinterpret_cast<futurehead::block_hash const &> (key3.pub));
	futurehead::pending_info info;
	auto error (store.pending_get (transaction, key, info));
	ASSERT_FALSE (error);
	ASSERT_EQ (key1.pub, info.source);
	ASSERT_EQ (futurehead::amount (100), info.amount);
	ASSERT_EQ (futurehead::epoch::epoch_0, info.epoch);
}

TEST (mdb_block_store, upgrade_v4_v5)
{
	futurehead::block_hash genesis_hash (0);
	futurehead::block_hash hash (0);
	auto path (futurehead::unique_path ());
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_write ());
		futurehead::genesis genesis;
		futurehead::stat stats;
		futurehead::ledger ledger (store, stats);
		store.initialize (transaction, genesis, ledger.cache);
		store.version_put (transaction, 4);
		futurehead::account_info info;
		ASSERT_FALSE (store.account_get (transaction, futurehead::test_genesis_key.pub, info));
		futurehead::keypair key0;
		futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
		futurehead::send_block block0 (info.head, key0.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (info.head));
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block0).code);
		hash = block0.hash ();
		auto original (store.block_get (transaction, info.head));
		genesis_hash = info.head;
		store.block_successor_clear (transaction, info.head);
		ASSERT_TRUE (store.block_successor (transaction, genesis_hash).is_zero ());
		modify_genesis_account_info_to_v5 (store, transaction);
		// The pending send needs to be the correct version
		auto status (mdb_put (store.env.tx (transaction), store.pending_v0, futurehead::mdb_val (futurehead::pending_key (key0.pub, block0.hash ())), futurehead::mdb_val (futurehead::pending_info_v14 (futurehead::genesis_account, futurehead::Gxrb_ratio, futurehead::epoch::epoch_0)), 0));
		ASSERT_EQ (status, MDB_SUCCESS);
		store.confirmation_height_del (transaction, futurehead::genesis_account);
	}
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_EQ (hash, store.block_successor (transaction, genesis_hash));
}

TEST (block_store, block_random)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	futurehead::genesis genesis;
	{
		futurehead::ledger_cache ledger_cache;
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, genesis, ledger_cache);
	}
	auto transaction (store->tx_begin_read ());
	auto block (store->block_random (transaction));
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (*block, *genesis.open);
}

TEST (mdb_block_store, upgrade_v5_v6)
{
	auto path (futurehead::unique_path ());
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_write ());
		futurehead::genesis genesis;
		futurehead::ledger_cache ledger_cache;
		store.initialize (transaction, genesis, ledger_cache);
		store.version_put (transaction, 5);
		modify_genesis_account_info_to_v5 (store, transaction);
		store.confirmation_height_del (transaction, futurehead::genesis_account);
	}
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	futurehead::account_info info;
	store.account_get (transaction, futurehead::test_genesis_key.pub, info);
	ASSERT_EQ (1, info.block_count);
}

TEST (mdb_block_store, upgrade_v6_v7)
{
	auto path (futurehead::unique_path ());
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_write ());
		futurehead::genesis genesis;
		futurehead::ledger_cache ledger_cache;
		store.initialize (transaction, genesis, ledger_cache);
		store.version_put (transaction, 6);
		modify_account_info_to_v13 (store, transaction, futurehead::genesis_account, futurehead::genesis_hash);
		auto send1 (std::make_shared<futurehead::send_block> (0, 0, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
		store.unchecked_put (transaction, send1->hash (), send1);
		store.flush (transaction);
		ASSERT_NE (store.unchecked_end (), store.unchecked_begin (transaction));
		store.confirmation_height_del (transaction, futurehead::genesis_account);
	}
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_EQ (store.unchecked_end (), store.unchecked_begin (transaction));
}

// Databases need to be dropped in order to convert to dupsort compatible
TEST (block_store, DISABLED_change_dupsort) // Unchecked is no longer dupsort table
{
	auto path (futurehead::unique_path ());
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path);
	auto transaction (store.tx_begin_write ());
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 1));
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE, &store.unchecked));
	auto send1 (std::make_shared<futurehead::send_block> (0, 0, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	auto send2 (std::make_shared<futurehead::send_block> (1, 0, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	ASSERT_NE (send1->hash (), send2->hash ());
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 0));
	mdb_dbi_close (store.env, store.unchecked);
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE | MDB_DUPSORT, &store.unchecked));
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 1));
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE | MDB_DUPSORT, &store.unchecked));
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_NE (store.unchecked_end (), iterator1);
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
}

TEST (mdb_block_store, upgrade_v7_v8)
{
	auto path (futurehead::unique_path ());
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		auto transaction (store.tx_begin_write ());
		ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 1));
		ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE, &store.unchecked));
		store.version_put (transaction, 7);
	}
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_write ());
	auto send1 (std::make_shared<futurehead::send_block> (0, 0, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	auto send2 (std::make_shared<futurehead::send_block> (1, 0, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_NE (store.unchecked_end (), iterator1);
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
}

TEST (block_store, sequence_flush)
{
	auto path (futurehead::unique_path ());
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, path);
	ASSERT_FALSE (store->init_error ());
	auto transaction (store->tx_begin_write ());
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::send_block> (0, 0, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	auto vote1 (store->vote_generate (transaction, key1.pub, key1.prv, send1));
	auto seq2 (store->vote_get (transaction, vote1->account));
	ASSERT_EQ (nullptr, seq2);
	store->flush (transaction);
	auto seq3 (store->vote_get (transaction, vote1->account));
	ASSERT_EQ (*seq3, *vote1);
}

TEST (block_store, sequence_flush_by_hash)
{
	auto path (futurehead::unique_path ());
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, path);
	ASSERT_FALSE (store->init_error ());
	auto transaction (store->tx_begin_write ());
	futurehead::keypair key1;
	std::vector<futurehead::block_hash> blocks1;
	blocks1.push_back (futurehead::genesis_hash);
	blocks1.push_back (1234);
	blocks1.push_back (5678);
	auto vote1 (store->vote_generate (transaction, key1.pub, key1.prv, blocks1));
	auto seq2 (store->vote_get (transaction, vote1->account));
	ASSERT_EQ (nullptr, seq2);
	store->flush (transaction);
	auto seq3 (store->vote_get (transaction, vote1->account));
	ASSERT_EQ (*seq3, *vote1);
}

// Upgrading tracking block sequence numbers to whole vote.
TEST (mdb_block_store, upgrade_v8_v9)
{
	auto path (futurehead::unique_path ());
	futurehead::keypair key;
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		auto transaction (store.tx_begin_write ());
		ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.vote, 1));
		ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "sequence", MDB_CREATE, &store.vote));
		uint64_t sequence (10);
		ASSERT_EQ (0, mdb_put (store.env.tx (transaction), store.vote, futurehead::mdb_val (key.pub), futurehead::mdb_val (sizeof (sequence), &sequence), 0));
		store.version_put (transaction, 8);
	}
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_LT (8, store.version_get (transaction));
	auto vote (store.vote_get (transaction, key.pub));
	ASSERT_NE (nullptr, vote);
	ASSERT_EQ (10, vote->sequence);
}

TEST (block_store, state_block)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	futurehead::genesis genesis;
	futurehead::keypair key1;
	futurehead::state_block block1 (1, genesis.hash (), 3, 4, 6, key1.prv, key1.pub, 7);
	block1.sideband_set ({});
	{
		futurehead::ledger_cache ledger_cache;
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, genesis, ledger_cache);
		ASSERT_EQ (futurehead::block_type::state, block1.type ());
		store->block_put (transaction, block1.hash (), block1);
		ASSERT_TRUE (store->block_exists (transaction, block1.hash ()));
		auto block2 (store->block_get (transaction, block1.hash ()));
		ASSERT_NE (nullptr, block2);
		ASSERT_EQ (block1, *block2);
	}
	{
		auto transaction (store->tx_begin_write ());
		auto count (store->block_count (transaction));
		ASSERT_EQ (1, count.state);
		store->block_del (transaction, block1.hash (), block1.type ());
		ASSERT_FALSE (store->block_exists (transaction, block1.hash ()));
	}
	auto transaction (store->tx_begin_read ());
	auto count2 (store->block_count (transaction));
	ASSERT_EQ (0, count2.state);
}

TEST (mdb_block_store, upgrade_sideband_genesis)
{
	futurehead::genesis genesis;
	auto path (futurehead::unique_path ());
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 11);
		futurehead::ledger_cache ledger_cache;
		store.initialize (transaction, genesis, ledger_cache);
		modify_account_info_to_v13 (store, transaction, futurehead::genesis_account, futurehead::genesis_hash);
		auto genesis_block (store.block_get (transaction, genesis.hash ()));
		ASSERT_NE (nullptr, genesis_block);
		ASSERT_EQ (1, genesis_block->sideband ().height);
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1));
		write_sideband_v12 (store, transaction, *genesis_block, 0, store.open_blocks);
		futurehead::block_sideband_v14 sideband1;
		auto genesis_block2 (store.block_get_v14 (transaction, genesis.hash (), &sideband1));
		ASSERT_NE (nullptr, genesis_block);
		ASSERT_EQ (0, sideband1.height);
		store.confirmation_height_del (transaction, futurehead::genesis_account);
	}
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_TRUE (store.full_sideband (transaction));
	auto genesis_block (store.block_get (transaction, genesis.hash ()));
	ASSERT_NE (nullptr, genesis_block);
	ASSERT_EQ (1, genesis_block->sideband ().height);
}

TEST (mdb_block_store, upgrade_sideband_two_blocks)
{
	futurehead::genesis genesis;
	futurehead::block_hash hash2;
	auto path (futurehead::unique_path ());
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		ASSERT_FALSE (store.init_error ());
		futurehead::stat stat;
		futurehead::ledger ledger (store, stat);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 11);
		store.initialize (transaction, genesis, ledger.cache);
		futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
		futurehead::state_block block (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
		hash2 = block.hash ();
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block).code);
		store.block_del (transaction, hash2, block.type ());
		mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1);
		mdb_dbi_open (store.env.tx (transaction), "state", MDB_CREATE, &store.state_blocks_v0);
		write_sideband_v12 (store, transaction, *genesis.open, hash2, store.open_blocks);
		write_sideband_v12 (store, transaction, block, 0, store.state_blocks_v0);
		modify_account_info_to_v13 (store, transaction, futurehead::genesis_account, hash2);
		auto status (mdb_put (store.env.tx (transaction), store.pending_v0, futurehead::mdb_val (futurehead::pending_key (futurehead::test_genesis_key.pub, block.hash ())), futurehead::mdb_val (futurehead::pending_info_v14 (futurehead::genesis_account, futurehead::Gxrb_ratio, futurehead::epoch::epoch_0)), 0));
		ASSERT_EQ (status, MDB_SUCCESS);
		store.confirmation_height_del (transaction, futurehead::genesis_account);
	}
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_TRUE (store.full_sideband (transaction));
	auto genesis_block (store.block_get (transaction, genesis.hash ()));
	ASSERT_NE (nullptr, genesis_block);
	ASSERT_EQ (1, genesis_block->sideband ().height);
	auto block2 (store.block_get (transaction, hash2));
	ASSERT_NE (nullptr, block2);
	ASSERT_EQ (2, block2->sideband ().height);
}

TEST (mdb_block_store, upgrade_sideband_two_accounts)
{
	futurehead::genesis genesis;
	futurehead::block_hash hash2;
	futurehead::block_hash hash3;
	futurehead::keypair key;
	auto path (futurehead::unique_path ());
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		futurehead::stat stat;
		futurehead::ledger ledger (store, stat);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 11);
		store.initialize (transaction, genesis, ledger.cache);
		futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
		futurehead::state_block block1 (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
		hash2 = block1.hash ();
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block1).code);
		futurehead::state_block block2 (key.pub, 0, futurehead::test_genesis_key.pub, futurehead::Gxrb_ratio, hash2, key.prv, key.pub, *pool.generate (key.pub));
		hash3 = block2.hash ();
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block2).code);
		store.block_del (transaction, hash2, block1.type ());
		store.block_del (transaction, hash3, block2.type ());
		mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1);
		mdb_dbi_open (store.env.tx (transaction), "state", MDB_CREATE, &store.state_blocks_v0);
		write_sideband_v12 (store, transaction, *genesis.open, hash2, store.open_blocks);
		write_sideband_v12 (store, transaction, block1, 0, store.state_blocks_v0);
		write_sideband_v12 (store, transaction, block2, 0, store.state_blocks_v0);
		modify_account_info_to_v13 (store, transaction, futurehead::genesis_account, hash2);
		modify_account_info_to_v13 (store, transaction, block2.account (), hash3);
		store.confirmation_height_del (transaction, futurehead::genesis_account);
		store.confirmation_height_del (transaction, key.pub);
	}
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_TRUE (store.full_sideband (transaction));
	auto genesis_block (store.block_get (transaction, genesis.hash ()));
	ASSERT_NE (nullptr, genesis_block);
	ASSERT_EQ (1, genesis_block->sideband ().height);
	auto block2 (store.block_get (transaction, hash2));
	ASSERT_NE (nullptr, block2);
	ASSERT_EQ (2, block2->sideband ().height);
	auto block3 (store.block_get (transaction, hash3));
	ASSERT_NE (nullptr, block3);
	ASSERT_EQ (1, block3->sideband ().height);
}

TEST (mdb_block_store, insert_after_legacy)
{
	futurehead::logger_mt logger;
	futurehead::genesis genesis;
	futurehead::mdb_store store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store.init_error ());
	futurehead::stat stat;
	futurehead::ledger ledger (store, stat);
	auto transaction (store.tx_begin_write ());
	store.version_put (transaction, 11);
	store.initialize (transaction, genesis, ledger.cache);
	mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1);
	write_sideband_v12 (store, transaction, *genesis.open, 0, store.open_blocks);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::state_block block (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block).code);
}

// Account for an open block should be retrievable
TEST (mdb_block_store, legacy_account_computed)
{
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store.init_error ());
	futurehead::stat stats;
	futurehead::ledger ledger (store, stats);
	futurehead::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis, ledger.cache);
	store.version_put (transaction, 11);
	mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1);
	write_sideband_v12 (store, transaction, *genesis.open, 0, store.open_blocks);
	ASSERT_EQ (futurehead::genesis_account, ledger.account (transaction, genesis.hash ()));
}

TEST (mdb_block_store, upgrade_sideband_epoch)
{
	bool error (false);
	futurehead::genesis genesis;
	futurehead::block_hash hash2;
	auto path (futurehead::unique_path ());
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		ASSERT_FALSE (error);
		futurehead::stat stat;
		futurehead::ledger ledger (store, stat);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 11);
		store.initialize (transaction, genesis, ledger.cache);
		futurehead::state_block block1 (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount, ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
		hash2 = block1.hash ();
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1));
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block1).code);
		ASSERT_EQ (futurehead::epoch::epoch_1, store.block_version (transaction, hash2));
		store.block_del (transaction, hash2, block1.type ());
		store.block_del (transaction, genesis.open->hash (), genesis.open->type ());
		write_sideband_v12 (store, transaction, *genesis.open, hash2, store.open_blocks);
		write_sideband_v12 (store, transaction, block1, 0, store.state_blocks_v1);

		futurehead::mdb_val value;
		ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.state_blocks_v1, futurehead::mdb_val (hash2), value));
		ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.open_blocks, futurehead::mdb_val (futurehead::genesis_hash), value));

		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "accounts_v1", MDB_CREATE, &store.accounts_v1));
		modify_account_info_to_v13 (store, transaction, futurehead::genesis_account, hash2);
		store.account_del (transaction, futurehead::genesis_account);
		store.confirmation_height_del (transaction, futurehead::genesis_account);
	}
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path);
	futurehead::stat stat;
	futurehead::ledger ledger (store, stat);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_write ());
	ASSERT_TRUE (store.full_sideband (transaction));
	ASSERT_EQ (futurehead::epoch::epoch_1, store.block_version (transaction, hash2));
	auto block1 (store.block_get (transaction, hash2));
	ASSERT_NE (0, block1->sideband ().height);
	futurehead::state_block block2 (futurehead::test_genesis_key.pub, hash2, futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (hash2));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (futurehead::epoch::epoch_1, store.block_version (transaction, block2.hash ()));
}

TEST (mdb_block_store, sideband_height)
{
	futurehead::logger_mt logger;
	futurehead::genesis genesis;
	futurehead::keypair key1;
	futurehead::keypair key2;
	futurehead::keypair key3;
	futurehead::mdb_store store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store.init_error ());
	futurehead::stat stat;
	futurehead::ledger ledger (store, stat);
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis, ledger.cache);
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::send_block send (genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send).code);
	futurehead::receive_block receive (send.hash (), send.hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, receive).code);
	futurehead::change_block change (receive.hash (), 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (receive.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, change).code);
	futurehead::state_block state_send1 (futurehead::test_genesis_key.pub, change.hash (), 0, futurehead::genesis_amount - futurehead::Gxrb_ratio, key1.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (change.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_send1).code);
	futurehead::state_block state_send2 (futurehead::test_genesis_key.pub, state_send1.hash (), 0, futurehead::genesis_amount - 2 * futurehead::Gxrb_ratio, key2.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (state_send1.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_send2).code);
	futurehead::state_block state_send3 (futurehead::test_genesis_key.pub, state_send2.hash (), 0, futurehead::genesis_amount - 3 * futurehead::Gxrb_ratio, key3.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (state_send2.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_send3).code);
	futurehead::state_block state_open (key1.pub, 0, 0, futurehead::Gxrb_ratio, state_send1.hash (), key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_open).code);
	futurehead::state_block epoch (key1.pub, state_open.hash (), 0, futurehead::Gxrb_ratio, ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (state_open.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch).code);
	ASSERT_EQ (futurehead::epoch::epoch_1, store.block_version (transaction, epoch.hash ()));
	futurehead::state_block epoch_open (key2.pub, 0, 0, 0, ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (key2.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch_open).code);
	ASSERT_EQ (futurehead::epoch::epoch_1, store.block_version (transaction, epoch_open.hash ()));
	futurehead::state_block state_receive (key2.pub, epoch_open.hash (), 0, futurehead::Gxrb_ratio, state_send2.hash (), key2.prv, key2.pub, *pool.generate (epoch_open.hash ()));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_receive).code);
	futurehead::open_block open (state_send3.hash (), futurehead::test_genesis_key.pub, key3.pub, key3.prv, key3.pub, *pool.generate (key3.pub));
	ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, open).code);
	auto block1 (store.block_get (transaction, genesis.hash ()));
	ASSERT_EQ (block1->sideband ().height, 1);
	auto block2 (store.block_get (transaction, send.hash ()));
	ASSERT_EQ (block2->sideband ().height, 2);
	auto block3 (store.block_get (transaction, receive.hash ()));
	ASSERT_EQ (block3->sideband ().height, 3);
	auto block4 (store.block_get (transaction, change.hash ()));
	ASSERT_EQ (block4->sideband ().height, 4);
	auto block5 (store.block_get (transaction, state_send1.hash ()));
	ASSERT_EQ (block5->sideband ().height, 5);
	auto block6 (store.block_get (transaction, state_send2.hash ()));
	ASSERT_EQ (block6->sideband ().height, 6);
	auto block7 (store.block_get (transaction, state_send3.hash ()));
	ASSERT_EQ (block7->sideband ().height, 7);
	auto block8 (store.block_get (transaction, state_open.hash ()));
	ASSERT_EQ (block8->sideband ().height, 1);
	auto block9 (store.block_get (transaction, epoch.hash ()));
	ASSERT_EQ (block9->sideband ().height, 2);
	auto block10 (store.block_get (transaction, epoch_open.hash ()));
	ASSERT_EQ (block10->sideband ().height, 1);
	auto block11 (store.block_get (transaction, state_receive.hash ()));
	ASSERT_EQ (block11->sideband ().height, 2);
	auto block12 (store.block_get (transaction, open.hash ()));
	ASSERT_EQ (block12->sideband ().height, 1);
}

TEST (block_store, peers)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());

	futurehead::endpoint_key endpoint (boost::asio::ip::address_v6::any ().to_bytes (), 100);
	{
		auto transaction (store->tx_begin_write ());

		// Confirm that the store is empty
		ASSERT_FALSE (store->peer_exists (transaction, endpoint));
		ASSERT_EQ (store->peer_count (transaction), 0);

		// Add one
		store->peer_put (transaction, endpoint);
		ASSERT_TRUE (store->peer_exists (transaction, endpoint));
	}

	// Confirm that it can be found
	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer_count (transaction), 1);
	}

	// Add another one and check that it (and the existing one) can be found
	futurehead::endpoint_key endpoint1 (boost::asio::ip::address_v6::any ().to_bytes (), 101);
	{
		auto transaction (store->tx_begin_write ());
		store->peer_put (transaction, endpoint1);
		ASSERT_TRUE (store->peer_exists (transaction, endpoint1)); // Check new peer is here
		ASSERT_TRUE (store->peer_exists (transaction, endpoint)); // Check first peer is still here
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer_count (transaction), 2);
	}

	// Delete the first one
	{
		auto transaction (store->tx_begin_write ());
		store->peer_del (transaction, endpoint1);
		ASSERT_FALSE (store->peer_exists (transaction, endpoint1)); // Confirm it no longer exists
		ASSERT_TRUE (store->peer_exists (transaction, endpoint)); // Check first peer is still here
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer_count (transaction), 1);
	}

	// Delete original one
	{
		auto transaction (store->tx_begin_write ());
		store->peer_del (transaction, endpoint);
		ASSERT_FALSE (store->peer_exists (transaction, endpoint));
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer_count (transaction), 0);
	}
}

TEST (block_store, endpoint_key_byte_order)
{
	boost::asio::ip::address_v6 address (boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1"));
	uint16_t port = 100;
	futurehead::endpoint_key endpoint_key (address.to_bytes (), port);

	std::vector<uint8_t> bytes;
	{
		futurehead::vectorstream stream (bytes);
		futurehead::write (stream, endpoint_key);
	}

	// This checks that the endpoint is serialized as expected, with a size
	// of 18 bytes (16 for ipv6 address and 2 for port), both in network byte order.
	ASSERT_EQ (bytes.size (), 18);
	ASSERT_EQ (bytes[10], 0xff);
	ASSERT_EQ (bytes[11], 0xff);
	ASSERT_EQ (bytes[12], 127);
	ASSERT_EQ (bytes[bytes.size () - 2], 0);
	ASSERT_EQ (bytes.back (), 100);

	// Deserialize the same stream bytes
	futurehead::bufferstream stream1 (bytes.data (), bytes.size ());
	futurehead::endpoint_key endpoint_key1;
	futurehead::read (stream1, endpoint_key1);

	// This should be in network bytes order
	ASSERT_EQ (address.to_bytes (), endpoint_key1.address_bytes ());

	// This should be in host byte order
	ASSERT_EQ (port, endpoint_key1.port ());
}

TEST (block_store, online_weight)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_FALSE (store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->online_weight_count (transaction));
		ASSERT_EQ (store->online_weight_end (), store->online_weight_begin (transaction));
		store->online_weight_put (transaction, 1, 2);
	}
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (1, store->online_weight_count (transaction));
		auto item (store->online_weight_begin (transaction));
		ASSERT_NE (store->online_weight_end (), item);
		ASSERT_EQ (1, item->first);
		ASSERT_EQ (2, item->second.number ());
		store->online_weight_del (transaction, 1);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (0, store->online_weight_count (transaction));
	ASSERT_EQ (store->online_weight_end (), store->online_weight_begin (transaction));
}

// Adding confirmation height to accounts
TEST (mdb_block_store, upgrade_v13_v14)
{
	auto path (futurehead::unique_path ());
	futurehead::genesis genesis;
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		auto transaction (store.tx_begin_write ());
		futurehead::ledger_cache ledger_cache;
		store.initialize (transaction, genesis, ledger_cache);
		futurehead::account_info account_info;
		ASSERT_FALSE (store.account_get (transaction, futurehead::genesis_account, account_info));
		store.version_put (transaction, 13);
		modify_account_info_to_v13 (store, transaction, futurehead::genesis_account, futurehead::genesis_hash);

		// This should fail as sizes are no longer correct for account_info_v14
		futurehead::mdb_val value;
		ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.accounts_v0, futurehead::mdb_val (futurehead::genesis_account), value));
		futurehead::account_info_v14 info;
		ASSERT_NE (value.size (), info.db_size ());
		store.confirmation_height_del (transaction, futurehead::genesis_account);
	}

	// Now do the upgrade
	futurehead::logger_mt logger;
	auto error (false);
	futurehead::mdb_store store (logger, path);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());

	// Size of account_info should now equal that set in db
	futurehead::mdb_val value;
	ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.accounts_v0, futurehead::mdb_val (futurehead::genesis_account), value));
	futurehead::account_info info;
	ASSERT_EQ (value.size (), info.db_size ());

	// Confirmation height should exist and be correct
	futurehead::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store.confirmation_height_get (transaction, futurehead::genesis_account, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 1);
	ASSERT_EQ (confirmation_height_info.frontier, genesis.hash ());

	// Test deleting node ID
	futurehead::uint256_union node_id_mdb_key (3);
	auto error_node_id (mdb_get (store.env.tx (transaction), store.meta, futurehead::mdb_val (node_id_mdb_key), value));
	ASSERT_EQ (error_node_id, MDB_NOTFOUND);

	ASSERT_LT (13, store.version_get (transaction));
}

TEST (mdb_block_store, upgrade_v14_v15)
{
	// Extract confirmation height to a separate database
	auto path (futurehead::unique_path ());
	futurehead::genesis genesis;
	futurehead::network_params network_params;
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::send_block send (genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	futurehead::state_block epoch (futurehead::test_genesis_key.pub, send.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, network_params.ledger.epochs.link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send.hash ()));
	futurehead::state_block state_send (futurehead::test_genesis_key.pub, epoch.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio * 2, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (epoch.hash ()));
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		futurehead::stat stats;
		futurehead::ledger ledger (store, stats);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, genesis, ledger.cache);
		futurehead::account_info account_info;
		ASSERT_FALSE (store.account_get (transaction, futurehead::genesis_account, account_info));
		futurehead::confirmation_height_info confirmation_height_info;
		ASSERT_FALSE (store.confirmation_height_get (transaction, futurehead::genesis_account, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, 1);
		ASSERT_EQ (confirmation_height_info.frontier, genesis.hash ());
		// These databases get remove after an upgrade, so readd them
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "accounts_v1", MDB_CREATE, &store.accounts_v1));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "pending_v1", MDB_CREATE, &store.pending_v1));
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send).code);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch).code);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_send).code);
		// Lower the database to the previous version
		store.version_put (transaction, 14);
		store.confirmation_height_del (transaction, futurehead::genesis_account);
		modify_account_info_to_v14 (store, transaction, futurehead::genesis_account, confirmation_height_info.height, state_send.hash ());

		store.pending_del (transaction, futurehead::pending_key (futurehead::genesis_account, state_send.hash ()));

		write_sideband_v14 (store, transaction, state_send, store.state_blocks_v1);
		write_sideband_v14 (store, transaction, epoch, store.state_blocks_v1);

		// Remove from state table
		store.block_del (transaction, state_send.hash (), state_send.type ());
		store.block_del (transaction, epoch.hash (), epoch.type ());

		// Turn pending into v14
		ASSERT_FALSE (mdb_put (store.env.tx (transaction), store.pending_v0, futurehead::mdb_val (futurehead::pending_key (futurehead::test_genesis_key.pub, send.hash ())), futurehead::mdb_val (futurehead::pending_info_v14 (futurehead::genesis_account, futurehead::Gxrb_ratio, futurehead::epoch::epoch_0)), 0));
		ASSERT_FALSE (mdb_put (store.env.tx (transaction), store.pending_v1, futurehead::mdb_val (futurehead::pending_key (futurehead::test_genesis_key.pub, state_send.hash ())), futurehead::mdb_val (futurehead::pending_info_v14 (futurehead::genesis_account, futurehead::Gxrb_ratio, futurehead::epoch::epoch_1)), 0));

		// This should fail as sizes are no longer correct for account_info
		futurehead::mdb_val value;
		ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.accounts_v1, futurehead::mdb_val (futurehead::genesis_account), value));
		futurehead::account_info info;
		ASSERT_NE (value.size (), info.db_size ());
		store.account_del (transaction, futurehead::genesis_account);

		// Confirmation height for the account should be deleted
		ASSERT_TRUE (mdb_get (store.env.tx (transaction), store.confirmation_height, futurehead::mdb_val (futurehead::genesis_account), value));
	}

	// Now do the upgrade
	futurehead::logger_mt logger;
	auto error (false);
	futurehead::mdb_store store (logger, path);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());

	// Size of account_info should now equal that set in db
	futurehead::mdb_val value;
	ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.accounts, futurehead::mdb_val (futurehead::genesis_account), value));
	futurehead::account_info info (value);
	ASSERT_EQ (value.size (), info.db_size ());

	// Confirmation height should exist
	futurehead::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store.confirmation_height_get (transaction, futurehead::genesis_account, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 1);
	ASSERT_EQ (confirmation_height_info.frontier, genesis.hash ());

	// accounts_v1, state_blocks_v1 & pending_v1 tables should be deleted
	auto error_get_accounts_v1 (mdb_get (store.env.tx (transaction), store.accounts_v1, futurehead::mdb_val (futurehead::genesis_account), value));
	ASSERT_NE (error_get_accounts_v1, MDB_SUCCESS);
	auto error_get_pending_v1 (mdb_get (store.env.tx (transaction), store.pending_v1, futurehead::mdb_val (futurehead::pending_key (futurehead::test_genesis_key.pub, state_send.hash ())), value));
	ASSERT_NE (error_get_pending_v1, MDB_SUCCESS);
	auto error_get_state_v1 (mdb_get (store.env.tx (transaction), store.state_blocks_v1, futurehead::mdb_val (state_send.hash ()), value));
	ASSERT_NE (error_get_state_v1, MDB_SUCCESS);

	// Check that the epochs are set correctly for the sideband, accounts and pending entries
	auto block = store.block_get (transaction, state_send.hash ());
	ASSERT_NE (block, nullptr);
	ASSERT_EQ (block->sideband ().details.epoch, futurehead::epoch::epoch_1);
	block = store.block_get (transaction, send.hash ());
	ASSERT_NE (block, nullptr);
	ASSERT_EQ (block->sideband ().details.epoch, futurehead::epoch::epoch_0);
	ASSERT_EQ (info.epoch (), futurehead::epoch::epoch_1);
	futurehead::pending_info pending_info;
	store.pending_get (transaction, futurehead::pending_key (futurehead::test_genesis_key.pub, send.hash ()), pending_info);
	ASSERT_EQ (pending_info.epoch, futurehead::epoch::epoch_0);
	store.pending_get (transaction, futurehead::pending_key (futurehead::test_genesis_key.pub, state_send.hash ()), pending_info);
	ASSERT_EQ (pending_info.epoch, futurehead::epoch::epoch_1);

	// Version should be correct
	ASSERT_LT (14, store.version_get (transaction));
}

TEST (mdb_block_store, upgrade_v15_v16)
{
	auto path (futurehead::unique_path ());
	futurehead::mdb_val value;
	{
		futurehead::genesis genesis;
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		futurehead::stat stats;
		futurehead::ledger ledger (store, stats);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, genesis, ledger.cache);
		// The representation table should get removed after, so readd it so that we can later confirm this actually happens
		auto txn = store.env.tx (transaction);
		ASSERT_FALSE (mdb_dbi_open (txn, "representation", MDB_CREATE, &store.representation));
		auto weight = ledger.cache.rep_weights.representation_get (futurehead::genesis_account);
		ASSERT_EQ (MDB_SUCCESS, mdb_put (txn, store.representation, futurehead::mdb_val (futurehead::genesis_account), futurehead::mdb_val (futurehead::uint128_union (weight)), 0));
		// Lower the database to the previous version
		store.version_put (transaction, 15);
		// Confirm the rep weight exists in the database
		ASSERT_EQ (MDB_SUCCESS, mdb_get (store.env.tx (transaction), store.representation, futurehead::mdb_val (futurehead::genesis_account), value));
		store.confirmation_height_del (transaction, futurehead::genesis_account);
	}

	// Now do the upgrade
	futurehead::logger_mt logger;
	auto error (false);
	futurehead::mdb_store store (logger, path);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());

	// The representation table should now be deleted
	auto error_get_representation (mdb_get (store.env.tx (transaction), store.representation, futurehead::mdb_val (futurehead::genesis_account), value));
	ASSERT_NE (MDB_SUCCESS, error_get_representation);
	ASSERT_EQ (store.representation, 0);

	// Version should be correct
	ASSERT_LT (15, store.version_get (transaction));
}

TEST (mdb_block_store, upgrade_v16_v17)
{
	futurehead::genesis genesis;
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::state_block block1 (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	futurehead::state_block block2 (futurehead::test_genesis_key.pub, block1.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio - 1, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block1.hash ()));
	futurehead::state_block block3 (futurehead::test_genesis_key.pub, block2.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio - 2, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (block2.hash ()));

	auto code = [&block1, &block2, &block3](auto confirmation_height, futurehead::block_hash const & expected_cemented_frontier) {
		auto path (futurehead::unique_path ());
		futurehead::mdb_val value;
		{
			futurehead::genesis genesis;
			futurehead::logger_mt logger;
			futurehead::mdb_store store (logger, path);
			futurehead::stat stats;
			futurehead::ledger ledger (store, stats);
			auto transaction (store.tx_begin_write ());
			store.initialize (transaction, genesis, ledger.cache);
			ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block1).code);
			ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block2).code);
			ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, block3).code);
			modify_confirmation_height_to_v15 (store, transaction, futurehead::genesis_account, confirmation_height);

			// Lower the database to the previous version
			store.version_put (transaction, 16);
		}

		// Now do the upgrade
		futurehead::logger_mt logger;
		auto error (false);
		futurehead::mdb_store store (logger, path);
		ASSERT_FALSE (error);
		auto transaction (store.tx_begin_read ());

		futurehead::confirmation_height_info confirmation_height_info;
		ASSERT_FALSE (store.confirmation_height_get (transaction, futurehead::genesis_account, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, confirmation_height);

		// Check confirmation height frontier is correct
		ASSERT_EQ (confirmation_height_info.frontier, expected_cemented_frontier);

		// Version should be correct
		ASSERT_LT (16, store.version_get (transaction));
	};

	code (0, futurehead::block_hash (0));
	code (1, genesis.hash ());
	code (2, block1.hash ());
	code (3, block2.hash ());
	code (4, block3.hash ());
}

TEST (mdb_block_store, upgrade_v17_v18)
{
	auto path (futurehead::unique_path ());
	futurehead::genesis genesis;
	futurehead::keypair key1;
	futurehead::keypair key2;
	futurehead::keypair key3;
	futurehead::network_params network_params;
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::send_block send_zero (genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ()));
	futurehead::state_block state_receive_zero (futurehead::test_genesis_key.pub, send_zero.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount, send_zero.hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (send_zero.hash ()));
	futurehead::state_block epoch (futurehead::test_genesis_key.pub, state_receive_zero.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount, network_params.ledger.epochs.link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (state_receive_zero.hash ()));
	futurehead::state_block state_send (futurehead::test_genesis_key.pub, epoch.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (epoch.hash ()));
	futurehead::state_block state_receive (futurehead::test_genesis_key.pub, state_send.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount, state_send.hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (state_send.hash ()));
	futurehead::state_block state_change (futurehead::test_genesis_key.pub, state_receive.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (state_receive.hash ()));
	futurehead::state_block state_send_change (futurehead::test_genesis_key.pub, state_change.hash (), key1.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, key1.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (state_change.hash ()));
	futurehead::state_block epoch_first (key1.pub, 0, 0, 0, network_params.ledger.epochs.link (futurehead::epoch::epoch_2), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (key1.pub));
	futurehead::state_block state_receive2 (key1.pub, epoch_first.hash (), key1.pub, futurehead::Gxrb_ratio, state_send_change.hash (), key1.prv, key1.pub, *pool.generate (epoch_first.hash ()));
	futurehead::state_block state_send2 (futurehead::test_genesis_key.pub, state_send_change.hash (), key1.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio * 2, key2.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (state_send_change.hash ()));
	futurehead::state_block state_open (key2.pub, 0, key2.pub, futurehead::Gxrb_ratio, state_send2.hash (), key2.prv, key2.pub, *pool.generate (key2.pub));
	futurehead::state_block state_send_epoch_link (key2.pub, state_open.hash (), key2.pub, 0, network_params.ledger.epochs.link (futurehead::epoch::epoch_2), key2.prv, key2.pub, *pool.generate (state_open.hash ()));
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		auto transaction (store.tx_begin_write ());
		futurehead::stat stats;
		futurehead::ledger ledger (store, stats);
		store.initialize (transaction, genesis, ledger.cache);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, send_zero).code);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_receive_zero).code);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch).code);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_send).code);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_receive).code);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_change).code);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_send_change).code);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, epoch_first).code);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_receive2).code);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_send2).code);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_open).code);
		ASSERT_EQ (futurehead::process_result::progress, ledger.process (transaction, state_send_epoch_link).code);

		// Downgrade the store
		store.version_put (transaction, 17);

		// Replace with the previous sideband version for state blocks
		// The upgrade can resume after upgrading some blocks, test this by only downgrading some of them
		write_sideband_v15 (store, transaction, state_receive_zero);
		write_sideband_v15 (store, transaction, epoch);
		write_sideband_v15 (store, transaction, state_send);
		// DISABLED write_sideband_v15 (store, transaction, state_receive);
		write_sideband_v15 (store, transaction, state_change);
		write_sideband_v15 (store, transaction, state_send_change);
		// DISABLED write_sideband_v15 (store, transaction, epoch_first);
		write_sideband_v15 (store, transaction, state_receive2);
		// DISABLED write_sideband_v15 (store, transaction, state_send2);
		write_sideband_v15 (store, transaction, state_open);
		// DISABLED write_sideband_v15 (store, transaction, state_send_epoch_link);
	}

	// Now do the upgrade
	futurehead::logger_mt logger;
	auto error (false);
	futurehead::mdb_store store (logger, path);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());

	// Size of state block should equal that set in db (no change)
	futurehead::mdb_val value;
	ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.state_blocks, futurehead::mdb_val (state_send.hash ()), value));
	ASSERT_EQ (value.size (), futurehead::state_block::size + futurehead::block_sideband::size (futurehead::block_type::state));

	// Check that sidebands are correctly populated
	{
		// Non-state unaffected
		auto block = store.block_get (transaction, send_zero.hash ());
		ASSERT_NE (block, nullptr);
		// All defaults
		ASSERT_EQ (block->sideband ().details.epoch, futurehead::epoch::epoch_0);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State receive from old zero send
		auto block = store.block_get (transaction, state_receive_zero.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, futurehead::epoch::epoch_0);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_TRUE (block->sideband ().details.is_receive);
	}
	{
		// Epoch
		auto block = store.block_get (transaction, epoch.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, futurehead::epoch::epoch_1);
		ASSERT_TRUE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State send
		auto block = store.block_get (transaction, state_send.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, futurehead::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_TRUE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State receive
		auto block = store.block_get (transaction, state_receive.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, futurehead::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_TRUE (block->sideband ().details.is_receive);
	}
	{
		// State change
		auto block = store.block_get (transaction, state_change.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, futurehead::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State send + change
		auto block = store.block_get (transaction, state_send_change.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, futurehead::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_TRUE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// Epoch on unopened account
		auto block = store.block_get (transaction, epoch_first.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, futurehead::epoch::epoch_2);
		ASSERT_TRUE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State open following epoch
		auto block = store.block_get (transaction, state_receive2.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, futurehead::epoch::epoch_2);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_TRUE (block->sideband ().details.is_receive);
	}
	{
		// Another state send
		auto block = store.block_get (transaction, state_send2.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, futurehead::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_TRUE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State open
		auto block = store.block_get (transaction, state_open.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, futurehead::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_TRUE (block->sideband ().details.is_receive);
	}
	{
		// State send to an epoch link
		auto block = store.block_get (transaction, state_send_epoch_link.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, futurehead::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_TRUE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	// Version should be correct
	ASSERT_LT (17, store.version_get (transaction));
}

TEST (mdb_block_store, upgrade_backup)
{
	auto dir (futurehead::unique_path ());
	namespace fs = boost::filesystem;
	fs::create_directory (dir);
	auto path = dir / "data.ldb";
	/** Returns 'dir' if backup file cannot be found */
	auto get_backup_path = [&dir]() {
		for (fs::directory_iterator itr (dir); itr != fs::directory_iterator (); ++itr)
		{
			if (itr->path ().filename ().string ().find ("data_backup_") != std::string::npos)
			{
				return itr->path ();
			}
		}
		return dir;
	};

	{
		futurehead::logger_mt logger;
		futurehead::genesis genesis;
		futurehead::mdb_store store (logger, path);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 14);
	}
	ASSERT_EQ (get_backup_path ().string (), dir.string ());

	// Now do the upgrade and confirm that backup is saved
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path, futurehead::txn_tracking_config{}, std::chrono::seconds (5), futurehead::lmdb_config{}, 512, true);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_LT (14, store.version_get (transaction));
	ASSERT_NE (get_backup_path ().string (), dir.string ());
}

// Test various confirmation height values as well as clearing them
TEST (block_store, confirmation_height)
{
	auto path (futurehead::unique_path ());
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path);

	futurehead::account account1 (0);
	futurehead::account account2 (1);
	futurehead::account account3 (2);
	futurehead::block_hash cemented_frontier1 (3);
	futurehead::block_hash cemented_frontier2 (4);
	futurehead::block_hash cemented_frontier3 (5);
	{
		auto transaction (store.tx_begin_write ());
		store.confirmation_height_put (transaction, account1, { 500, cemented_frontier1 });
		store.confirmation_height_put (transaction, account2, { std::numeric_limits<uint64_t>::max (), cemented_frontier2 });
		store.confirmation_height_put (transaction, account3, { 10, cemented_frontier3 });

		futurehead::confirmation_height_info confirmation_height_info;
		ASSERT_FALSE (store.confirmation_height_get (transaction, account1, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, 500);
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier1);
		ASSERT_FALSE (store.confirmation_height_get (transaction, account2, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, std::numeric_limits<uint64_t>::max ());
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier2);
		ASSERT_FALSE (store.confirmation_height_get (transaction, account3, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, 10);
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier3);

		// Check cleaning of confirmation heights
		store.confirmation_height_clear (transaction);
	}
	auto transaction (store.tx_begin_read ());
	ASSERT_EQ (store.confirmation_height_count (transaction), 3);
	futurehead::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store.confirmation_height_get (transaction, account1, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 0);
	ASSERT_EQ (confirmation_height_info.frontier, futurehead::block_hash (0));
	ASSERT_FALSE (store.confirmation_height_get (transaction, account2, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 0);
	ASSERT_EQ (confirmation_height_info.frontier, futurehead::block_hash (0));
	ASSERT_FALSE (store.confirmation_height_get (transaction, account3, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 0);
	ASSERT_EQ (confirmation_height_info.frontier, futurehead::block_hash (0));
}

// Upgrade many accounts and check they all have a confirmation height of 0 (except genesis which should have 1)
TEST (mdb_block_store, upgrade_confirmation_height_many)
{
	auto error (false);
	futurehead::genesis genesis;
	auto total_num_accounts = 1000; // Includes the genesis account

	auto path (futurehead::unique_path ());
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, path);
		ASSERT_FALSE (error);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 13);
		futurehead::ledger_cache ledger_cache;
		store.initialize (transaction, genesis, ledger_cache);
		modify_account_info_to_v13 (store, transaction, futurehead::genesis_account, futurehead::genesis_hash);

		// Add many accounts
		for (auto i = 0; i < total_num_accounts - 1; ++i)
		{
			futurehead::account account (i);
			futurehead::open_block open (1, futurehead::genesis_account, 3, nullptr);
			open.sideband_set ({});
			store.block_put (transaction, open.hash (), open);
			futurehead::account_info_v13 account_info_v13 (open.hash (), open.hash (), open.hash (), 3, 4, 1, futurehead::epoch::epoch_0);
			auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, futurehead::mdb_val (account), futurehead::mdb_val (account_info_v13), 0));
			ASSERT_EQ (status, 0);
		}
		store.confirmation_height_del (transaction, futurehead::genesis_account);

		ASSERT_EQ (store.count (transaction, store.accounts_v0), total_num_accounts);
	}

	// Loop over them all and confirm they all have the correct confirmation heights
	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, path);
	auto transaction (store.tx_begin_read ());
	ASSERT_EQ (store.account_count (transaction), total_num_accounts);
	ASSERT_EQ (store.confirmation_height_count (transaction), total_num_accounts);

	for (auto i (store.confirmation_height_begin (transaction)), n (store.confirmation_height_end ()); i != n; ++i)
	{
		ASSERT_EQ (i->second.height, (i->first == futurehead::genesis_account) ? 1 : 0);
		ASSERT_EQ (i->second.frontier, (i->first == futurehead::genesis_account) ? genesis.hash () : futurehead::block_hash (0));
	}
}

// Ledger versions are not forward compatible
TEST (block_store, incompatible_version)
{
	auto path (futurehead::unique_path ());
	futurehead::logger_mt logger;
	{
		auto store = futurehead::make_store (logger, path);
		ASSERT_FALSE (store->init_error ());

		// Put version to an unreachable number so that it should always be incompatible
		auto transaction (store->tx_begin_write ());
		store->version_put (transaction, std::numeric_limits<int>::max ());
	}

	// Now try and read it, should give an error
	{
		auto store = futurehead::make_store (logger, path, true);
		ASSERT_TRUE (store->init_error ());

		auto transaction = store->tx_begin_read ();
		auto version_l = store->version_get (transaction);
		ASSERT_EQ (version_l, std::numeric_limits<int>::max ());
	}
}

TEST (block_store, reset_renew_existing_transaction)
{
	futurehead::logger_mt logger;
	auto store = futurehead::make_store (logger, futurehead::unique_path ());
	ASSERT_TRUE (!store->init_error ());

	futurehead::keypair key1;
	futurehead::open_block block (0, 1, 1, futurehead::keypair ().prv, 0, 0);
	block.sideband_set ({});
	auto hash1 (block.hash ());
	auto read_transaction = store->tx_begin_read ();

	// Block shouldn't exist yet
	auto block_non_existing (store->block_get (read_transaction, hash1));
	ASSERT_EQ (nullptr, block_non_existing);

	// Release resources for the transaction
	read_transaction.reset ();

	// Write the block
	{
		auto write_transaction (store->tx_begin_write ());
		store->block_put (write_transaction, hash1, block);
	}

	read_transaction.renew ();

	// Block should exist now
	auto block_existing (store->block_get (read_transaction, hash1));
	ASSERT_NE (nullptr, block_existing);
}

TEST (block_store, rocksdb_force_test_env_variable)
{
	futurehead::logger_mt logger;

	// Set environment variable
	constexpr auto env_var = "TEST_USE_ROCKSDB";
	auto value = std::getenv (env_var);
	(void)value;

	auto store = futurehead::make_store (logger, futurehead::unique_path ());

	auto mdb_cast = dynamic_cast<futurehead::mdb_store *> (store.get ());

#if FUTUREHEAD_ROCKSDB
	if (value && boost::lexical_cast<int> (value) == 1)
	{
		ASSERT_NE (boost::polymorphic_downcast<futurehead::rocksdb_store *> (store.get ()), nullptr);
	}
	else
	{
		ASSERT_NE (mdb_cast, nullptr);
	}
#else
	ASSERT_NE (mdb_cast, nullptr);
#endif
}

namespace
{
void write_sideband_v12 (futurehead::mdb_store & store_a, futurehead::transaction & transaction_a, futurehead::block & block_a, futurehead::block_hash const & successor_a, MDB_dbi db_a)
{
	std::vector<uint8_t> vector;
	{
		futurehead::vectorstream stream (vector);
		block_a.serialize (stream);
		futurehead::write (stream, successor_a);
	}
	MDB_val val{ vector.size (), vector.data () };
	auto hash (block_a.hash ());
	auto status (mdb_put (store_a.env.tx (transaction_a), db_a, futurehead::mdb_val (hash), &val, 0));
	ASSERT_EQ (0, status);
	futurehead::block_sideband_v14 sideband_v14;
	auto block (store_a.block_get_v14 (transaction_a, hash, &sideband_v14));
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (0, sideband_v14.height);
};

void write_sideband_v14 (futurehead::mdb_store & store_a, futurehead::transaction & transaction_a, futurehead::block const & block_a, MDB_dbi db_a)
{
	auto block = store_a.block_get (transaction_a, block_a.hash ());
	ASSERT_NE (block, nullptr);

	futurehead::block_sideband_v14 sideband_v14 (block->type (), block->sideband ().account, block->sideband ().successor, block->sideband ().balance, block->sideband ().timestamp, block->sideband ().height);
	std::vector<uint8_t> data;
	{
		futurehead::vectorstream stream (data);
		block_a.serialize (stream);
		sideband_v14.serialize (stream);
	}

	MDB_val val{ data.size (), data.data () };
	ASSERT_FALSE (mdb_put (store_a.env.tx (transaction_a), block->sideband ().details.epoch == futurehead::epoch::epoch_0 ? store_a.state_blocks_v0 : store_a.state_blocks_v1, futurehead::mdb_val (block_a.hash ()), &val, 0));
}

void write_sideband_v15 (futurehead::mdb_store & store_a, futurehead::transaction & transaction_a, futurehead::block const & block_a)
{
	auto block = store_a.block_get (transaction_a, block_a.hash ());
	ASSERT_NE (block, nullptr);

	ASSERT_LE (block->sideband ().details.epoch, futurehead::epoch::max);
	// Simulated by writing 0 on every of the most significant bits, leaving out epoch only, as if pre-upgrade
	futurehead::block_sideband sideband_v15 (block->sideband ().account, block->sideband ().successor, block->sideband ().balance, block->sideband ().timestamp, block->sideband ().height, block->sideband ().details.epoch, false, false, false);
	std::vector<uint8_t> data;
	{
		futurehead::vectorstream stream (data);
		block_a.serialize (stream);
		sideband_v15.serialize (stream, block_a.type ());
	}

	MDB_val val{ data.size (), data.data () };
	ASSERT_FALSE (mdb_put (store_a.env.tx (transaction_a), store_a.state_blocks, futurehead::mdb_val (block_a.hash ()), &val, 0));
}

// These functions take the latest account_info and create a legacy one so that upgrade tests can be emulated more easily.
void modify_account_info_to_v13 (futurehead::mdb_store & store, futurehead::transaction const & transaction, futurehead::account const & account, futurehead::block_hash const & rep_block)
{
	futurehead::account_info info;
	ASSERT_FALSE (store.account_get (transaction, account, info));
	futurehead::account_info_v13 account_info_v13 (info.head, rep_block, info.open_block, info.balance, info.modified, info.block_count, info.epoch ());
	auto status (mdb_put (store.env.tx (transaction), (info.epoch () == futurehead::epoch::epoch_0) ? store.accounts_v0 : store.accounts_v1, futurehead::mdb_val (account), futurehead::mdb_val (account_info_v13), 0));
	ASSERT_EQ (status, 0);
}

void modify_account_info_to_v14 (futurehead::mdb_store & store, futurehead::transaction const & transaction, futurehead::account const & account, uint64_t confirmation_height, futurehead::block_hash const & rep_block)
{
	futurehead::account_info info;
	ASSERT_FALSE (store.account_get (transaction, account, info));
	futurehead::account_info_v14 account_info_v14 (info.head, rep_block, info.open_block, info.balance, info.modified, info.block_count, confirmation_height, info.epoch ());
	auto status (mdb_put (store.env.tx (transaction), info.epoch () == futurehead::epoch::epoch_0 ? store.accounts_v0 : store.accounts_v1, futurehead::mdb_val (account), futurehead::mdb_val (account_info_v14), 0));
	ASSERT_EQ (status, 0);
}

void modify_confirmation_height_to_v15 (futurehead::mdb_store & store, futurehead::transaction const & transaction, futurehead::account const & account, uint64_t confirmation_height)
{
	auto status (mdb_put (store.env.tx (transaction), store.confirmation_height, futurehead::mdb_val (account), futurehead::mdb_val (confirmation_height), 0));
	ASSERT_EQ (status, 0);
}

void modify_genesis_account_info_to_v5 (futurehead::mdb_store & store, futurehead::transaction const & transaction)
{
	futurehead::account_info info;
	store.account_get (transaction, futurehead::test_genesis_key.pub, info);
	futurehead::representative_visitor visitor (transaction, store);
	visitor.compute (info.head);
	futurehead::account_info_v5 info_old (info.head, visitor.result, info.open_block, info.balance, info.modified);
	auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, futurehead::mdb_val (futurehead::test_genesis_key.pub), futurehead::mdb_val (sizeof (info_old), &info_old), 0));
	ASSERT_EQ (status, 0);
}
}
