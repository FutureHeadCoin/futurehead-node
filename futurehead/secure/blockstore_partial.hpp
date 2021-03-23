#pragma once

#include <futurehead/lib/rep_weights.hpp>
#include <futurehead/secure/blockstore.hpp>
#include <futurehead/secure/buffer.hpp>

#include <crypto/cryptopp/words.h>

namespace futurehead
{
template <typename Val, typename Derived_Store>
class block_predecessor_set;

/** This base class implements the block_store interface functions which have DB agnostic functionality */
template <typename Val, typename Derived_Store>
class block_store_partial : public block_store
{
public:
	using block_store::block_exists;
	using block_store::unchecked_put;

	friend class futurehead::block_predecessor_set<Val, Derived_Store>;

	std::mutex cache_mutex;

	/**
	 * If using a different store version than the latest then you may need
	 * to modify some of the objects in the store to be appropriate for the version before an upgrade.
	 */
	void initialize (futurehead::write_transaction const & transaction_a, futurehead::genesis const & genesis_a, futurehead::ledger_cache & ledger_cache_a) override
	{
		auto hash_l (genesis_a.hash ());
		debug_assert (latest_begin (transaction_a) == latest_end ());
		genesis_a.open->sideband_set (futurehead::block_sideband (network_params.ledger.genesis_account, 0, network_params.ledger.genesis_amount, 1, futurehead::seconds_since_epoch (), futurehead::epoch::epoch_0, false, false, false));
		block_put (transaction_a, hash_l, *genesis_a.open);
		++ledger_cache_a.block_count;
		confirmation_height_put (transaction_a, network_params.ledger.genesis_account, futurehead::confirmation_height_info{ 1, genesis_a.hash () });
		++ledger_cache_a.cemented_count;
		//TO-CHANGE FIRST BLOCK AMOUNT DIVIDE 3.4 BILLION BY THE AMOUNT YOU NEED
		account_put (transaction_a, network_params.ledger.genesis_account, { hash_l, network_params.ledger.genesis_account, genesis_a.open->hash (), std::numeric_limits<futurehead::uint128_t>::max (), futurehead::seconds_since_epoch (), 1, futurehead::epoch::epoch_0 });
		++ledger_cache_a.account_count;
		ledger_cache_a.rep_weights.representation_put (network_params.ledger.genesis_account, std::numeric_limits<futurehead::uint128_t>::max ());
		frontier_put (transaction_a, hash_l, network_params.ledger.genesis_account);
	}

	futurehead::uint128_t block_balance (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) override
	{
		auto block (block_get (transaction_a, hash_a));
		release_assert (block);
		futurehead::uint128_t result (block_balance_calculated (block));
		return result;
	}

	bool account_exists (futurehead::transaction const & transaction_a, futurehead::account const & account_a) override
	{
		auto iterator (latest_begin (transaction_a, account_a));
		return iterator != latest_end () && futurehead::account (iterator->first) == account_a;
	}

	void confirmation_height_clear (futurehead::write_transaction const & transaction_a, futurehead::account const & account_a, uint64_t existing_confirmation_height_a) override
	{
		if (existing_confirmation_height_a > 0)
		{
			confirmation_height_put (transaction_a, account_a, { 0, futurehead::block_hash{ 0 } });
		}
	}

	void confirmation_height_clear (futurehead::write_transaction const & transaction_a) override
	{
		for (auto i (confirmation_height_begin (transaction_a)), n (confirmation_height_end ()); i != n; ++i)
		{
			confirmation_height_clear (transaction_a, i->first, i->second.height);
		}
	}

	bool pending_exists (futurehead::transaction const & transaction_a, futurehead::pending_key const & key_a) override
	{
		auto iterator (pending_begin (transaction_a, key_a));
		return iterator != pending_end () && futurehead::pending_key (iterator->first) == key_a;
	}

	bool pending_any (futurehead::transaction const & transaction_a, futurehead::account const & account_a) override
	{
		auto iterator (pending_begin (transaction_a, futurehead::pending_key (account_a, 0)));
		return iterator != pending_end () && futurehead::pending_key (iterator->first).account == account_a;
	}

	bool unchecked_exists (futurehead::transaction const & transaction_a, futurehead::unchecked_key const & unchecked_key_a) override
	{
		futurehead::db_val<Val> value;
		auto status (get (transaction_a, tables::unchecked, futurehead::db_val<Val> (unchecked_key_a), value));
		release_assert (success (status) || not_found (status));
		return (success (status));
	}

	std::vector<futurehead::unchecked_info> unchecked_get (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) override
	{
		std::vector<futurehead::unchecked_info> result;
		for (auto i (unchecked_begin (transaction_a, futurehead::unchecked_key (hash_a, 0))), n (unchecked_end ()); i != n && i->first.key () == hash_a; ++i)
		{
			futurehead::unchecked_info const & unchecked_info (i->second);
			result.push_back (unchecked_info);
		}
		return result;
	}

	void block_put (futurehead::write_transaction const & transaction_a, futurehead::block_hash const & hash_a, futurehead::block const & block_a) override
	{
		debug_assert (block_a.sideband ().successor.is_zero () || block_exists (transaction_a, block_a.sideband ().successor));
		std::vector<uint8_t> vector;
		{
			futurehead::vectorstream stream (vector);
			block_a.serialize (stream);
			block_a.sideband ().serialize (stream, block_a.type ());
		}
		block_raw_put (transaction_a, vector, block_a.type (), hash_a);
		futurehead::block_predecessor_set<Val, Derived_Store> predecessor (transaction_a, *this);
		block_a.visit (predecessor);
		debug_assert (block_a.previous ().is_zero () || block_successor (transaction_a, block_a.previous ()) == hash_a);
	}

	// Converts a block hash to a block height
	uint64_t block_account_height (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const override
	{
		auto block = block_get (transaction_a, hash_a);
		debug_assert (block != nullptr);
		return block->sideband ().height;
	}

	std::shared_ptr<futurehead::block> block_get (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const override
	{
		futurehead::block_type type;
		auto value (block_raw_get (transaction_a, hash_a, type));
		std::shared_ptr<futurehead::block> result;
		if (value.size () != 0)
		{
			futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = futurehead::deserialize_block (stream, type);
			debug_assert (result != nullptr);
			futurehead::block_sideband sideband;
			if (full_sideband (transaction_a) || entry_has_sideband (value.size (), type))
			{
				auto error (sideband.deserialize (stream, type));
				(void)error;
				debug_assert (!error);
			}
			else
			{
				// Reconstruct sideband data for block.
				sideband.account = block_account_computed (transaction_a, hash_a);
				sideband.balance = block_balance_computed (transaction_a, hash_a);
				sideband.successor = block_successor (transaction_a, hash_a);
				sideband.height = 0;
				sideband.timestamp = 0;
			}
			result->sideband_set (sideband);
		}
		return result;
	}

	std::shared_ptr<futurehead::block> block_get_no_sideband (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const override
	{
		futurehead::block_type type;
		auto value (block_raw_get (transaction_a, hash_a, type));
		std::shared_ptr<futurehead::block> result;
		if (value.size () != 0)
		{
			futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = futurehead::deserialize_block (stream, type);
			debug_assert (result != nullptr);
		}
		return result;
	}

	bool block_exists (futurehead::transaction const & transaction_a, futurehead::block_type type, futurehead::block_hash const & hash_a) override
	{
		auto junk = block_raw_get_by_type (transaction_a, hash_a, type);
		return junk.is_initialized ();
	}

	bool block_exists (futurehead::transaction const & tx_a, futurehead::block_hash const & hash_a) override
	{
		// Table lookups are ordered by match probability
		// clang-format off
		return
			block_exists (tx_a, futurehead::block_type::state, hash_a) ||
			block_exists (tx_a, futurehead::block_type::send, hash_a) ||
			block_exists (tx_a, futurehead::block_type::receive, hash_a) ||
			block_exists (tx_a, futurehead::block_type::open, hash_a) ||
			block_exists (tx_a, futurehead::block_type::change, hash_a);
		// clang-format on
	}

	bool root_exists (futurehead::transaction const & transaction_a, futurehead::root const & root_a) override
	{
		return block_exists (transaction_a, root_a) || account_exists (transaction_a, root_a);
	}

	bool source_exists (futurehead::transaction const & transaction_a, futurehead::block_hash const & source_a) override
	{
		return block_exists (transaction_a, futurehead::block_type::state, source_a) || block_exists (transaction_a, futurehead::block_type::send, source_a);
	}

	futurehead::account block_account (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const override
	{
		auto block (block_get (transaction_a, hash_a));
		debug_assert (block != nullptr);
		return block_account_calculated (*block);
	}

	futurehead::account block_account_calculated (futurehead::block const & block_a) const override
	{
		debug_assert (block_a.has_sideband ());
		futurehead::account result (block_a.account ());
		if (result.is_zero ())
		{
			result = block_a.sideband ().account;
		}
		debug_assert (!result.is_zero ());
		return result;
	}

	futurehead::uint128_t block_balance_calculated (std::shared_ptr<futurehead::block> const & block_a) const override
	{
		futurehead::uint128_t result;
		switch (block_a->type ())
		{
			case futurehead::block_type::open:
			case futurehead::block_type::receive:
			case futurehead::block_type::change:
				result = block_a->sideband ().balance.number ();
				break;
			case futurehead::block_type::send:
				result = boost::polymorphic_downcast<futurehead::send_block *> (block_a.get ())->hashables.balance.number ();
				break;
			case futurehead::block_type::state:
				result = boost::polymorphic_downcast<futurehead::state_block *> (block_a.get ())->hashables.balance.number ();
				break;
			case futurehead::block_type::invalid:
			case futurehead::block_type::not_a_block:
				release_assert (false);
				break;
		}
		return result;
	}

	futurehead::block_hash block_successor (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const override
	{
		futurehead::block_type type;
		auto value (block_raw_get (transaction_a, hash_a, type));
		futurehead::block_hash result;
		if (value.size () != 0)
		{
			debug_assert (value.size () >= result.bytes.size ());
			futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()) + block_successor_offset (transaction_a, value.size (), type), result.bytes.size ());
			auto error (futurehead::try_read (stream, result.bytes));
			(void)error;
			debug_assert (!error);
		}
		else
		{
			result.clear ();
		}
		return result;
	}

	bool full_sideband (futurehead::transaction const & transaction_a) const
	{
		return version_get (transaction_a) > 12;
	}

	void block_successor_clear (futurehead::write_transaction const & transaction_a, futurehead::block_hash const & hash_a) override
	{
		futurehead::block_type type;
		auto value (block_raw_get (transaction_a, hash_a, type));
		debug_assert (value.size () != 0);
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
		std::fill_n (data.begin () + block_successor_offset (transaction_a, value.size (), type), sizeof (futurehead::block_hash), uint8_t{ 0 });
		block_raw_put (transaction_a, data, type, hash_a);
	}

	void unchecked_put (futurehead::write_transaction const & transaction_a, futurehead::block_hash const & hash_a, std::shared_ptr<futurehead::block> const & block_a) override
	{
		futurehead::unchecked_key key (hash_a, block_a->hash ());
		futurehead::unchecked_info info (block_a, block_a->account (), futurehead::seconds_since_epoch (), futurehead::signature_verification::unknown);
		unchecked_put (transaction_a, key, info);
	}

	std::shared_ptr<futurehead::vote> vote_current (futurehead::transaction const & transaction_a, futurehead::account const & account_a) override
	{
		debug_assert (!cache_mutex.try_lock ());
		std::shared_ptr<futurehead::vote> result;
		auto existing (vote_cache_l1.find (account_a));
		auto have_existing (true);
		if (existing == vote_cache_l1.end ())
		{
			existing = vote_cache_l2.find (account_a);
			if (existing == vote_cache_l2.end ())
			{
				have_existing = false;
			}
		}
		if (have_existing)
		{
			result = existing->second;
		}
		else
		{
			result = vote_get (transaction_a, account_a);
		}
		return result;
	}

	std::shared_ptr<futurehead::vote> vote_generate (futurehead::transaction const & transaction_a, futurehead::account const & account_a, futurehead::raw_key const & key_a, std::shared_ptr<futurehead::block> block_a) override
	{
		futurehead::lock_guard<std::mutex> lock (cache_mutex);
		auto result (vote_current (transaction_a, account_a));
		uint64_t sequence ((result ? result->sequence : 0) + 1);
		result = std::make_shared<futurehead::vote> (account_a, key_a, sequence, block_a);
		vote_cache_l1[account_a] = result;
		return result;
	}

	std::shared_ptr<futurehead::vote> vote_generate (futurehead::transaction const & transaction_a, futurehead::account const & account_a, futurehead::raw_key const & key_a, std::vector<futurehead::block_hash> blocks_a) override
	{
		futurehead::lock_guard<std::mutex> lock (cache_mutex);
		auto result (vote_current (transaction_a, account_a));
		uint64_t sequence ((result ? result->sequence : 0) + 1);
		result = std::make_shared<futurehead::vote> (account_a, key_a, sequence, blocks_a);
		vote_cache_l1[account_a] = result;
		return result;
	}

	std::shared_ptr<futurehead::vote> vote_max (futurehead::transaction const & transaction_a, std::shared_ptr<futurehead::vote> vote_a) override
	{
		futurehead::lock_guard<std::mutex> lock (cache_mutex);
		auto current (vote_current (transaction_a, vote_a->account));
		auto result (vote_a);
		if (current != nullptr && current->sequence > result->sequence)
		{
			result = current;
		}
		vote_cache_l1[vote_a->account] = result;
		return result;
	}

	futurehead::store_iterator<futurehead::unchecked_key, futurehead::unchecked_info> unchecked_end () const override
	{
		return futurehead::store_iterator<futurehead::unchecked_key, futurehead::unchecked_info> (nullptr);
	}

	futurehead::store_iterator<futurehead::account, std::shared_ptr<futurehead::vote>> vote_end () override
	{
		return futurehead::store_iterator<futurehead::account, std::shared_ptr<futurehead::vote>> (nullptr);
	}

	futurehead::store_iterator<futurehead::endpoint_key, futurehead::no_value> peers_end () const override
	{
		return futurehead::store_iterator<futurehead::endpoint_key, futurehead::no_value> (nullptr);
	}

	futurehead::store_iterator<futurehead::pending_key, futurehead::pending_info> pending_end () override
	{
		return futurehead::store_iterator<futurehead::pending_key, futurehead::pending_info> (nullptr);
	}

	futurehead::store_iterator<uint64_t, futurehead::amount> online_weight_end () const override
	{
		return futurehead::store_iterator<uint64_t, futurehead::amount> (nullptr);
	}

	futurehead::store_iterator<futurehead::account, futurehead::account_info> latest_end () const override
	{
		return futurehead::store_iterator<futurehead::account, futurehead::account_info> (nullptr);
	}

	futurehead::store_iterator<futurehead::account, futurehead::confirmation_height_info> confirmation_height_end () override
	{
		return futurehead::store_iterator<futurehead::account, futurehead::confirmation_height_info> (nullptr);
	}

	std::mutex & get_cache_mutex () override
	{
		return cache_mutex;
	}

	void block_del (futurehead::write_transaction const & transaction_a, futurehead::block_hash const & hash_a, futurehead::block_type block_type_a) override
	{
		auto table = tables::state_blocks;
		switch (block_type_a)
		{
			case futurehead::block_type::open:
				table = tables::open_blocks;
				break;
			case futurehead::block_type::receive:
				table = tables::receive_blocks;
				break;
			case futurehead::block_type::send:
				table = tables::send_blocks;
				break;
			case futurehead::block_type::change:
				table = tables::change_blocks;
				break;
			case futurehead::block_type::state:
				table = tables::state_blocks;
				break;
			default:
				debug_assert (false);
		}

		auto status = del (transaction_a, table, hash_a);
		release_assert (success (status));
	}

	int version_get (futurehead::transaction const & transaction_a) const override
	{
		futurehead::uint256_union version_key (1);
		futurehead::db_val<Val> data;
		auto status = get (transaction_a, tables::meta, futurehead::db_val<Val> (version_key), data);
		int result (1);
		if (!not_found (status))
		{
			futurehead::uint256_union version_value (data);
			debug_assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
			result = version_value.number ().convert_to<int> ();
		}
		return result;
	}

	futurehead::epoch block_version (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) override
	{
		futurehead::db_val<Val> value;
		auto block = block_get (transaction_a, hash_a);
		if (block && block->type () == futurehead::block_type::state)
		{
			return block->sideband ().details.epoch;
		}

		return futurehead::epoch::epoch_0;
	}

	void block_raw_put (futurehead::write_transaction const & transaction_a, std::vector<uint8_t> const & data, futurehead::block_type block_type_a, futurehead::block_hash const & hash_a)
	{
		auto database_a = block_database (block_type_a);
		futurehead::db_val<Val> value{ data.size (), (void *)data.data () };
		auto status = put (transaction_a, database_a, hash_a, value);
		release_assert (success (status));
	}

	void pending_put (futurehead::write_transaction const & transaction_a, futurehead::pending_key const & key_a, futurehead::pending_info const & pending_info_a) override
	{
		futurehead::db_val<Val> pending (pending_info_a);
		auto status = put (transaction_a, tables::pending, key_a, pending);
		release_assert (success (status));
	}

	void pending_del (futurehead::write_transaction const & transaction_a, futurehead::pending_key const & key_a) override
	{
		auto status = del (transaction_a, tables::pending, key_a);
		release_assert (success (status));
	}

	bool pending_get (futurehead::transaction const & transaction_a, futurehead::pending_key const & key_a, futurehead::pending_info & pending_a) override
	{
		futurehead::db_val<Val> value;
		futurehead::db_val<Val> key (key_a);
		auto status1 = get (transaction_a, tables::pending, key, value);
		release_assert (success (status1) || not_found (status1));
		bool result (true);
		if (success (status1))
		{
			futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = pending_a.deserialize (stream);
		}
		return result;
	}

	void frontier_put (futurehead::write_transaction const & transaction_a, futurehead::block_hash const & block_a, futurehead::account const & account_a) override
	{
		futurehead::db_val<Val> account (account_a);
		auto status (put (transaction_a, tables::frontiers, block_a, account));
		release_assert (success (status));
	}

	futurehead::account frontier_get (futurehead::transaction const & transaction_a, futurehead::block_hash const & block_a) const override
	{
		futurehead::db_val<Val> value;
		auto status (get (transaction_a, tables::frontiers, futurehead::db_val<Val> (block_a), value));
		release_assert (success (status) || not_found (status));
		futurehead::account result (0);
		if (success (status))
		{
			result = static_cast<futurehead::account> (value);
		}
		return result;
	}

	void frontier_del (futurehead::write_transaction const & transaction_a, futurehead::block_hash const & block_a) override
	{
		auto status (del (transaction_a, tables::frontiers, block_a));
		release_assert (success (status));
	}

	void unchecked_put (futurehead::write_transaction const & transaction_a, futurehead::unchecked_key const & key_a, futurehead::unchecked_info const & info_a) override
	{
		futurehead::db_val<Val> info (info_a);
		auto status (put (transaction_a, tables::unchecked, key_a, info));
		release_assert (success (status));
	}

	void unchecked_del (futurehead::write_transaction const & transaction_a, futurehead::unchecked_key const & key_a) override
	{
		auto status (del (transaction_a, tables::unchecked, key_a));
		release_assert (success (status));
	}

	std::shared_ptr<futurehead::vote> vote_get (futurehead::transaction const & transaction_a, futurehead::account const & account_a) override
	{
		futurehead::db_val<Val> value;
		auto status (get (transaction_a, tables::vote, futurehead::db_val<Val> (account_a), value));
		release_assert (success (status) || not_found (status));
		if (success (status))
		{
			std::shared_ptr<futurehead::vote> result (value);
			debug_assert (result != nullptr);
			return result;
		}
		return nullptr;
	}

	void flush (futurehead::write_transaction const & transaction_a) override
	{
		{
			futurehead::lock_guard<std::mutex> lock (cache_mutex);
			vote_cache_l1.swap (vote_cache_l2);
			vote_cache_l1.clear ();
		}
		for (auto i (vote_cache_l2.begin ()), n (vote_cache_l2.end ()); i != n; ++i)
		{
			std::vector<uint8_t> vector;
			{
				futurehead::vectorstream stream (vector);
				i->second->serialize (stream);
			}
			futurehead::db_val<Val> value (vector.size (), vector.data ());
			auto status1 (put (transaction_a, tables::vote, i->first, value));
			release_assert (success (status1));
		}
	}

	void online_weight_put (futurehead::write_transaction const & transaction_a, uint64_t time_a, futurehead::amount const & amount_a) override
	{
		futurehead::db_val<Val> value (amount_a);
		auto status (put (transaction_a, tables::online_weight, time_a, value));
		release_assert (success (status));
	}

	void online_weight_del (futurehead::write_transaction const & transaction_a, uint64_t time_a) override
	{
		auto status (del (transaction_a, tables::online_weight, time_a));
		release_assert (success (status));
	}

	void account_put (futurehead::write_transaction const & transaction_a, futurehead::account const & account_a, futurehead::account_info const & info_a) override
	{
		// Check we are still in sync with other tables
		debug_assert (confirmation_height_exists (transaction_a, account_a));
		futurehead::db_val<Val> info (info_a);
		auto status = put (transaction_a, tables::accounts, account_a, info);
		release_assert (success (status));
	}

	void account_del (futurehead::write_transaction const & transaction_a, futurehead::account const & account_a) override
	{
		auto status = del (transaction_a, tables::accounts, account_a);
		release_assert (success (status));
	}

	bool account_get (futurehead::transaction const & transaction_a, futurehead::account const & account_a, futurehead::account_info & info_a) override
	{
		futurehead::db_val<Val> value;
		futurehead::db_val<Val> account (account_a);
		auto status1 (get (transaction_a, tables::accounts, account, value));
		release_assert (success (status1) || not_found (status1));
		bool result (true);
		if (success (status1))
		{
			futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = info_a.deserialize (stream);
		}
		return result;
	}

	void unchecked_clear (futurehead::write_transaction const & transaction_a) override
	{
		auto status = drop (transaction_a, tables::unchecked);
		release_assert (success (status));
	}

	size_t online_weight_count (futurehead::transaction const & transaction_a) const override
	{
		return count (transaction_a, tables::online_weight);
	}

	void online_weight_clear (futurehead::write_transaction const & transaction_a) override
	{
		auto status (drop (transaction_a, tables::online_weight));
		release_assert (success (status));
	}

	void peer_put (futurehead::write_transaction const & transaction_a, futurehead::endpoint_key const & endpoint_a) override
	{
		futurehead::db_val<Val> zero (static_cast<uint64_t> (0));
		auto status = put (transaction_a, tables::peers, endpoint_a, zero);
		release_assert (success (status));
	}

	void peer_del (futurehead::write_transaction const & transaction_a, futurehead::endpoint_key const & endpoint_a) override
	{
		auto status (del (transaction_a, tables::peers, endpoint_a));
		release_assert (success (status));
	}

	bool peer_exists (futurehead::transaction const & transaction_a, futurehead::endpoint_key const & endpoint_a) const override
	{
		return exists (transaction_a, tables::peers, futurehead::db_val<Val> (endpoint_a));
	}

	size_t peer_count (futurehead::transaction const & transaction_a) const override
	{
		return count (transaction_a, tables::peers);
	}

	void peer_clear (futurehead::write_transaction const & transaction_a) override
	{
		auto status = drop (transaction_a, tables::peers);
		release_assert (success (status));
	}

	bool exists (futurehead::transaction const & transaction_a, tables table_a, futurehead::db_val<Val> const & key_a) const
	{
		return static_cast<const Derived_Store &> (*this).exists (transaction_a, table_a, key_a);
	}

	futurehead::block_counts block_count (futurehead::transaction const & transaction_a) override
	{
		futurehead::block_counts result;
		result.send = count (transaction_a, tables::send_blocks);
		result.receive = count (transaction_a, tables::receive_blocks);
		result.open = count (transaction_a, tables::open_blocks);
		result.change = count (transaction_a, tables::change_blocks);
		result.state = count (transaction_a, tables::state_blocks);
		return result;
	}

	size_t account_count (futurehead::transaction const & transaction_a) override
	{
		return count (transaction_a, tables::accounts);
	}

	std::shared_ptr<futurehead::block> block_random (futurehead::transaction const & transaction_a) override
	{
		auto count (block_count (transaction_a));
		release_assert (std::numeric_limits<CryptoPP::word32>::max () > count.sum ());
		auto region = static_cast<size_t> (futurehead::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (count.sum () - 1)));
		std::shared_ptr<futurehead::block> result;
		auto & derived_store = static_cast<Derived_Store &> (*this);
		if (region < count.send)
		{
			result = derived_store.template block_random<futurehead::send_block> (transaction_a, tables::send_blocks);
		}
		else
		{
			region -= count.send;
			if (region < count.receive)
			{
				result = derived_store.template block_random<futurehead::receive_block> (transaction_a, tables::receive_blocks);
			}
			else
			{
				region -= count.receive;
				if (region < count.open)
				{
					result = derived_store.template block_random<futurehead::open_block> (transaction_a, tables::open_blocks);
				}
				else
				{
					region -= count.open;
					if (region < count.change)
					{
						result = derived_store.template block_random<futurehead::change_block> (transaction_a, tables::change_blocks);
					}
					else
					{
						result = derived_store.template block_random<futurehead::state_block> (transaction_a, tables::state_blocks);
					}
				}
			}
		}
		debug_assert (result != nullptr);
		return result;
	}

	uint64_t confirmation_height_count (futurehead::transaction const & transaction_a) override
	{
		return count (transaction_a, tables::confirmation_height);
	}

	void confirmation_height_put (futurehead::write_transaction const & transaction_a, futurehead::account const & account_a, futurehead::confirmation_height_info const & confirmation_height_info_a) override
	{
		futurehead::db_val<Val> confirmation_height_info (confirmation_height_info_a);
		auto status = put (transaction_a, tables::confirmation_height, account_a, confirmation_height_info);
		release_assert (success (status));
	}

	bool confirmation_height_get (futurehead::transaction const & transaction_a, futurehead::account const & account_a, futurehead::confirmation_height_info & confirmation_height_info_a) override
	{
		futurehead::db_val<Val> value;
		auto status = get (transaction_a, tables::confirmation_height, futurehead::db_val<Val> (account_a), value);
		release_assert (success (status) || not_found (status));
		bool result (true);
		if (success (status))
		{
			futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = confirmation_height_info_a.deserialize (stream);
		}
		return result;
	}

	void confirmation_height_del (futurehead::write_transaction const & transaction_a, futurehead::account const & account_a) override
	{
		auto status (del (transaction_a, tables::confirmation_height, futurehead::db_val<Val> (account_a)));
		release_assert (success (status));
	}

	bool confirmation_height_exists (futurehead::transaction const & transaction_a, futurehead::account const & account_a) const override
	{
		return exists (transaction_a, tables::confirmation_height, futurehead::db_val<Val> (account_a));
	}

	futurehead::store_iterator<futurehead::account, futurehead::account_info> latest_begin (futurehead::transaction const & transaction_a, futurehead::account const & account_a) const override
	{
		return make_iterator<futurehead::account, futurehead::account_info> (transaction_a, tables::accounts, futurehead::db_val<Val> (account_a));
	}

	futurehead::store_iterator<futurehead::account, futurehead::account_info> latest_begin (futurehead::transaction const & transaction_a) const override
	{
		return make_iterator<futurehead::account, futurehead::account_info> (transaction_a, tables::accounts);
	}

	futurehead::store_iterator<futurehead::pending_key, futurehead::pending_info> pending_begin (futurehead::transaction const & transaction_a, futurehead::pending_key const & key_a) override
	{
		return make_iterator<futurehead::pending_key, futurehead::pending_info> (transaction_a, tables::pending, futurehead::db_val<Val> (key_a));
	}

	futurehead::store_iterator<futurehead::pending_key, futurehead::pending_info> pending_begin (futurehead::transaction const & transaction_a) override
	{
		return make_iterator<futurehead::pending_key, futurehead::pending_info> (transaction_a, tables::pending);
	}

	futurehead::store_iterator<futurehead::unchecked_key, futurehead::unchecked_info> unchecked_begin (futurehead::transaction const & transaction_a) const override
	{
		return make_iterator<futurehead::unchecked_key, futurehead::unchecked_info> (transaction_a, tables::unchecked);
	}

	futurehead::store_iterator<futurehead::unchecked_key, futurehead::unchecked_info> unchecked_begin (futurehead::transaction const & transaction_a, futurehead::unchecked_key const & key_a) const override
	{
		return make_iterator<futurehead::unchecked_key, futurehead::unchecked_info> (transaction_a, tables::unchecked, futurehead::db_val<Val> (key_a));
	}

	futurehead::store_iterator<futurehead::account, std::shared_ptr<futurehead::vote>> vote_begin (futurehead::transaction const & transaction_a) override
	{
		return make_iterator<futurehead::account, std::shared_ptr<futurehead::vote>> (transaction_a, tables::vote);
	}

	futurehead::store_iterator<uint64_t, futurehead::amount> online_weight_begin (futurehead::transaction const & transaction_a) const override
	{
		return make_iterator<uint64_t, futurehead::amount> (transaction_a, tables::online_weight);
	}

	futurehead::store_iterator<futurehead::endpoint_key, futurehead::no_value> peers_begin (futurehead::transaction const & transaction_a) const override
	{
		return make_iterator<futurehead::endpoint_key, futurehead::no_value> (transaction_a, tables::peers);
	}

	futurehead::store_iterator<futurehead::account, futurehead::confirmation_height_info> confirmation_height_begin (futurehead::transaction const & transaction_a, futurehead::account const & account_a) override
	{
		return make_iterator<futurehead::account, futurehead::confirmation_height_info> (transaction_a, tables::confirmation_height, futurehead::db_val<Val> (account_a));
	}

	futurehead::store_iterator<futurehead::account, futurehead::confirmation_height_info> confirmation_height_begin (futurehead::transaction const & transaction_a) override
	{
		return make_iterator<futurehead::account, futurehead::confirmation_height_info> (transaction_a, tables::confirmation_height);
	}

	size_t unchecked_count (futurehead::transaction const & transaction_a) override
	{
		return count (transaction_a, tables::unchecked);
	}

protected:
	futurehead::network_params network_params;
	std::unordered_map<futurehead::account, std::shared_ptr<futurehead::vote>> vote_cache_l1;
	std::unordered_map<futurehead::account, std::shared_ptr<futurehead::vote>> vote_cache_l2;
	static int constexpr version{ 18 };

	template <typename T>
	std::shared_ptr<futurehead::block> block_random (futurehead::transaction const & transaction_a, tables table_a)
	{
		futurehead::block_hash hash;
		futurehead::random_pool::generate_block (hash.bytes.data (), hash.bytes.size ());
		auto existing = make_iterator<futurehead::block_hash, std::shared_ptr<T>> (transaction_a, table_a, futurehead::db_val<Val> (hash));
		if (existing == futurehead::store_iterator<futurehead::block_hash, std::shared_ptr<T>> (nullptr))
		{
			existing = make_iterator<futurehead::block_hash, std::shared_ptr<T>> (transaction_a, table_a);
		}
		auto end (futurehead::store_iterator<futurehead::block_hash, std::shared_ptr<T>> (nullptr));
		debug_assert (existing != end);
		return block_get (transaction_a, futurehead::block_hash (existing->first));
	}

	template <typename Key, typename Value>
	futurehead::store_iterator<Key, Value> make_iterator (futurehead::transaction const & transaction_a, tables table_a) const
	{
		return static_cast<Derived_Store const &> (*this).template make_iterator<Key, Value> (transaction_a, table_a);
	}

	template <typename Key, typename Value>
	futurehead::store_iterator<Key, Value> make_iterator (futurehead::transaction const & transaction_a, tables table_a, futurehead::db_val<Val> const & key) const
	{
		return static_cast<Derived_Store const &> (*this).template make_iterator<Key, Value> (transaction_a, table_a, key);
	}

	bool entry_has_sideband (size_t entry_size_a, futurehead::block_type type_a) const
	{
		return entry_size_a == futurehead::block::size (type_a) + futurehead::block_sideband::size (type_a);
	}

	futurehead::db_val<Val> block_raw_get (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a, futurehead::block_type & type_a) const
	{
		futurehead::db_val<Val> result;
		// Table lookups are ordered by match probability
		futurehead::block_type block_types[]{ futurehead::block_type::state, futurehead::block_type::send, futurehead::block_type::receive, futurehead::block_type::open, futurehead::block_type::change };
		for (auto current_type : block_types)
		{
			auto db_val (block_raw_get_by_type (transaction_a, hash_a, current_type));
			if (db_val.is_initialized ())
			{
				type_a = current_type;
				result = db_val.get ();
				break;
			}
		}

		return result;
	}

	// Return account containing hash
	futurehead::account block_account_computed (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const
	{
		debug_assert (!full_sideband (transaction_a));
		futurehead::account result (0);
		auto hash (hash_a);
		while (result.is_zero ())
		{
			auto block (block_get_no_sideband (transaction_a, hash));
			debug_assert (block);
			result = block->account ();
			if (result.is_zero ())
			{
				auto type (futurehead::block_type::invalid);
				auto value (block_raw_get (transaction_a, block->previous (), type));
				if (entry_has_sideband (value.size (), type))
				{
					result = block_account (transaction_a, block->previous ());
				}
				else
				{
					futurehead::block_info block_info;
					if (!block_info_get (transaction_a, hash, block_info))
					{
						result = block_info.account;
					}
					else
					{
						result = frontier_get (transaction_a, hash);
						if (result.is_zero ())
						{
							auto successor (block_successor (transaction_a, hash));
							debug_assert (!successor.is_zero ());
							hash = successor;
						}
					}
				}
			}
		}
		debug_assert (!result.is_zero ());
		return result;
	}

	futurehead::uint128_t block_balance_computed (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const
	{
		debug_assert (!full_sideband (transaction_a));
		summation_visitor visitor (transaction_a, *this);
		return visitor.compute_balance (hash_a);
	}

	size_t block_successor_offset (futurehead::transaction const & transaction_a, size_t entry_size_a, futurehead::block_type type_a) const
	{
		size_t result;
		if (full_sideband (transaction_a) || entry_has_sideband (entry_size_a, type_a))
		{
			result = entry_size_a - futurehead::block_sideband::size (type_a);
		}
		else
		{
			// Read old successor-only sideband
			debug_assert (entry_size_a == futurehead::block::size (type_a) + sizeof (futurehead::block_hash));
			result = entry_size_a - sizeof (futurehead::block_hash);
		}
		return result;
	}

	boost::optional<futurehead::db_val<Val>> block_raw_get_by_type (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a, futurehead::block_type & type_a) const
	{
		futurehead::db_val<Val> value;
		futurehead::db_val<Val> hash (hash_a);
		int status = status_code_not_found ();
		switch (type_a)
		{
			case futurehead::block_type::send:
			{
				status = get (transaction_a, tables::send_blocks, hash, value);
				break;
			}
			case futurehead::block_type::receive:
			{
				status = get (transaction_a, tables::receive_blocks, hash, value);
				break;
			}
			case futurehead::block_type::open:
			{
				status = get (transaction_a, tables::open_blocks, hash, value);
				break;
			}
			case futurehead::block_type::change:
			{
				status = get (transaction_a, tables::change_blocks, hash, value);
				break;
			}
			case futurehead::block_type::state:
			{
				status = get (transaction_a, tables::state_blocks, hash, value);
				break;
			}
			case futurehead::block_type::invalid:
			case futurehead::block_type::not_a_block:
			{
				break;
			}
		}

		release_assert (success (status) || not_found (status));
		boost::optional<futurehead::db_val<Val>> result;
		if (success (status))
		{
			result = value;
		}
		return result;
	}

	tables block_database (futurehead::block_type type_a)
	{
		tables result = tables::frontiers;
		switch (type_a)
		{
			case futurehead::block_type::send:
				result = tables::send_blocks;
				break;
			case futurehead::block_type::receive:
				result = tables::receive_blocks;
				break;
			case futurehead::block_type::open:
				result = tables::open_blocks;
				break;
			case futurehead::block_type::change:
				result = tables::change_blocks;
				break;
			case futurehead::block_type::state:
				result = tables::state_blocks;
				break;
			default:
				debug_assert (false);
				break;
		}
		return result;
	}

	size_t count (futurehead::transaction const & transaction_a, std::initializer_list<tables> dbs_a) const
	{
		size_t total_count = 0;
		for (auto db : dbs_a)
		{
			total_count += count (transaction_a, db);
		}
		return total_count;
	}

	int get (futurehead::transaction const & transaction_a, tables table_a, futurehead::db_val<Val> const & key_a, futurehead::db_val<Val> & value_a) const
	{
		return static_cast<Derived_Store const &> (*this).get (transaction_a, table_a, key_a, value_a);
	}

	int put (futurehead::write_transaction const & transaction_a, tables table_a, futurehead::db_val<Val> const & key_a, futurehead::db_val<Val> const & value_a)
	{
		return static_cast<Derived_Store &> (*this).put (transaction_a, table_a, key_a, value_a);
	}

	int del (futurehead::write_transaction const & transaction_a, tables table_a, futurehead::db_val<Val> const & key_a)
	{
		return static_cast<Derived_Store &> (*this).del (transaction_a, table_a, key_a);
	}

	virtual size_t count (futurehead::transaction const & transaction_a, tables table_a) const = 0;
	virtual int drop (futurehead::write_transaction const & transaction_a, tables table_a) = 0;
	virtual bool not_found (int status) const = 0;
	virtual bool success (int status) const = 0;
	virtual int status_code_not_found () const = 0;
};

/**
 * Fill in our predecessors
 */
template <typename Val, typename Derived_Store>
class block_predecessor_set : public futurehead::block_visitor
{
public:
	block_predecessor_set (futurehead::write_transaction const & transaction_a, futurehead::block_store_partial<Val, Derived_Store> & store_a) :
	transaction (transaction_a),
	store (store_a)
	{
	}
	virtual ~block_predecessor_set () = default;
	void fill_value (futurehead::block const & block_a)
	{
		auto hash (block_a.hash ());
		futurehead::block_type type;
		auto value (store.block_raw_get (transaction, block_a.previous (), type));
		debug_assert (value.size () != 0);
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
		std::copy (hash.bytes.begin (), hash.bytes.end (), data.begin () + store.block_successor_offset (transaction, value.size (), type));
		store.block_raw_put (transaction, data, type, block_a.previous ());
	}
	void send_block (futurehead::send_block const & block_a) override
	{
		fill_value (block_a);
	}
	void receive_block (futurehead::receive_block const & block_a) override
	{
		fill_value (block_a);
	}
	void open_block (futurehead::open_block const & block_a) override
	{
		// Open blocks don't have a predecessor
	}
	void change_block (futurehead::change_block const & block_a) override
	{
		fill_value (block_a);
	}
	void state_block (futurehead::state_block const & block_a) override
	{
		if (!block_a.previous ().is_zero ())
		{
			fill_value (block_a);
		}
	}
	futurehead::write_transaction const & transaction;
	futurehead::block_store_partial<Val, Derived_Store> & store;
};
}
