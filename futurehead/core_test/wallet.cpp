#include <futurehead/core_test/testutil.hpp>
#include <futurehead/crypto_lib/random_pool.hpp>
#include <futurehead/lib/threading.hpp>
#include <futurehead/node/lmdb/wallet_value.hpp>
#include <futurehead/node/testing.hpp>

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

using namespace std::chrono_literals;
unsigned constexpr futurehead::wallet_store::version_current;

TEST (wallet, no_special_keys_accounts)
{
	bool init;
	futurehead::mdb_env env (init, futurehead::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	futurehead::keypair key1;
	ASSERT_FALSE (wallet.exists (transaction, key1.pub));
	wallet.insert_adhoc (transaction, key1.prv);
	ASSERT_TRUE (wallet.exists (transaction, key1.pub));

	for (uint64_t account = 0; account < futurehead::wallet_store::special_count; account++)
	{
		futurehead::account account_l (account);
		ASSERT_FALSE (wallet.exists (transaction, account_l));
	}
}

TEST (wallet, no_key)
{
	bool init;
	futurehead::mdb_env env (init, futurehead::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	futurehead::keypair key1;
	futurehead::raw_key prv1;
	ASSERT_TRUE (wallet.fetch (transaction, key1.pub, prv1));
	ASSERT_TRUE (wallet.valid_password (transaction));
}

TEST (wallet, fetch_locked)
{
	bool init;
	futurehead::mdb_env env (init, futurehead::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_TRUE (wallet.valid_password (transaction));
	futurehead::keypair key1;
	ASSERT_EQ (key1.pub, wallet.insert_adhoc (transaction, key1.prv));
	auto key2 (wallet.deterministic_insert (transaction));
	ASSERT_FALSE (key2.is_zero ());
	futurehead::raw_key key3;
	key3.data = 1;
	wallet.password.value_set (key3);
	ASSERT_FALSE (wallet.valid_password (transaction));
	futurehead::raw_key key4;
	ASSERT_TRUE (wallet.fetch (transaction, key1.pub, key4));
	ASSERT_TRUE (wallet.fetch (transaction, key2, key4));
}

TEST (wallet, retrieval)
{
	bool init;
	futurehead::mdb_env env (init, futurehead::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	futurehead::keypair key1;
	ASSERT_TRUE (wallet.valid_password (transaction));
	wallet.insert_adhoc (transaction, key1.prv);
	futurehead::raw_key prv1;
	ASSERT_FALSE (wallet.fetch (transaction, key1.pub, prv1));
	ASSERT_TRUE (wallet.valid_password (transaction));
	ASSERT_EQ (key1.prv, prv1);
	wallet.password.values[0]->bytes[16] ^= 1;
	futurehead::raw_key prv2;
	ASSERT_TRUE (wallet.fetch (transaction, key1.pub, prv2));
	ASSERT_FALSE (wallet.valid_password (transaction));
}

TEST (wallet, empty_iteration)
{
	bool init;
	futurehead::mdb_env env (init, futurehead::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	auto i (wallet.begin (transaction));
	auto j (wallet.end ());
	ASSERT_EQ (i, j);
}

TEST (wallet, one_item_iteration)
{
	bool init;
	futurehead::mdb_env env (init, futurehead::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	futurehead::keypair key1;
	wallet.insert_adhoc (transaction, key1.prv);
	for (auto i (wallet.begin (transaction)), j (wallet.end ()); i != j; ++i)
	{
		ASSERT_EQ (key1.pub, futurehead::uint256_union (i->first));
		futurehead::raw_key password;
		wallet.wallet_key (password, transaction);
		futurehead::raw_key key;
		key.decrypt (futurehead::wallet_value (i->second).key, password, (futurehead::uint256_union (i->first)).owords[0].number ());
		ASSERT_EQ (key1.prv, key);
	}
}

TEST (wallet, two_item_iteration)
{
	bool init;
	futurehead::mdb_env env (init, futurehead::unique_path ());
	ASSERT_FALSE (init);
	futurehead::keypair key1;
	futurehead::keypair key2;
	ASSERT_NE (key1.pub, key2.pub);
	std::unordered_set<futurehead::public_key> pubs;
	std::unordered_set<futurehead::private_key> prvs;
	futurehead::kdf kdf;
	{
		auto transaction (env.tx_begin_write ());
		futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
		ASSERT_FALSE (init);
		wallet.insert_adhoc (transaction, key1.prv);
		wallet.insert_adhoc (transaction, key2.prv);
		for (auto i (wallet.begin (transaction)), j (wallet.end ()); i != j; ++i)
		{
			pubs.insert (i->first);
			futurehead::raw_key password;
			wallet.wallet_key (password, transaction);
			futurehead::raw_key key;
			key.decrypt (futurehead::wallet_value (i->second).key, password, (i->first).owords[0].number ());
			prvs.insert (key.as_private_key ());
		}
	}
	ASSERT_EQ (2, pubs.size ());
	ASSERT_EQ (2, prvs.size ());
	ASSERT_NE (pubs.end (), pubs.find (key1.pub));
	ASSERT_NE (prvs.end (), prvs.find (key1.prv.as_private_key ()));
	ASSERT_NE (pubs.end (), pubs.find (key2.pub));
	ASSERT_NE (prvs.end (), prvs.find (key2.prv.as_private_key ()));
}

TEST (wallet, insufficient_spend_one)
{
	futurehead::system system (1);
	futurehead::keypair key1;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto block (system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key1.pub, 500));
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key1.pub, futurehead::genesis_amount));
}

TEST (wallet, spend_all_one)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::block_hash latest1 (node1.latest (futurehead::test_genesis_key.pub));
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, std::numeric_limits<futurehead::uint128_t>::max ()));
	futurehead::account_info info2;
	{
		auto transaction (node1.store.tx_begin_read ());
		node1.store.account_get (transaction, futurehead::test_genesis_key.pub, info2);
		ASSERT_NE (latest1, info2.head);
		auto block (node1.store.block_get (transaction, info2.head));
		ASSERT_NE (nullptr, block);
		ASSERT_EQ (latest1, block->previous ());
	}
	ASSERT_TRUE (info2.balance.is_zero ());
	ASSERT_EQ (0, node1.balance (futurehead::test_genesis_key.pub));
}

TEST (wallet, send_async)
{
	futurehead::system system (1);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key2;
	boost::thread thread ([&system]() {
		system.deadline_set (10s);
		while (!system.nodes[0]->balance (futurehead::test_genesis_key.pub).is_zero ())
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	});
	std::atomic<bool> success (false);
	system.wallet (0)->send_async (futurehead::test_genesis_key.pub, key2.pub, std::numeric_limits<futurehead::uint128_t>::max (), [&success](std::shared_ptr<futurehead::block> block_a) { ASSERT_NE (nullptr, block_a); success = true; });
	thread.join ();
	ASSERT_TIMELY (2s, success);
}

TEST (wallet, spend)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::block_hash latest1 (node1.latest (futurehead::test_genesis_key.pub));
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key2;
	// Sending from empty accounts should always be an error.  Accounts need to be opened with an open block, not a send block.
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (0, key2.pub, 0));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, std::numeric_limits<futurehead::uint128_t>::max ()));
	futurehead::account_info info2;
	{
		auto transaction (node1.store.tx_begin_read ());
		node1.store.account_get (transaction, futurehead::test_genesis_key.pub, info2);
		ASSERT_NE (latest1, info2.head);
		auto block (node1.store.block_get (transaction, info2.head));
		ASSERT_NE (nullptr, block);
		ASSERT_EQ (latest1, block->previous ());
	}
	ASSERT_TRUE (info2.balance.is_zero ());
	ASSERT_EQ (0, node1.balance (futurehead::test_genesis_key.pub));
}

TEST (wallet, change)
{
	futurehead::system system (1);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key2;
	auto block1 (system.nodes[0]->rep_block (futurehead::test_genesis_key.pub));
	ASSERT_FALSE (block1.is_zero ());
	ASSERT_NE (nullptr, system.wallet (0)->change_action (futurehead::test_genesis_key.pub, key2.pub));
	auto block2 (system.nodes[0]->rep_block (futurehead::test_genesis_key.pub));
	ASSERT_FALSE (block2.is_zero ());
	ASSERT_NE (block1, block2);
}

TEST (wallet, partial_spend)
{
	futurehead::system system (1);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, 500));
	ASSERT_EQ (std::numeric_limits<futurehead::uint128_t>::max () - 500, system.nodes[0]->balance (futurehead::test_genesis_key.pub));
}

TEST (wallet, spend_no_previous)
{
	futurehead::system system (1);
	{
		system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		futurehead::account_info info1;
		ASSERT_FALSE (system.nodes[0]->store.account_get (transaction, futurehead::test_genesis_key.pub, info1));
		for (auto i (0); i < 50; ++i)
		{
			futurehead::keypair key;
			system.wallet (0)->insert_adhoc (key.prv);
		}
	}
	futurehead::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, 500));
	ASSERT_EQ (std::numeric_limits<futurehead::uint128_t>::max () - 500, system.nodes[0]->balance (futurehead::test_genesis_key.pub));
}

TEST (wallet, find_none)
{
	bool init;
	futurehead::mdb_env env (init, futurehead::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	futurehead::account account (1000);
	ASSERT_EQ (wallet.end (), wallet.find (transaction, account));
}

TEST (wallet, find_existing)
{
	bool init;
	futurehead::mdb_env env (init, futurehead::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	futurehead::keypair key1;
	ASSERT_FALSE (wallet.exists (transaction, key1.pub));
	wallet.insert_adhoc (transaction, key1.prv);
	ASSERT_TRUE (wallet.exists (transaction, key1.pub));
	auto existing (wallet.find (transaction, key1.pub));
	ASSERT_NE (wallet.end (), existing);
	++existing;
	ASSERT_EQ (wallet.end (), existing);
}

TEST (wallet, rekey)
{
	bool init;
	futurehead::mdb_env env (init, futurehead::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	futurehead::raw_key password;
	wallet.password.value (password);
	ASSERT_TRUE (password.data.is_zero ());
	ASSERT_FALSE (init);
	futurehead::keypair key1;
	wallet.insert_adhoc (transaction, key1.prv);
	futurehead::raw_key prv1;
	wallet.fetch (transaction, key1.pub, prv1);
	ASSERT_EQ (key1.prv, prv1);
	ASSERT_FALSE (wallet.rekey (transaction, "1"));
	wallet.password.value (password);
	futurehead::raw_key password1;
	wallet.derive_key (password1, transaction, "1");
	ASSERT_EQ (password1, password);
	futurehead::raw_key prv2;
	wallet.fetch (transaction, key1.pub, prv2);
	ASSERT_EQ (key1.prv, prv2);
	*wallet.password.values[0] = 2;
	ASSERT_TRUE (wallet.rekey (transaction, "2"));
}

TEST (account, encode_zero)
{
	futurehead::account number0 (0);
	std::string str0;
	number0.encode_account (str0);

	//TO-CHANGE NUMEROS CARACTERES DA HASH 60 + 5 (4 letras + underscore)
	ASSERT_EQ (65, str0.size ());
	ASSERT_EQ (65, str0.size ());
	futurehead::account number1;
	ASSERT_FALSE (number1.decode_account (str0));
	ASSERT_EQ (number0, number1);
}

TEST (account, encode_all)
{
	futurehead::account number0;
	number0.decode_hex ("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
	std::string str0;
	number0.encode_account (str0);

	//TO-CHANGE NUMEROS CARACTERES DA HASH 60 + 5 (4 letras + underscore)
	ASSERT_EQ (65, str0.size ());
	futurehead::account number1;
	ASSERT_FALSE (number1.decode_account (str0));
	ASSERT_EQ (number0, number1);
}

TEST (account, encode_fail)
{
	futurehead::account number0 (0);
	std::string str0;
	number0.encode_account (str0);
	str0[16] ^= 1;
	futurehead::account number1;
	ASSERT_TRUE (number1.decode_account (str0));
}

TEST (wallet, hash_password)
{
	bool init;
	futurehead::mdb_env env (init, futurehead::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	futurehead::raw_key hash1;
	wallet.derive_key (hash1, transaction, "");
	futurehead::raw_key hash2;
	wallet.derive_key (hash2, transaction, "");
	ASSERT_EQ (hash1, hash2);
	futurehead::raw_key hash3;
	wallet.derive_key (hash3, transaction, "a");
	ASSERT_NE (hash1, hash3);
}

TEST (fan, reconstitute)
{
	futurehead::uint256_union value0 (0);
	futurehead::fan fan (value0, 1024);
	for (auto & i : fan.values)
	{
		ASSERT_NE (value0, *i);
	}
	futurehead::raw_key value1;
	fan.value (value1);
	ASSERT_EQ (value0, value1.data);
}

TEST (fan, change)
{
	futurehead::raw_key value0;
	value0.data = 0;
	futurehead::raw_key value1;
	value1.data = 1;
	ASSERT_NE (value0, value1);
	futurehead::fan fan (value0.data, 1024);
	ASSERT_EQ (1024, fan.values.size ());
	futurehead::raw_key value2;
	fan.value (value2);
	ASSERT_EQ (value0, value2);
	fan.value_set (value1);
	fan.value (value2);
	ASSERT_EQ (value1, value2);
}

TEST (wallet, reopen_default_password)
{
	bool init;
	futurehead::mdb_env env (init, futurehead::unique_path ());
	auto transaction (env.tx_begin_write ());
	ASSERT_FALSE (init);
	futurehead::kdf kdf;
	{
		futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
		ASSERT_FALSE (init);
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		bool init;
		futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
		ASSERT_FALSE (init);
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
		ASSERT_FALSE (init);
		wallet.rekey (transaction, "");
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		bool init;
		futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
		ASSERT_FALSE (init);
		ASSERT_FALSE (wallet.valid_password (transaction));
		wallet.attempt_password (transaction, " ");
		ASSERT_FALSE (wallet.valid_password (transaction));
		wallet.attempt_password (transaction, "");
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
}

TEST (wallet, representative)
{
	auto error (false);
	futurehead::mdb_env env (error, futurehead::unique_path ());
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet (error, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	ASSERT_FALSE (wallet.is_representative (transaction));
	ASSERT_EQ (futurehead::genesis_account, wallet.representative (transaction));
	ASSERT_FALSE (wallet.is_representative (transaction));
	futurehead::keypair key;
	wallet.representative_set (transaction, key.pub);
	ASSERT_FALSE (wallet.is_representative (transaction));
	ASSERT_EQ (key.pub, wallet.representative (transaction));
	ASSERT_FALSE (wallet.is_representative (transaction));
	wallet.insert_adhoc (transaction, key.prv);
	ASSERT_TRUE (wallet.is_representative (transaction));
}

TEST (wallet, serialize_json_empty)
{
	auto error (false);
	futurehead::mdb_env env (error, futurehead::unique_path ());
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet1 (error, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	futurehead::wallet_store wallet2 (error, kdf, transaction, futurehead::genesis_account, 1, "1", serialized);
	ASSERT_FALSE (error);
	futurehead::raw_key password1;
	futurehead::raw_key password2;
	wallet1.wallet_key (password1, transaction);
	wallet2.wallet_key (password2, transaction);
	ASSERT_EQ (password1, password2);
	ASSERT_EQ (wallet1.salt (transaction), wallet2.salt (transaction));
	ASSERT_EQ (wallet1.check (transaction), wallet2.check (transaction));
	ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
	ASSERT_EQ (wallet1.end (), wallet1.begin (transaction));
	ASSERT_EQ (wallet2.end (), wallet2.begin (transaction));
}

TEST (wallet, serialize_json_one)
{
	auto error (false);
	futurehead::mdb_env env (error, futurehead::unique_path ());
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet1 (error, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	futurehead::keypair key;
	wallet1.insert_adhoc (transaction, key.prv);
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	futurehead::wallet_store wallet2 (error, kdf, transaction, futurehead::genesis_account, 1, "1", serialized);
	ASSERT_FALSE (error);
	futurehead::raw_key password1;
	futurehead::raw_key password2;
	wallet1.wallet_key (password1, transaction);
	wallet2.wallet_key (password2, transaction);
	ASSERT_EQ (password1, password2);
	ASSERT_EQ (wallet1.salt (transaction), wallet2.salt (transaction));
	ASSERT_EQ (wallet1.check (transaction), wallet2.check (transaction));
	ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
	ASSERT_TRUE (wallet2.exists (transaction, key.pub));
	futurehead::raw_key prv;
	wallet2.fetch (transaction, key.pub, prv);
	ASSERT_EQ (key.prv, prv);
}

TEST (wallet, serialize_json_password)
{
	auto error (false);
	futurehead::mdb_env env (error, futurehead::unique_path ());
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet1 (error, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	futurehead::keypair key;
	wallet1.rekey (transaction, "password");
	wallet1.insert_adhoc (transaction, key.prv);
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	futurehead::wallet_store wallet2 (error, kdf, transaction, futurehead::genesis_account, 1, "1", serialized);
	ASSERT_FALSE (error);
	ASSERT_FALSE (wallet2.valid_password (transaction));
	ASSERT_FALSE (wallet2.attempt_password (transaction, "password"));
	ASSERT_TRUE (wallet2.valid_password (transaction));
	futurehead::raw_key password1;
	futurehead::raw_key password2;
	wallet1.wallet_key (password1, transaction);
	wallet2.wallet_key (password2, transaction);
	ASSERT_EQ (password1, password2);
	ASSERT_EQ (wallet1.salt (transaction), wallet2.salt (transaction));
	ASSERT_EQ (wallet1.check (transaction), wallet2.check (transaction));
	ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
	ASSERT_TRUE (wallet2.exists (transaction, key.pub));
	futurehead::raw_key prv;
	wallet2.fetch (transaction, key.pub, prv);
	ASSERT_EQ (key.prv, prv);
}

TEST (wallet_store, move)
{
	auto error (false);
	futurehead::mdb_env env (error, futurehead::unique_path ());
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet1 (error, kdf, transaction, futurehead::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	futurehead::keypair key1;
	wallet1.insert_adhoc (transaction, key1.prv);
	futurehead::wallet_store wallet2 (error, kdf, transaction, futurehead::genesis_account, 1, "1");
	ASSERT_FALSE (error);
	futurehead::keypair key2;
	wallet2.insert_adhoc (transaction, key2.prv);
	ASSERT_FALSE (wallet1.exists (transaction, key2.pub));
	ASSERT_TRUE (wallet2.exists (transaction, key2.pub));
	std::vector<futurehead::public_key> keys;
	keys.push_back (key2.pub);
	ASSERT_FALSE (wallet1.move (transaction, wallet2, keys));
	ASSERT_TRUE (wallet1.exists (transaction, key2.pub));
	ASSERT_FALSE (wallet2.exists (transaction, key2.pub));
}

TEST (wallet_store, import)
{
	futurehead::system system (2);
	auto wallet1 (system.wallet (0));
	auto wallet2 (system.wallet (1));
	futurehead::keypair key1;
	wallet1->insert_adhoc (key1.prv);
	std::string json;
	wallet1->serialize (json);
	ASSERT_FALSE (wallet2->exists (key1.pub));
	auto error (wallet2->import (json, ""));
	ASSERT_FALSE (error);
	ASSERT_TRUE (wallet2->exists (key1.pub));
}

TEST (wallet_store, fail_import_bad_password)
{
	futurehead::system system (2);
	auto wallet1 (system.wallet (0));
	auto wallet2 (system.wallet (1));
	futurehead::keypair key1;
	wallet1->insert_adhoc (key1.prv);
	std::string json;
	wallet1->serialize (json);
	ASSERT_FALSE (wallet2->exists (key1.pub));
	auto error (wallet2->import (json, "1"));
	ASSERT_TRUE (error);
}

TEST (wallet_store, fail_import_corrupt)
{
	futurehead::system system (2);
	auto wallet1 (system.wallet (1));
	std::string json;
	auto error (wallet1->import (json, "1"));
	ASSERT_TRUE (error);
}

// Test work is precached when a key is inserted
TEST (wallet, work)
{
	futurehead::system system (1);
	auto wallet (system.wallet (0));
	wallet->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::genesis genesis;
	auto done (false);
	system.deadline_set (20s);
	while (!done)
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_read ());
		uint64_t work (0);
		if (!wallet->store.work_get (transaction, futurehead::test_genesis_key.pub, work))
		{
			done = futurehead::work_difficulty (genesis.open->work_version (), genesis.hash (), work) >= system.nodes[0]->default_difficulty (genesis.open->work_version ());
		}
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (wallet, work_generate)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	auto wallet (system.wallet (0));
	futurehead::uint128_t amount1 (node1.balance (futurehead::test_genesis_key.pub));
	uint64_t work1;
	wallet->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::account account1;
	{
		auto transaction (node1.wallets.tx_begin_read ());
		account1 = system.account (transaction, 0);
	}
	futurehead::keypair key;
	auto block (wallet->send_action (futurehead::test_genesis_key.pub, key.pub, 100));
	system.deadline_set (10s);
	auto transaction (node1.store.tx_begin_read ());
	while (node1.ledger.account_balance (transaction, futurehead::test_genesis_key.pub) == amount1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.deadline_set (10s);
	auto again (true);
	while (again)
	{
		ASSERT_NO_ERROR (system.poll ());
		auto block_transaction (node1.store.tx_begin_read ());
		auto transaction (system.wallet (0)->wallets.tx_begin_read ());
		again = wallet->store.work_get (transaction, account1, work1) || futurehead::work_difficulty (block->work_version (), node1.ledger.latest_root (block_transaction, account1), work1) < node1.default_difficulty (block->work_version ());
	}
}

TEST (wallet, work_cache_delayed)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	auto wallet (system.wallet (0));
	uint64_t work1;
	wallet->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::account account1;
	{
		auto transaction (node1.wallets.tx_begin_read ());
		account1 = system.account (transaction, 0);
	}
	futurehead::keypair key;
	auto block1 (wallet->send_action (futurehead::test_genesis_key.pub, key.pub, 100));
	ASSERT_EQ (block1->hash (), node1.latest (futurehead::test_genesis_key.pub));
	auto block2 (wallet->send_action (futurehead::test_genesis_key.pub, key.pub, 100));
	ASSERT_EQ (block2->hash (), node1.latest (futurehead::test_genesis_key.pub));
	ASSERT_EQ (block2->hash (), node1.wallets.delayed_work->operator[] (futurehead::test_genesis_key.pub));
	auto threshold (node1.default_difficulty (futurehead::work_version::work_1));
	auto again (true);
	system.deadline_set (10s);
	while (again)
	{
		ASSERT_NO_ERROR (system.poll ());
		if (!wallet->store.work_get (node1.wallets.tx_begin_read (), account1, work1))
		{
			again = futurehead::work_difficulty (futurehead::work_version::work_1, block2->hash (), work1) < threshold;
		}
	}
	ASSERT_GE (futurehead::work_difficulty (futurehead::work_version::work_1, block2->hash (), work1), threshold);
}

TEST (wallet, insert_locked)
{
	futurehead::system system (1);
	auto wallet (system.wallet (0));
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		wallet->store.rekey (transaction, "1");
		ASSERT_TRUE (wallet->store.valid_password (transaction));
		wallet->enter_password (transaction, "");
	}
	auto transaction (wallet->wallets.tx_begin_read ());
	ASSERT_FALSE (wallet->store.valid_password (transaction));
	ASSERT_TRUE (wallet->insert_adhoc (futurehead::keypair ().prv).is_zero ());
}

TEST (wallet, version_1_upgrade)
{
	futurehead::system system (1);
	auto wallet (system.wallet (0));
	wallet->enter_initial_password ();
	futurehead::keypair key;
	auto transaction (wallet->wallets.tx_begin_write ());
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	wallet->store.rekey (transaction, "1");
	wallet->enter_password (transaction, "");
	ASSERT_FALSE (wallet->store.valid_password (transaction));
	futurehead::raw_key password_l;
	futurehead::wallet_value value (wallet->store.entry_get_raw (transaction, futurehead::wallet_store::wallet_key_special));
	futurehead::raw_key kdf;
	kdf.data.clear ();
	password_l.decrypt (value.key, kdf, wallet->store.salt (transaction).owords[0]);
	futurehead::uint256_union ciphertext;
	ciphertext.encrypt (key.prv, password_l, wallet->store.salt (transaction).owords[0]);
	wallet->store.entry_put_raw (transaction, key.pub, futurehead::wallet_value (ciphertext, 0));
	wallet->store.version_put (transaction, 1);
	wallet->enter_password (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	ASSERT_EQ (futurehead::wallet_store::version_current, wallet->store.version (transaction));
	futurehead::raw_key prv;
	ASSERT_FALSE (wallet->store.fetch (transaction, key.pub, prv));
	ASSERT_EQ (key.prv, prv);
	value = wallet->store.entry_get_raw (transaction, futurehead::wallet_store::wallet_key_special);
	wallet->store.derive_key (kdf, transaction, "");
	password_l.decrypt (value.key, kdf, wallet->store.salt (transaction).owords[0]);
	ciphertext.encrypt (key.prv, password_l, wallet->store.salt (transaction).owords[0]);
	wallet->store.entry_put_raw (transaction, key.pub, futurehead::wallet_value (ciphertext, 0));
	wallet->store.version_put (transaction, 1);
	wallet->enter_password (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	ASSERT_EQ (futurehead::wallet_store::version_current, wallet->store.version (transaction));
	futurehead::raw_key prv2;
	ASSERT_FALSE (wallet->store.fetch (transaction, key.pub, prv2));
	ASSERT_EQ (key.prv, prv2);
}

TEST (wallet, deterministic_keys)
{
	bool init;
	futurehead::mdb_env env (init, futurehead::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
	auto key1 = wallet.deterministic_key (transaction, 0);
	auto key2 = wallet.deterministic_key (transaction, 0);
	ASSERT_EQ (key1, key2);
	auto key3 = wallet.deterministic_key (transaction, 1);
	ASSERT_NE (key1, key3);
	ASSERT_EQ (0, wallet.deterministic_index_get (transaction));
	wallet.deterministic_index_set (transaction, 1);
	ASSERT_EQ (1, wallet.deterministic_index_get (transaction));
	auto key4 (wallet.deterministic_insert (transaction));
	futurehead::raw_key key5;
	ASSERT_FALSE (wallet.fetch (transaction, key4, key5));
	ASSERT_EQ (key3, key5.as_private_key ());
	ASSERT_EQ (2, wallet.deterministic_index_get (transaction));
	wallet.deterministic_index_set (transaction, 1);
	ASSERT_EQ (1, wallet.deterministic_index_get (transaction));
	wallet.erase (transaction, key4);
	ASSERT_FALSE (wallet.exists (transaction, key4));
	auto key8 (wallet.deterministic_insert (transaction));
	ASSERT_EQ (key4, key8);
	auto key6 (wallet.deterministic_insert (transaction));
	futurehead::raw_key key7;
	ASSERT_FALSE (wallet.fetch (transaction, key6, key7));
	ASSERT_NE (key5, key7);
	ASSERT_EQ (3, wallet.deterministic_index_get (transaction));
	futurehead::keypair key9;
	ASSERT_EQ (key9.pub, wallet.insert_adhoc (transaction, key9.prv));
	ASSERT_TRUE (wallet.exists (transaction, key9.pub));
	wallet.deterministic_clear (transaction);
	ASSERT_EQ (0, wallet.deterministic_index_get (transaction));
	ASSERT_FALSE (wallet.exists (transaction, key4));
	ASSERT_FALSE (wallet.exists (transaction, key6));
	ASSERT_FALSE (wallet.exists (transaction, key8));
	ASSERT_TRUE (wallet.exists (transaction, key9.pub));
}

TEST (wallet, reseed)
{
	bool init;
	futurehead::mdb_env env (init, futurehead::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	futurehead::kdf kdf;
	futurehead::wallet_store wallet (init, kdf, transaction, futurehead::genesis_account, 1, "0");
	futurehead::raw_key seed1;
	seed1.data = 1;
	futurehead::raw_key seed2;
	seed2.data = 2;
	wallet.seed_set (transaction, seed1);
	futurehead::raw_key seed3;
	wallet.seed (seed3, transaction);
	ASSERT_EQ (seed1, seed3);
	auto key1 (wallet.deterministic_insert (transaction));
	ASSERT_EQ (1, wallet.deterministic_index_get (transaction));
	wallet.seed_set (transaction, seed2);
	ASSERT_EQ (0, wallet.deterministic_index_get (transaction));
	futurehead::raw_key seed4;
	wallet.seed (seed4, transaction);
	ASSERT_EQ (seed2, seed4);
	auto key2 (wallet.deterministic_insert (transaction));
	ASSERT_NE (key1, key2);
	wallet.seed_set (transaction, seed1);
	futurehead::raw_key seed5;
	wallet.seed (seed5, transaction);
	ASSERT_EQ (seed1, seed5);
	auto key3 (wallet.deterministic_insert (transaction));
	ASSERT_EQ (key1, key3);
}

TEST (wallet, insert_deterministic_locked)
{
	futurehead::system system (1);
	auto wallet (system.wallet (0));
	auto transaction (wallet->wallets.tx_begin_write ());
	wallet->store.rekey (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	wallet->enter_password (transaction, "");
	ASSERT_FALSE (wallet->store.valid_password (transaction));
	ASSERT_TRUE (wallet->deterministic_insert (transaction).is_zero ());
}

TEST (wallet, version_2_upgrade)
{
	futurehead::system system (1);
	auto wallet (system.wallet (0));
	auto transaction (wallet->wallets.tx_begin_write ());
	wallet->store.rekey (transaction, "1");
	ASSERT_TRUE (wallet->store.attempt_password (transaction, ""));
	wallet->store.erase (transaction, futurehead::wallet_store::deterministic_index_special);
	wallet->store.erase (transaction, futurehead::wallet_store::seed_special);
	wallet->store.version_put (transaction, 2);
	ASSERT_EQ (2, wallet->store.version (transaction));
	ASSERT_EQ (wallet->store.find (transaction, futurehead::wallet_store::deterministic_index_special), wallet->store.end ());
	ASSERT_EQ (wallet->store.find (transaction, futurehead::wallet_store::seed_special), wallet->store.end ());
	wallet->store.attempt_password (transaction, "1");
	ASSERT_EQ (futurehead::wallet_store::version_current, wallet->store.version (transaction));
	ASSERT_NE (wallet->store.find (transaction, futurehead::wallet_store::deterministic_index_special), wallet->store.end ());
	ASSERT_NE (wallet->store.find (transaction, futurehead::wallet_store::seed_special), wallet->store.end ());
	ASSERT_FALSE (wallet->deterministic_insert (transaction).is_zero ());
}

TEST (wallet, version_3_upgrade)
{
	futurehead::system system (1);
	auto wallet (system.wallet (0));
	auto transaction (wallet->wallets.tx_begin_write ());
	wallet->store.rekey (transaction, "1");
	wallet->enter_password (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	ASSERT_EQ (futurehead::wallet_store::version_current, wallet->store.version (transaction));
	futurehead::keypair key;
	futurehead::raw_key seed;
	futurehead::uint256_union seed_ciphertext;
	futurehead::random_pool::generate_block (seed.data.bytes.data (), seed.data.bytes.size ());
	futurehead::raw_key password_l;
	futurehead::wallet_value value (wallet->store.entry_get_raw (transaction, futurehead::wallet_store::wallet_key_special));
	futurehead::raw_key kdf;
	wallet->store.derive_key (kdf, transaction, "1");
	password_l.decrypt (value.key, kdf, wallet->store.salt (transaction).owords[0]);
	futurehead::uint256_union ciphertext;
	ciphertext.encrypt (key.prv, password_l, wallet->store.salt (transaction).owords[0]);
	wallet->store.entry_put_raw (transaction, key.pub, futurehead::wallet_value (ciphertext, 0));
	seed_ciphertext.encrypt (seed, password_l, wallet->store.salt (transaction).owords[0]);
	wallet->store.entry_put_raw (transaction, futurehead::wallet_store::seed_special, futurehead::wallet_value (seed_ciphertext, 0));
	wallet->store.version_put (transaction, 3);
	wallet->enter_password (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	ASSERT_EQ (futurehead::wallet_store::version_current, wallet->store.version (transaction));
	futurehead::raw_key prv;
	ASSERT_FALSE (wallet->store.fetch (transaction, key.pub, prv));
	ASSERT_EQ (key.prv, prv);
	futurehead::raw_key seed_compare;
	wallet->store.seed (seed_compare, transaction);
	ASSERT_EQ (seed, seed_compare);
	ASSERT_NE (seed_ciphertext, wallet->store.entry_get_raw (transaction, futurehead::wallet_store::seed_special).key);
}

TEST (wallet, upgrade_backup)
{
	futurehead::system system (1);
	auto dir (futurehead::unique_path ());
	namespace fs = boost::filesystem;
	fs::create_directory (dir);
	/** Returns 'dir' if backup file cannot be found */
	auto get_backup_path = [&dir]() {
		for (fs::directory_iterator itr (dir); itr != fs::directory_iterator (); ++itr)
		{
			if (itr->path ().filename ().string ().find ("wallets_backup_") != std::string::npos)
			{
				return itr->path ();
			}
		}
		return dir;
	};

	auto wallet_id = futurehead::random_wallet_id ();
	{
		auto node1 (std::make_shared<futurehead::node> (system.io_ctx, futurehead::get_available_port (), dir, system.alarm, system.logging, system.work));
		ASSERT_FALSE (node1->init_error ());
		auto wallet (node1->wallets.create (wallet_id));
		ASSERT_NE (nullptr, wallet);
		auto transaction (node1->wallets.tx_begin_write ());
		wallet->store.version_put (transaction, 3);
	}
	ASSERT_EQ (get_backup_path ().string (), dir.string ());

	// Check with config backup_before_upgrade = false
	{
		auto node1 (std::make_shared<futurehead::node> (system.io_ctx, futurehead::get_available_port (), dir, system.alarm, system.logging, system.work));
		ASSERT_FALSE (node1->init_error ());
		auto wallet (node1->wallets.open (wallet_id));
		ASSERT_NE (nullptr, wallet);
		auto transaction (node1->wallets.tx_begin_write ());
		ASSERT_LT (3u, wallet->store.version (transaction));
		wallet->store.version_put (transaction, 3);
	}
	ASSERT_EQ (get_backup_path ().string (), dir.string ());

	// Now do the upgrade and confirm that backup is saved
	{
		futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
		node_config.backup_before_upgrade = true;
		auto node1 (std::make_shared<futurehead::node> (system.io_ctx, dir, system.alarm, node_config, system.work));
		ASSERT_FALSE (node1->init_error ());
		auto wallet (node1->wallets.open (wallet_id));
		ASSERT_NE (nullptr, wallet);
		auto transaction (node1->wallets.tx_begin_read ());
		ASSERT_LT (3u, wallet->store.version (transaction));
	}
	ASSERT_NE (get_backup_path ().string (), dir.string ());
}

TEST (wallet, no_work)
{
	futurehead::system system (1);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv, false);
	futurehead::keypair key2;
	auto block (system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, std::numeric_limits<futurehead::uint128_t>::max (), false));
	ASSERT_NE (nullptr, block);
	ASSERT_NE (0, block->block_work ());
	ASSERT_GE (block->difficulty (), futurehead::work_threshold (block->work_version (), block->sideband ().details));
	auto transaction (system.wallet (0)->wallets.tx_begin_read ());
	uint64_t cached_work (0);
	system.wallet (0)->store.work_get (transaction, futurehead::test_genesis_key.pub, cached_work);
	ASSERT_EQ (0, cached_work);
}

TEST (wallet, send_race)
{
	futurehead::system system (1);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key2;
	for (auto i (1); i < 60; ++i)
	{
		ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, futurehead::Gxrb_ratio));
		ASSERT_EQ (futurehead::genesis_amount - futurehead::Gxrb_ratio * i, system.nodes[0]->balance (futurehead::test_genesis_key.pub));
	}
}

TEST (wallet, password_race)
{
	futurehead::system system (1);
	futurehead::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
	auto wallet = system.wallet (0);
	std::thread thread ([&wallet]() {
		for (int i = 0; i < 100; i++)
		{
			auto transaction (wallet->wallets.tx_begin_write ());
			wallet->store.rekey (transaction, std::to_string (i));
		}
	});
	for (int i = 0; i < 100; i++)
	{
		auto transaction (wallet->wallets.tx_begin_read ());
		// Password should always be valid, the rekey operation should be atomic.
		bool ok = wallet->store.valid_password (transaction);
		EXPECT_TRUE (ok);
		if (!ok)
		{
			break;
		}
	}
	thread.join ();
	system.stop ();
	runner.join ();
}

TEST (wallet, password_race_corrupt_seed)
{
	futurehead::system system (1);
	futurehead::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
	auto wallet = system.wallet (0);
	futurehead::raw_key seed;
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		ASSERT_FALSE (wallet->store.rekey (transaction, "4567"));
		wallet->store.seed (seed, transaction);
		ASSERT_FALSE (wallet->store.attempt_password (transaction, "4567"));
	}
	std::vector<std::thread> threads;
	for (int i = 0; i < 100; i++)
	{
		threads.emplace_back ([&wallet]() {
			for (int i = 0; i < 10; i++)
			{
				auto transaction (wallet->wallets.tx_begin_write ());
				wallet->store.rekey (transaction, "0000");
			}
		});
		threads.emplace_back ([&wallet]() {
			for (int i = 0; i < 10; i++)
			{
				auto transaction (wallet->wallets.tx_begin_write ());
				wallet->store.rekey (transaction, "1234");
			}
		});
		threads.emplace_back ([&wallet]() {
			for (int i = 0; i < 10; i++)
			{
				auto transaction (wallet->wallets.tx_begin_read ());
				wallet->store.attempt_password (transaction, "1234");
			}
		});
	}
	for (auto & thread : threads)
	{
		thread.join ();
	}
	system.stop ();
	runner.join ();
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		if (!wallet->store.attempt_password (transaction, "1234"))
		{
			futurehead::raw_key seed_now;
			wallet->store.seed (seed_now, transaction);
			ASSERT_TRUE (seed_now == seed);
		}
		else if (!wallet->store.attempt_password (transaction, "0000"))
		{
			futurehead::raw_key seed_now;
			wallet->store.seed (seed_now, transaction);
			ASSERT_TRUE (seed_now == seed);
		}
		else if (!wallet->store.attempt_password (transaction, "4567"))
		{
			futurehead::raw_key seed_now;
			wallet->store.seed (seed_now, transaction);
			ASSERT_TRUE (seed_now == seed);
		}
		else
		{
			ASSERT_FALSE (true);
		}
	}
}

TEST (wallet, change_seed)
{
	futurehead::system system (1);
	auto wallet (system.wallet (0));
	wallet->enter_initial_password ();
	futurehead::raw_key seed1;
	seed1.data = 1;
	futurehead::public_key pub;
	uint32_t index (4);
	auto prv = futurehead::deterministic_key (seed1, index);
	pub = futurehead::pub_key (prv);
	wallet->insert_adhoc (futurehead::test_genesis_key.prv, false);
	auto block (wallet->send_action (futurehead::test_genesis_key.pub, pub, 100));
	ASSERT_NE (nullptr, block);
	system.nodes[0]->block_processor.flush ();
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		wallet->change_seed (transaction, seed1);
		futurehead::raw_key seed2;
		wallet->store.seed (seed2, transaction);
		ASSERT_EQ (seed1, seed2);
		ASSERT_EQ (index + 1, wallet->store.deterministic_index_get (transaction));
	}
	ASSERT_TRUE (wallet->exists (pub));
}

TEST (wallet, deterministic_restore)
{
	futurehead::system system (1);
	auto wallet (system.wallet (0));
	wallet->enter_initial_password ();
	futurehead::raw_key seed1;
	seed1.data = 1;
	futurehead::public_key pub;
	uint32_t index (4);
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		wallet->change_seed (transaction, seed1);
		futurehead::raw_key seed2;
		wallet->store.seed (seed2, transaction);
		ASSERT_EQ (seed1, seed2);
		ASSERT_EQ (1, wallet->store.deterministic_index_get (transaction));
		auto prv = futurehead::deterministic_key (seed1, index);
		pub = futurehead::pub_key (prv);
	}
	wallet->insert_adhoc (futurehead::test_genesis_key.prv, false);
	auto block (wallet->send_action (futurehead::test_genesis_key.pub, pub, 100));
	ASSERT_NE (nullptr, block);
	system.nodes[0]->block_processor.flush ();
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		wallet->deterministic_restore (transaction);
		ASSERT_EQ (index + 1, wallet->store.deterministic_index_get (transaction));
	}
	ASSERT_TRUE (wallet->exists (pub));
}

TEST (work_watcher, update)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.work_watcher_period = 1s;
	node_config.max_work_generate_multiplier = 1e6;
	futurehead::node_flags node_flags;
	node_flags.disable_request_loop = true;
	auto & node = *system.add_node (node_config, node_flags);
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key;
	auto const block1 (wallet.send_action (futurehead::test_genesis_key.pub, key.pub, 100));
	auto difficulty1 (block1->difficulty ());
	auto multiplier1 (futurehead::normalized_multiplier (futurehead::difficulty::to_multiplier (difficulty1, futurehead::work_threshold (block1->work_version (), futurehead::block_details (futurehead::epoch::epoch_0, true, false, false))), node.network_params.network.publish_thresholds.epoch_1));
	auto const block2 (wallet.send_action (futurehead::test_genesis_key.pub, key.pub, 200));
	auto difficulty2 (block2->difficulty ());
	auto multiplier2 (futurehead::normalized_multiplier (futurehead::difficulty::to_multiplier (difficulty2, futurehead::work_threshold (block2->work_version (), futurehead::block_details (futurehead::epoch::epoch_0, true, false, false))), node.network_params.network.publish_thresholds.epoch_1));
	double updated_multiplier1{ multiplier1 }, updated_multiplier2{ multiplier2 }, target_multiplier{ std::max (multiplier1, multiplier2) + 1e-6 };
	{
		futurehead::lock_guard<std::mutex> guard (node.active.mutex);
		node.active.trended_active_multiplier = target_multiplier;
	}
	system.deadline_set (20s);
	while (updated_multiplier1 == multiplier1 || updated_multiplier2 == multiplier2)
	{
		{
			futurehead::lock_guard<std::mutex> guard (node.active.mutex);
			{
				auto const existing (node.active.roots.find (block1->qualified_root ()));
				//if existing is junk the block has been confirmed already
				ASSERT_NE (existing, node.active.roots.end ());
				updated_multiplier1 = existing->multiplier;
			}
			{
				auto const existing (node.active.roots.find (block2->qualified_root ()));
				//if existing is junk the block has been confirmed already
				ASSERT_NE (existing, node.active.roots.end ());
				updated_multiplier2 = existing->multiplier;
			}
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_GT (updated_multiplier1, multiplier1);
	ASSERT_GT (updated_multiplier2, multiplier2);
}

TEST (work_watcher, propagate)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.work_watcher_period = 1s;
	node_config.max_work_generate_multiplier = 1e6;
	futurehead::node_flags node_flags;
	node_flags.disable_request_loop = true;
	auto & node = *system.add_node (node_config, node_flags);
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (futurehead::test_genesis_key.prv);
	node_config.peering_port = futurehead::get_available_port ();
	auto & node_passive = *system.add_node (node_config);
	futurehead::keypair key;
	auto const block (wallet.send_action (futurehead::test_genesis_key.pub, key.pub, 100));
	system.deadline_set (5s);
	while (!node_passive.ledger.block_exists (block->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto const multiplier (futurehead::normalized_multiplier (futurehead::difficulty::to_multiplier (block->difficulty (), futurehead::work_threshold (block->work_version (), futurehead::block_details (futurehead::epoch::epoch_0, false, false, false))), node.network_params.network.publish_thresholds.epoch_1));
	auto updated_multiplier{ multiplier };
	auto propagated_multiplier{ multiplier };
	{
		futurehead::lock_guard<std::mutex> guard (node.active.mutex);
		node.active.trended_active_multiplier = multiplier * 1.001;
	}
	bool updated{ false };
	bool propagated{ false };
	system.deadline_set (10s);
	while (!(updated && propagated))
	{
		{
			futurehead::lock_guard<std::mutex> guard (node.active.mutex);
			{
				auto const existing (node.active.roots.find (block->qualified_root ()));
				ASSERT_NE (existing, node.active.roots.end ());
				updated_multiplier = existing->multiplier;
			}
		}
		{
			futurehead::lock_guard<std::mutex> guard (node_passive.active.mutex);
			{
				auto const existing (node_passive.active.roots.find (block->qualified_root ()));
				ASSERT_NE (existing, node_passive.active.roots.end ());
				propagated_multiplier = existing->multiplier;
			}
		}
		updated = updated_multiplier != multiplier;
		propagated = propagated_multiplier != multiplier;
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_GT (updated_multiplier, multiplier);
	ASSERT_EQ (propagated_multiplier, updated_multiplier);
}

TEST (work_watcher, removed_after_win)
{
	futurehead::system system (1);
	auto & node (*system.nodes[0]);
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key;
	ASSERT_EQ (0, wallet.wallets.watcher->size ());
	auto const block1 (wallet.send_action (futurehead::test_genesis_key.pub, key.pub, 100));
	ASSERT_EQ (1, wallet.wallets.watcher->size ());
	system.deadline_set (5s);
	while (node.wallets.watcher->is_watched (block1->qualified_root ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (0, node.wallets.watcher->size ());
}

TEST (work_watcher, removed_after_lose)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.work_watcher_period = 1s;
	auto & node = *system.add_node (node_config);
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key;
	auto const block1 (wallet.send_action (futurehead::test_genesis_key.pub, key.pub, 100));
	ASSERT_TRUE (node.wallets.watcher->is_watched (block1->qualified_root ()));
	futurehead::genesis genesis;
	auto fork1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::xrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	node.process_active (fork1);
	node.block_processor.flush ();
	auto vote (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 0, fork1));
	futurehead::confirm_ack message (vote);
	node.network.process_message (message, nullptr);
	system.deadline_set (5s);
	while (node.wallets.watcher->is_watched (block1->qualified_root ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (0, node.wallets.watcher->size ());
}

TEST (work_watcher, generation_disabled)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.enable_voting = false;
	node_config.work_watcher_period = 1s;
	node_config.work_threads = 0;
	futurehead::node_flags node_flags;
	node_flags.disable_request_loop = true;
	auto & node = *system.add_node (node_config);
	ASSERT_FALSE (node.work_generation_enabled ());
	futurehead::work_pool pool (std::numeric_limits<unsigned>::max ());
	futurehead::genesis genesis;
	futurehead::keypair key;
	auto block (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Mxrb_ratio, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *pool.generate (genesis.hash ())));
	auto difficulty (block->difficulty ());
	node.wallets.watcher->add (block);
	ASSERT_FALSE (node.process_local (block).code != futurehead::process_result::progress);
	ASSERT_TRUE (node.wallets.watcher->is_watched (block->qualified_root ()));
	auto multiplier = futurehead::normalized_multiplier (futurehead::difficulty::to_multiplier (difficulty, futurehead::work_threshold (block->work_version (), futurehead::block_details (futurehead::epoch::epoch_0, true, false, false))), node.network_params.network.publish_thresholds.epoch_1);
	double updated_multiplier{ multiplier };
	{
		futurehead::lock_guard<std::mutex> guard (node.active.mutex);
		node.active.trended_active_multiplier = multiplier * 10;
	}
	std::this_thread::sleep_for (2s);
	ASSERT_TRUE (node.wallets.watcher->is_watched (block->qualified_root ()));
	{
		futurehead::lock_guard<std::mutex> guard (node.active.mutex);
		auto const existing (node.active.roots.find (block->qualified_root ()));
		ASSERT_NE (existing, node.active.roots.end ());
		updated_multiplier = existing->multiplier;
	}
	ASSERT_EQ (updated_multiplier, multiplier);
	ASSERT_TRUE (node.distributed_work.items.empty ());
}

TEST (work_watcher, cancel)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.work_watcher_period = 1s;
	node_config.max_work_generate_multiplier = 1e6;
	node_config.enable_voting = false;
	auto & node = *system.add_node (node_config);
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (futurehead::test_genesis_key.prv, false);
	futurehead::keypair key;
	auto work1 (node.work_generate_blocking (futurehead::test_genesis_key.pub));
	auto const block1 (wallet.send_action (futurehead::test_genesis_key.pub, key.pub, 100, *work1, false));
	{
		futurehead::unique_lock<std::mutex> lock (node.active.mutex);
		// Prevent active difficulty repopulating multipliers
		node.network_params.network.request_interval_ms = 10000;
		// Fill multipliers_cb and update active difficulty;
		for (auto i (0); i < node.active.multipliers_cb.size (); i++)
		{
			node.active.multipliers_cb.push_back (node.config.max_work_generate_multiplier);
		}
		node.active.update_active_multiplier (lock);
	}
	// Wait for work generation to start
	system.deadline_set (5s);
	while (0 == node.work.size ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Cancel the ongoing work
	ASSERT_EQ (1, node.work.size ());
	node.work.cancel (block1->root ());
	ASSERT_EQ (0, node.work.size ());
	{
		futurehead::unique_lock<std::mutex> lock (wallet.wallets.watcher->mutex);
		auto existing (wallet.wallets.watcher->watched.find (block1->qualified_root ()));
		ASSERT_NE (wallet.wallets.watcher->watched.end (), existing);
		auto block2 (existing->second);
		// Block must be the same
		ASSERT_NE (nullptr, block1);
		ASSERT_NE (nullptr, block2);
		ASSERT_EQ (*block1, *block2);
		// but should still be under watch
		lock.unlock ();
		ASSERT_TRUE (wallet.wallets.watcher->is_watched (block1->qualified_root ()));
	}
}

// Ensure the minimum limited difficulty is enough for the highest threshold
TEST (wallet, limited_difficulty)
{
	futurehead::system system;
	futurehead::genesis genesis;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.max_work_generate_multiplier = 1;
	futurehead::node_flags node_flags;
	node_flags.disable_request_loop = true;
	auto & node = *system.add_node (node_config, node_flags);
	auto & wallet (*system.wallet (0));
	// Upgrade the genesis account to epoch 2
	ASSERT_NE (nullptr, system.upgrade_genesis_epoch (node, futurehead::epoch::epoch_1));
	ASSERT_NE (nullptr, system.upgrade_genesis_epoch (node, futurehead::epoch::epoch_2));
	ASSERT_EQ (futurehead::epoch::epoch_2, node.store.block_version (node.store.tx_begin_read (), node.latest (futurehead::test_genesis_key.pub)));
	wallet.insert_adhoc (futurehead::test_genesis_key.prv, false);
	{
		// Force active difficulty to an impossibly high value
		futurehead::lock_guard<std::mutex> guard (node.active.mutex);
		node.active.trended_active_multiplier = 1024 * 1024 * 1024;
	}
	ASSERT_EQ (node.max_work_generate_difficulty (futurehead::work_version::work_1), node.active.limited_active_difficulty (*genesis.open));
	auto send = wallet.send_action (futurehead::test_genesis_key.pub, futurehead::keypair ().pub, 1, 1);
	ASSERT_NE (nullptr, send);
}

TEST (wallet, epoch_2_validation)
{
	futurehead::system system (1);
	auto & node (*system.nodes[0]);
	auto & wallet (*system.wallet (0));

	// Upgrade the genesis account to epoch 2
	ASSERT_NE (nullptr, system.upgrade_genesis_epoch (node, futurehead::epoch::epoch_1));
	ASSERT_NE (nullptr, system.upgrade_genesis_epoch (node, futurehead::epoch::epoch_2));

	wallet.insert_adhoc (futurehead::test_genesis_key.prv, false);

	// Test send and receive blocks
	// An epoch 2 receive block should be generated with lower difficulty with high probability
	auto tries = 0;
	auto max_tries = 20;
	auto amount = node.config.receive_minimum.number ();
	while (++tries < max_tries)
	{
		auto send = wallet.send_action (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.pub, amount, 1);
		ASSERT_NE (nullptr, send);

		auto receive = wallet.receive_action (*send, futurehead::test_genesis_key.pub, amount, 1);
		ASSERT_NE (nullptr, receive);
		if (receive->difficulty () < node.network_params.network.publish_thresholds.base)
		{
			ASSERT_GE (receive->difficulty (), node.network_params.network.publish_thresholds.epoch_2_receive);
			break;
		}
	}
	ASSERT_LT (tries, max_tries);

	// Test a change block
	ASSERT_NE (nullptr, wallet.change_action (futurehead::test_genesis_key.pub, futurehead::keypair ().pub, 1));
}

// Receiving from an upgraded account uses the lower threshold and upgrades the receiving account
TEST (wallet, epoch_2_receive_propagation)
{
	auto tries = 0;
	auto const max_tries = 20;
	while (++tries < max_tries)
	{
		futurehead::system system;
		futurehead::node_flags node_flags;
		node_flags.disable_request_loop = true;
		auto & node (*system.add_node (node_flags));
		auto & wallet (*system.wallet (0));

		// Upgrade the genesis account to epoch 1
		auto epoch1 = system.upgrade_genesis_epoch (node, futurehead::epoch::epoch_1);
		ASSERT_NE (nullptr, epoch1);

		futurehead::keypair key;
		futurehead::state_block_builder builder;

		// Send and open the account
		wallet.insert_adhoc (futurehead::test_genesis_key.prv, false);
		wallet.insert_adhoc (key.prv, false);
		auto amount = node.config.receive_minimum.number ();
		auto send1 = wallet.send_action (futurehead::test_genesis_key.pub, key.pub, amount, 1);
		ASSERT_NE (nullptr, send1);
		ASSERT_NE (nullptr, wallet.receive_action (*send1, futurehead::test_genesis_key.pub, amount, 1));

		// Upgrade the genesis account to epoch 2
		auto epoch2 = system.upgrade_genesis_epoch (node, futurehead::epoch::epoch_2);
		ASSERT_NE (nullptr, epoch2);

		// Send a block
		auto send2 = wallet.send_action (futurehead::test_genesis_key.pub, key.pub, amount, 1);
		ASSERT_NE (nullptr, send2);

		// Receiving should use the lower difficulty
		{
			futurehead::lock_guard<std::mutex> guard (node.active.mutex);
			node.active.trended_active_multiplier = 1.0;
		}
		auto receive2 = wallet.receive_action (*send2, key.pub, amount, 1);
		ASSERT_NE (nullptr, receive2);
		if (receive2->difficulty () < node.network_params.network.publish_thresholds.base)
		{
			ASSERT_GE (receive2->difficulty (), node.network_params.network.publish_thresholds.epoch_2_receive);
			ASSERT_EQ (futurehead::epoch::epoch_2, node.store.block_version (node.store.tx_begin_read (), receive2->hash ()));
			break;
		}
	}
	ASSERT_LT (tries, max_tries);
}

// Opening an upgraded account uses the lower threshold
TEST (wallet, epoch_2_receive_unopened)
{
	// Ensure the lower receive work is used when receiving
	auto tries = 0;
	auto const max_tries = 20;
	while (++tries < max_tries)
	{
		futurehead::system system;
		futurehead::node_flags node_flags;
		node_flags.disable_request_loop = true;
		auto & node (*system.add_node (node_flags));
		auto & wallet (*system.wallet (0));

		// Upgrade the genesis account to epoch 1
		auto epoch1 = system.upgrade_genesis_epoch (node, futurehead::epoch::epoch_1);
		ASSERT_NE (nullptr, epoch1);

		futurehead::keypair key;
		futurehead::state_block_builder builder;

		// Send
		wallet.insert_adhoc (futurehead::test_genesis_key.prv, false);
		auto amount = node.config.receive_minimum.number ();
		auto send1 = wallet.send_action (futurehead::test_genesis_key.pub, key.pub, amount, 1);

		// Upgrade unopened account to epoch_2
		auto epoch2_unopened = futurehead::state_block (key.pub, 0, 0, 0, node.network_params.ledger.epochs.link (futurehead::epoch::epoch_2), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (key.pub, node.network_params.network.publish_thresholds.epoch_2));
		ASSERT_EQ (futurehead::process_result::progress, node.process (epoch2_unopened).code);

		wallet.insert_adhoc (key.prv, false);

		// Receiving should use the lower difficulty
		{
			futurehead::lock_guard<std::mutex> guard (node.active.mutex);
			node.active.trended_active_multiplier = 1.0;
		}
		auto receive1 = wallet.receive_action (*send1, key.pub, amount, 1);
		ASSERT_NE (nullptr, receive1);
		if (receive1->difficulty () < node.network_params.network.publish_thresholds.base)
		{
			ASSERT_GE (receive1->difficulty (), node.network_params.network.publish_thresholds.epoch_2_receive);
			ASSERT_EQ (futurehead::epoch::epoch_2, node.store.block_version (node.store.tx_begin_read (), receive1->hash ()));
			break;
		}
	}
	ASSERT_LT (tries, max_tries);
}

TEST (wallet, foreach_representative_deadlock)
{
	futurehead::system system (1);
	auto & node (*system.nodes[0]);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	node.wallets.compute_reps ();
	ASSERT_EQ (1, node.wallets.reps ().voting);
	node.wallets.foreach_representative ([&node](futurehead::public_key const & pub, futurehead::raw_key const & prv) {
		if (node.wallets.mutex.try_lock ())
		{
			node.wallets.mutex.unlock ();
		}
		else
		{
			ASSERT_FALSE (true);
		}
	});
}
