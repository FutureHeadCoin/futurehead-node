#pragma once

#include <futurehead/lib/lmdbconfig.hpp>
#include <futurehead/lib/locks.hpp>
#include <futurehead/lib/work.hpp>
#include <futurehead/node/lmdb/lmdb.hpp>
#include <futurehead/node/lmdb/wallet_value.hpp>
#include <futurehead/node/openclwork.hpp>
#include <futurehead/secure/blockstore.hpp>
#include <futurehead/secure/common.hpp>

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_set>
namespace futurehead
{
class node;
class node_config;
class wallets;
// The fan spreads a key out over the heap to decrease the likelihood of it being recovered by memory inspection
class fan final
{
public:
	fan (futurehead::uint256_union const &, size_t);
	void value (futurehead::raw_key &);
	void value_set (futurehead::raw_key const &);
	std::vector<std::unique_ptr<futurehead::uint256_union>> values;

private:
	std::mutex mutex;
	void value_get (futurehead::raw_key &);
};
class kdf final
{
public:
	void phs (futurehead::raw_key &, std::string const &, futurehead::uint256_union const &);
	std::mutex mutex;
};
enum class key_type
{
	not_a_type,
	unknown,
	adhoc,
	deterministic
};
class wallet_store final
{
public:
	wallet_store (bool &, futurehead::kdf &, futurehead::transaction &, futurehead::account, unsigned, std::string const &);
	wallet_store (bool &, futurehead::kdf &, futurehead::transaction &, futurehead::account, unsigned, std::string const &, std::string const &);
	std::vector<futurehead::account> accounts (futurehead::transaction const &);
	void initialize (futurehead::transaction const &, bool &, std::string const &);
	futurehead::uint256_union check (futurehead::transaction const &);
	bool rekey (futurehead::transaction const &, std::string const &);
	bool valid_password (futurehead::transaction const &);
	bool valid_public_key (futurehead::public_key const &);
	bool attempt_password (futurehead::transaction const &, std::string const &);
	void wallet_key (futurehead::raw_key &, futurehead::transaction const &);
	void seed (futurehead::raw_key &, futurehead::transaction const &);
	void seed_set (futurehead::transaction const &, futurehead::raw_key const &);
	futurehead::key_type key_type (futurehead::wallet_value const &);
	futurehead::public_key deterministic_insert (futurehead::transaction const &);
	futurehead::public_key deterministic_insert (futurehead::transaction const &, uint32_t const);
	futurehead::private_key deterministic_key (futurehead::transaction const &, uint32_t);
	uint32_t deterministic_index_get (futurehead::transaction const &);
	void deterministic_index_set (futurehead::transaction const &, uint32_t);
	void deterministic_clear (futurehead::transaction const &);
	futurehead::uint256_union salt (futurehead::transaction const &);
	bool is_representative (futurehead::transaction const &);
	futurehead::account representative (futurehead::transaction const &);
	void representative_set (futurehead::transaction const &, futurehead::account const &);
	futurehead::public_key insert_adhoc (futurehead::transaction const &, futurehead::raw_key const &);
	bool insert_watch (futurehead::transaction const &, futurehead::account const &);
	void erase (futurehead::transaction const &, futurehead::account const &);
	futurehead::wallet_value entry_get_raw (futurehead::transaction const &, futurehead::account const &);
	void entry_put_raw (futurehead::transaction const &, futurehead::account const &, futurehead::wallet_value const &);
	bool fetch (futurehead::transaction const &, futurehead::account const &, futurehead::raw_key &);
	bool exists (futurehead::transaction const &, futurehead::account const &);
	void destroy (futurehead::transaction const &);
	futurehead::store_iterator<futurehead::account, futurehead::wallet_value> find (futurehead::transaction const &, futurehead::account const &);
	futurehead::store_iterator<futurehead::account, futurehead::wallet_value> begin (futurehead::transaction const &, futurehead::account const &);
	futurehead::store_iterator<futurehead::account, futurehead::wallet_value> begin (futurehead::transaction const &);
	futurehead::store_iterator<futurehead::account, futurehead::wallet_value> end ();
	void derive_key (futurehead::raw_key &, futurehead::transaction const &, std::string const &);
	void serialize_json (futurehead::transaction const &, std::string &);
	void write_backup (futurehead::transaction const &, boost::filesystem::path const &);
	bool move (futurehead::transaction const &, futurehead::wallet_store &, std::vector<futurehead::public_key> const &);
	bool import (futurehead::transaction const &, futurehead::wallet_store &);
	bool work_get (futurehead::transaction const &, futurehead::public_key const &, uint64_t &);
	void work_put (futurehead::transaction const &, futurehead::public_key const &, uint64_t);
	unsigned version (futurehead::transaction const &);
	void version_put (futurehead::transaction const &, unsigned);
	void upgrade_v1_v2 (futurehead::transaction const &);
	void upgrade_v2_v3 (futurehead::transaction const &);
	void upgrade_v3_v4 (futurehead::transaction const &);
	futurehead::fan password;
	futurehead::fan wallet_key_mem;
	static unsigned const version_1 = 1;
	static unsigned const version_2 = 2;
	static unsigned const version_3 = 3;
	static unsigned const version_4 = 4;
	static unsigned constexpr version_current = version_4;
	static futurehead::account const version_special;
	static futurehead::account const wallet_key_special;
	static futurehead::account const salt_special;
	static futurehead::account const check_special;
	static futurehead::account const representative_special;
	static futurehead::account const seed_special;
	static futurehead::account const deterministic_index_special;
	static size_t const check_iv_index;
	static size_t const seed_iv_index;
	static int const special_count;
	futurehead::kdf & kdf;
	std::atomic<MDB_dbi> handle{ 0 };
	std::recursive_mutex mutex;

private:
	MDB_txn * tx (futurehead::transaction const &) const;
};
// A wallet is a set of account keys encrypted by a common encryption key
class wallet final : public std::enable_shared_from_this<futurehead::wallet>
{
public:
	std::shared_ptr<futurehead::block> change_action (futurehead::account const &, futurehead::account const &, uint64_t = 0, bool = true);
	std::shared_ptr<futurehead::block> receive_action (futurehead::block const &, futurehead::account const &, futurehead::uint128_union const &, uint64_t = 0, bool = true);
	std::shared_ptr<futurehead::block> send_action (futurehead::account const &, futurehead::account const &, futurehead::uint128_t const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	bool action_complete (std::shared_ptr<futurehead::block> const &, futurehead::account const &, bool const, futurehead::block_details const &);
	wallet (bool &, futurehead::transaction &, futurehead::wallets &, std::string const &);
	wallet (bool &, futurehead::transaction &, futurehead::wallets &, std::string const &, std::string const &);
	void enter_initial_password ();
	bool enter_password (futurehead::transaction const &, std::string const &);
	futurehead::public_key insert_adhoc (futurehead::raw_key const &, bool = true);
	futurehead::public_key insert_adhoc (futurehead::transaction const &, futurehead::raw_key const &, bool = true);
	bool insert_watch (futurehead::transaction const &, futurehead::public_key const &);
	futurehead::public_key deterministic_insert (futurehead::transaction const &, bool = true);
	futurehead::public_key deterministic_insert (uint32_t, bool = true);
	futurehead::public_key deterministic_insert (bool = true);
	bool exists (futurehead::public_key const &);
	bool import (std::string const &, std::string const &);
	void serialize (std::string &);
	bool change_sync (futurehead::account const &, futurehead::account const &);
	void change_async (futurehead::account const &, futurehead::account const &, std::function<void(std::shared_ptr<futurehead::block>)> const &, uint64_t = 0, bool = true);
	bool receive_sync (std::shared_ptr<futurehead::block>, futurehead::account const &, futurehead::uint128_t const &);
	void receive_async (std::shared_ptr<futurehead::block>, futurehead::account const &, futurehead::uint128_t const &, std::function<void(std::shared_ptr<futurehead::block>)> const &, uint64_t = 0, bool = true);
	futurehead::block_hash send_sync (futurehead::account const &, futurehead::account const &, futurehead::uint128_t const &);
	void send_async (futurehead::account const &, futurehead::account const &, futurehead::uint128_t const &, std::function<void(std::shared_ptr<futurehead::block>)> const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	void work_cache_blocking (futurehead::account const &, futurehead::root const &);
	void work_update (futurehead::transaction const &, futurehead::account const &, futurehead::root const &, uint64_t);
	// Schedule work generation after a few seconds
	void work_ensure (futurehead::account const &, futurehead::root const &);
	bool search_pending ();
	void init_free_accounts (futurehead::transaction const &);
	uint32_t deterministic_check (futurehead::transaction const & transaction_a, uint32_t index);
	/** Changes the wallet seed and returns the first account */
	futurehead::public_key change_seed (futurehead::transaction const & transaction_a, futurehead::raw_key const & prv_a, uint32_t count = 0);
	void deterministic_restore (futurehead::transaction const & transaction_a);
	bool live ();
	futurehead::network_params network_params;
	std::unordered_set<futurehead::account> free_accounts;
	std::function<void(bool, bool)> lock_observer;
	futurehead::wallet_store store;
	futurehead::wallets & wallets;
	std::mutex representatives_mutex;
	std::unordered_set<futurehead::account> representatives;
};

class work_watcher final : public std::enable_shared_from_this<futurehead::work_watcher>
{
public:
	work_watcher (futurehead::node &);
	~work_watcher ();
	void stop ();
	void add (std::shared_ptr<futurehead::block>);
	void update (futurehead::qualified_root const &, std::shared_ptr<futurehead::state_block>);
	void watching (futurehead::qualified_root const &, std::shared_ptr<futurehead::state_block>);
	void remove (futurehead::block const &);
	bool is_watched (futurehead::qualified_root const &);
	size_t size ();
	std::mutex mutex;
	futurehead::node & node;
	std::unordered_map<futurehead::qualified_root, std::shared_ptr<futurehead::state_block>> watched;
	std::atomic<bool> stopped;
};

class wallet_representatives
{
public:
	uint64_t voting{ 0 }; // Number of representatives with at least the configured minimum voting weight
	uint64_t half_principal{ 0 }; // Number of representatives with at least 50% of principal representative requirements
	std::unordered_set<futurehead::account> accounts; // Representatives with at least the configured minimum voting weight
	bool have_half_rep () const
	{
		return half_principal > 0;
	}
	bool exists (futurehead::account const & rep_a) const
	{
		return accounts.count (rep_a) > 0;
	}
	void clear ()
	{
		voting = 0;
		half_principal = 0;
		accounts.clear ();
	}
};

/**
 * The wallets set is all the wallets a node controls.
 * A node may contain multiple wallets independently encrypted and operated.
 */
class wallets final
{
public:
	wallets (bool, futurehead::node &);
	~wallets ();
	std::shared_ptr<futurehead::wallet> open (futurehead::wallet_id const &);
	std::shared_ptr<futurehead::wallet> create (futurehead::wallet_id const &);
	bool search_pending (futurehead::wallet_id const &);
	void search_pending_all ();
	void destroy (futurehead::wallet_id const &);
	void reload ();
	void do_wallet_actions ();
	void queue_wallet_action (futurehead::uint128_t const &, std::shared_ptr<futurehead::wallet>, std::function<void(futurehead::wallet &)> const &);
	void foreach_representative (std::function<void(futurehead::public_key const &, futurehead::raw_key const &)> const &);
	bool exists (futurehead::transaction const &, futurehead::account const &);
	void stop ();
	void clear_send_ids (futurehead::transaction const &);
	futurehead::wallet_representatives reps () const;
	bool check_rep (futurehead::account const &, futurehead::uint128_t const &, const bool = true);
	void compute_reps ();
	void ongoing_compute_reps ();
	void split_if_needed (futurehead::transaction &, futurehead::block_store &);
	void move_table (std::string const &, MDB_txn *, MDB_txn *);
	futurehead::network_params network_params;
	std::function<void(bool)> observer;
	std::unordered_map<futurehead::wallet_id, std::shared_ptr<futurehead::wallet>> items;
	std::multimap<futurehead::uint128_t, std::pair<std::shared_ptr<futurehead::wallet>, std::function<void(futurehead::wallet &)>>, std::greater<futurehead::uint128_t>> actions;
	futurehead::locked<std::unordered_map<futurehead::account, futurehead::root>> delayed_work;
	std::mutex mutex;
	std::mutex action_mutex;
	futurehead::condition_variable condition;
	futurehead::kdf kdf;
	MDB_dbi handle;
	MDB_dbi send_action_ids;
	futurehead::node & node;
	futurehead::mdb_env & env;
	std::atomic<bool> stopped;
	std::shared_ptr<futurehead::work_watcher> watcher;
	std::thread thread;
	static futurehead::uint128_t const generate_priority;
	static futurehead::uint128_t const high_priority;
	/** Start read-write transaction */
	futurehead::write_transaction tx_begin_write ();

	/** Start read-only transaction */
	futurehead::read_transaction tx_begin_read ();

private:
	mutable std::mutex reps_cache_mutex;
	futurehead::wallet_representatives representatives;
};

std::unique_ptr<container_info_component> collect_container_info (wallets & wallets, const std::string & name);

class wallets_store
{
public:
	virtual ~wallets_store () = default;
	virtual bool init_error () const = 0;
};
class mdb_wallets_store final : public wallets_store
{
public:
	mdb_wallets_store (boost::filesystem::path const &, futurehead::lmdb_config const & lmdb_config_a = futurehead::lmdb_config{});
	futurehead::mdb_env environment;
	bool init_error () const override;
	bool error{ false };
};
}
