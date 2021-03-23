#include <futurehead/lib/logger_mt.hpp>
#include <futurehead/node/lmdb/lmdb.hpp>
#include <futurehead/secure/blockstore.hpp>
#include <futurehead/secure/utility.hpp>
#include <futurehead/secure/versioning.hpp>

#include <gtest/gtest.h>

TEST (versioning, account_info_v1)
{
	auto file (futurehead::unique_path ());
	futurehead::account account (1);
	futurehead::open_block open (1, 2, 3, nullptr);
	open.sideband_set ({});
	futurehead::account_info_v1 v1 (open.hash (), open.hash (), 3, 4);
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, file);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_write ());
		store.block_put (transaction, open.hash (), open);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, futurehead::mdb_val (account), futurehead::mdb_val (sizeof (v1), &v1), 0));
		ASSERT_EQ (0, status);
		store.version_put (transaction, 1);
	}

	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, file);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	futurehead::account_info v_latest;
	ASSERT_FALSE (store.account_get (transaction, account, v_latest));
	ASSERT_EQ (open.hash (), v_latest.open_block);
	ASSERT_EQ (v1.balance, v_latest.balance);
	ASSERT_EQ (v1.head, v_latest.head);
	ASSERT_EQ (v1.modified, v_latest.modified);
	ASSERT_EQ (v1.rep_block, open.hash ());
	ASSERT_EQ (1, v_latest.block_count);
	futurehead::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store.confirmation_height_get (transaction, account, confirmation_height_info));
	ASSERT_EQ (0, confirmation_height_info.height);
	ASSERT_EQ (futurehead::epoch::epoch_0, v_latest.epoch ());
}

TEST (versioning, account_info_v5)
{
	auto file (futurehead::unique_path ());
	futurehead::account account (1);
	futurehead::open_block open (1, 2, 3, nullptr);
	open.sideband_set ({});
	futurehead::account_info_v5 v5 (open.hash (), open.hash (), open.hash (), 3, 4);
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, file);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_write ());
		store.block_put (transaction, open.hash (), open);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, futurehead::mdb_val (account), futurehead::mdb_val (sizeof (v5), &v5), 0));
		ASSERT_EQ (0, status);
		store.version_put (transaction, 5);
	}

	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, file);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	futurehead::account_info v_latest;
	ASSERT_FALSE (store.account_get (transaction, account, v_latest));
	ASSERT_EQ (v5.open_block, v_latest.open_block);
	ASSERT_EQ (v5.balance, v_latest.balance);
	ASSERT_EQ (v5.head, v_latest.head);
	ASSERT_EQ (v5.modified, v_latest.modified);
	ASSERT_EQ (v5.rep_block, open.hash ());
	ASSERT_EQ (1, v_latest.block_count);
	futurehead::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store.confirmation_height_get (transaction, account, confirmation_height_info));
	ASSERT_EQ (0, confirmation_height_info.height);
	ASSERT_EQ (futurehead::epoch::epoch_0, v_latest.epoch ());
}

TEST (versioning, account_info_v13)
{
	auto file (futurehead::unique_path ());
	futurehead::account account (1);
	futurehead::open_block open (1, 2, 3, nullptr);
	open.sideband_set ({});
	futurehead::account_info_v13 v13 (open.hash (), open.hash (), open.hash (), 3, 4, 10, futurehead::epoch::epoch_0);
	{
		futurehead::logger_mt logger;
		futurehead::mdb_store store (logger, file);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_write ());
		store.block_put (transaction, open.hash (), open);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, futurehead::mdb_val (account), futurehead::mdb_val (v13), 0));
		ASSERT_EQ (0, status);
		store.version_put (transaction, 13);
	}

	futurehead::logger_mt logger;
	futurehead::mdb_store store (logger, file);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	futurehead::account_info v_latest;
	ASSERT_FALSE (store.account_get (transaction, account, v_latest));
	ASSERT_EQ (v13.open_block, v_latest.open_block);
	ASSERT_EQ (v13.balance, v_latest.balance);
	ASSERT_EQ (v13.head, v_latest.head);
	ASSERT_EQ (v13.modified, v_latest.modified);
	ASSERT_EQ (v13.rep_block, open.hash ());
	ASSERT_EQ (v13.block_count, v_latest.block_count);
	futurehead::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store.confirmation_height_get (transaction, account, confirmation_height_info));
	ASSERT_EQ (0, confirmation_height_info.height);
	ASSERT_EQ (v13.epoch, v_latest.epoch ());
}
