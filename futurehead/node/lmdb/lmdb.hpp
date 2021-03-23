#pragma once

#include <futurehead/lib/diagnosticsconfig.hpp>
#include <futurehead/lib/lmdbconfig.hpp>
#include <futurehead/lib/logger_mt.hpp>
#include <futurehead/lib/numbers.hpp>
#include <futurehead/node/lmdb/lmdb_env.hpp>
#include <futurehead/node/lmdb/lmdb_iterator.hpp>
#include <futurehead/node/lmdb/lmdb_txn.hpp>
#include <futurehead/secure/blockstore_partial.hpp>
#include <futurehead/secure/common.hpp>
#include <futurehead/secure/versioning.hpp>

#include <boost/optional.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace boost
{
namespace filesystem
{
	class path;
}
}

namespace futurehead
{
using mdb_val = db_val<MDB_val>;

class logging_mt;
/**
 * mdb implementation of the block store
 */
class mdb_store : public block_store_partial<MDB_val, mdb_store>
{
public:
	using block_store_partial::block_exists;
	using block_store_partial::unchecked_put;

	mdb_store (futurehead::logger_mt &, boost::filesystem::path const &, futurehead::txn_tracking_config const & txn_tracking_config_a = futurehead::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), futurehead::lmdb_config const & lmdb_config_a = futurehead::lmdb_config{}, size_t batch_size = 512, bool backup_before_upgrade = false);
	futurehead::write_transaction tx_begin_write (std::vector<futurehead::tables> const & tables_requiring_lock = {}, std::vector<futurehead::tables> const & tables_no_lock = {}) override;
	futurehead::read_transaction tx_begin_read () override;

	std::string vendor_get () const override;

	bool block_info_get (futurehead::transaction const &, futurehead::block_hash const &, futurehead::block_info &) const override;

	void version_put (futurehead::write_transaction const &, int) override;

	void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) override;

	static void create_backup_file (futurehead::mdb_env &, boost::filesystem::path const &, futurehead::logger_mt &);

private:
	futurehead::logger_mt & logger;
	bool error{ false };

public:
	futurehead::mdb_env env;

	/**
	 * Maps head block to owning account
	 * futurehead::block_hash -> futurehead::account
	 */
	MDB_dbi frontiers{ 0 };

	/**
	 * Maps account v1 to account information, head, rep, open, balance, timestamp and block count. (Removed)
	 * futurehead::account -> futurehead::block_hash, futurehead::block_hash, futurehead::block_hash, futurehead::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v0{ 0 };

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp and block count. (Removed)
	 * futurehead::account -> futurehead::block_hash, futurehead::block_hash, futurehead::block_hash, futurehead::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v1{ 0 };

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp, block count and epoch. (Removed)
	 * futurehead::account -> futurehead::block_hash, futurehead::block_hash, futurehead::block_hash, futurehead::amount, uint64_t, uint64_t, futurehead::epoch
	 */
	MDB_dbi accounts{ 0 };

	/**
	 * Maps block hash to send block.
	 * futurehead::block_hash -> futurehead::send_block
	 */
	MDB_dbi send_blocks{ 0 };

	/**
	 * Maps block hash to receive block.
	 * futurehead::block_hash -> futurehead::receive_block
	 */
	MDB_dbi receive_blocks{ 0 };

	/**
	 * Maps block hash to open block.
	 * futurehead::block_hash -> futurehead::open_block
	 */
	MDB_dbi open_blocks{ 0 };

	/**
	 * Maps block hash to change block.
	 * futurehead::block_hash -> futurehead::change_block
	 */
	MDB_dbi change_blocks{ 0 };

	/**
	 * Maps block hash to v0 state block. (Removed)
	 * futurehead::block_hash -> futurehead::state_block
	 */
	MDB_dbi state_blocks_v0{ 0 };

	/**
	 * Maps block hash to v1 state block. (Removed)
	 * futurehead::block_hash -> futurehead::state_block
	 */
	MDB_dbi state_blocks_v1{ 0 };

	/**
	 * Maps block hash to state block.
	 * futurehead::block_hash -> futurehead::state_block
	 */
	MDB_dbi state_blocks{ 0 };

	/**
	 * Maps min_version 0 (destination account, pending block) to (source account, amount). (Removed)
	 * futurehead::account, futurehead::block_hash -> futurehead::account, futurehead::amount
	 */
	MDB_dbi pending_v0{ 0 };

	/**
	 * Maps min_version 1 (destination account, pending block) to (source account, amount). (Removed)
	 * futurehead::account, futurehead::block_hash -> futurehead::account, futurehead::amount
	 */
	MDB_dbi pending_v1{ 0 };

	/**
	 * Maps (destination account, pending block) to (source account, amount, version). (Removed)
	 * futurehead::account, futurehead::block_hash -> futurehead::account, futurehead::amount, futurehead::epoch
	 */
	MDB_dbi pending{ 0 };

	/**
	 * Maps block hash to account and balance. (Removed)
	 * block_hash -> futurehead::account, futurehead::amount
	 */
	MDB_dbi blocks_info{ 0 };

	/**
	 * Representative weights. (Removed)
	 * futurehead::account -> futurehead::uint128_t
	 */
	MDB_dbi representation{ 0 };

	/**
	 * Unchecked bootstrap blocks info.
	 * futurehead::block_hash -> futurehead::unchecked_info
	 */
	MDB_dbi unchecked{ 0 };

	/**
	 * Highest vote observed for account.
	 * futurehead::account -> uint64_t
	 */
	MDB_dbi vote{ 0 };

	/**
	 * Samples of online vote weight
	 * uint64_t -> futurehead::amount
	 */
	MDB_dbi online_weight{ 0 };

	/**
	 * Meta information about block store, such as versions.
	 * futurehead::uint256_union (arbitrary key) -> blob
	 */
	MDB_dbi meta{ 0 };

	/*
	 * Endpoints for peers
	 * futurehead::endpoint_key -> no_value
	*/
	MDB_dbi peers{ 0 };

	/*
	 * Confirmation height of an account, and the hash for the block at that height
	 * futurehead::account -> uint64_t, futurehead::block_hash
	 */
	MDB_dbi confirmation_height{ 0 };

	bool exists (futurehead::transaction const & transaction_a, tables table_a, futurehead::mdb_val const & key_a) const;

	int get (futurehead::transaction const & transaction_a, tables table_a, futurehead::mdb_val const & key_a, futurehead::mdb_val & value_a) const;
	int put (futurehead::write_transaction const & transaction_a, tables table_a, futurehead::mdb_val const & key_a, const futurehead::mdb_val & value_a) const;
	int del (futurehead::write_transaction const & transaction_a, tables table_a, futurehead::mdb_val const & key_a) const;

	bool copy_db (boost::filesystem::path const & destination_file) override;
	void rebuild_db (futurehead::write_transaction const & transaction_a) override;

	template <typename Key, typename Value>
	futurehead::store_iterator<Key, Value> make_iterator (futurehead::transaction const & transaction_a, tables table_a) const
	{
		return futurehead::store_iterator<Key, Value> (std::make_unique<futurehead::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a)));
	}

	template <typename Key, typename Value>
	futurehead::store_iterator<Key, Value> make_iterator (futurehead::transaction const & transaction_a, tables table_a, futurehead::mdb_val const & key) const
	{
		return futurehead::store_iterator<Key, Value> (std::make_unique<futurehead::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a), key));
	}

	bool init_error () const override;

	size_t count (futurehead::transaction const &, MDB_dbi) const;

	// These are only use in the upgrade process.
	std::shared_ptr<futurehead::block> block_get_v14 (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a, futurehead::block_sideband_v14 * sideband_a = nullptr, bool * is_state_v1 = nullptr) const override;
	bool entry_has_sideband_v14 (size_t entry_size_a, futurehead::block_type type_a) const;
	size_t block_successor_offset_v14 (futurehead::transaction const & transaction_a, size_t entry_size_a, futurehead::block_type type_a) const;
	futurehead::block_hash block_successor_v14 (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const;
	futurehead::mdb_val block_raw_get_v14 (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a, futurehead::block_type & type_a, bool * is_state_v1 = nullptr) const;
	boost::optional<futurehead::mdb_val> block_raw_get_by_type_v14 (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a, futurehead::block_type & type_a, bool * is_state_v1) const;
	futurehead::account block_account_computed_v14 (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const;
	futurehead::account block_account_v14 (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const;
	futurehead::uint128_t block_balance_computed_v14 (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const;

private:
	bool do_upgrades (futurehead::write_transaction &, bool &, size_t);
	void upgrade_v1_to_v2 (futurehead::write_transaction const &);
	void upgrade_v2_to_v3 (futurehead::write_transaction const &);
	void upgrade_v3_to_v4 (futurehead::write_transaction const &);
	void upgrade_v4_to_v5 (futurehead::write_transaction const &);
	void upgrade_v5_to_v6 (futurehead::write_transaction const &);
	void upgrade_v6_to_v7 (futurehead::write_transaction const &);
	void upgrade_v7_to_v8 (futurehead::write_transaction const &);
	void upgrade_v8_to_v9 (futurehead::write_transaction const &);
	void upgrade_v10_to_v11 (futurehead::write_transaction const &);
	void upgrade_v11_to_v12 (futurehead::write_transaction const &);
	void upgrade_v12_to_v13 (futurehead::write_transaction &, size_t);
	void upgrade_v13_to_v14 (futurehead::write_transaction const &);
	void upgrade_v14_to_v15 (futurehead::write_transaction &);
	void upgrade_v15_to_v16 (futurehead::write_transaction const &);
	void upgrade_v16_to_v17 (futurehead::write_transaction const &);
	void upgrade_v17_to_v18 (futurehead::write_transaction const &);

	void open_databases (bool &, futurehead::transaction const &, unsigned);

	int drop (futurehead::write_transaction const & transaction_a, tables table_a) override;
	int clear (futurehead::write_transaction const & transaction_a, MDB_dbi handle_a);

	bool not_found (int status) const override;
	bool success (int status) const override;
	int status_code_not_found () const override;

	MDB_dbi table_to_dbi (tables table_a) const;

	futurehead::mdb_txn_tracker mdb_txn_tracker;
	futurehead::mdb_txn_callbacks create_txn_callbacks ();
	bool txn_tracking_enabled;

	size_t count (futurehead::transaction const & transaction_a, tables table_a) const override;

	bool vacuum_after_upgrade (boost::filesystem::path const & path_a, futurehead::lmdb_config const & lmdb_config_a);

	class upgrade_counters
	{
	public:
		upgrade_counters (uint64_t count_before_v0, uint64_t count_before_v1);
		bool are_equal () const;

		uint64_t before_v0;
		uint64_t before_v1;
		uint64_t after_v0{ 0 };
		uint64_t after_v1{ 0 };
	};
};

template <>
void * mdb_val::data () const;
template <>
size_t mdb_val::size () const;
template <>
mdb_val::db_val (size_t size_a, void * data_a);
template <>
void mdb_val::convert_buffer_to_value ();

extern template class block_store_partial<MDB_val, mdb_store>;
}
