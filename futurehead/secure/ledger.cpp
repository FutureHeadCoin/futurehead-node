#include <futurehead/lib/rep_weights.hpp>
#include <futurehead/lib/stats.hpp>
#include <futurehead/lib/utility.hpp>
#include <futurehead/lib/work.hpp>
#include <futurehead/secure/blockstore.hpp>
#include <futurehead/secure/common.hpp>
#include <futurehead/secure/ledger.hpp>

namespace
{
/**
 * Roll back the visited block
 */
class rollback_visitor : public futurehead::block_visitor
{
public:
	rollback_visitor (futurehead::write_transaction const & transaction_a, futurehead::ledger & ledger_a, std::vector<std::shared_ptr<futurehead::block>> & list_a) :
	transaction (transaction_a),
	ledger (ledger_a),
	list (list_a)
	{
	}
	virtual ~rollback_visitor () = default;
	void send_block (futurehead::send_block const & block_a) override
	{
		auto hash (block_a.hash ());
		futurehead::pending_info pending;
		futurehead::pending_key key (block_a.hashables.destination, hash);
		while (!error && ledger.store.pending_get (transaction, key, pending))
		{
			error = ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.destination), list);
		}
		if (!error)
		{
			futurehead::account_info info;
			auto error (ledger.store.account_get (transaction, pending.source, info));
			(void)error;
			debug_assert (!error);
			ledger.store.pending_del (transaction, key);
			ledger.cache.rep_weights.representation_add (info.representative, pending.amount.number ());
			futurehead::account_info new_info (block_a.hashables.previous, info.representative, info.open_block, ledger.balance (transaction, block_a.hashables.previous), futurehead::seconds_since_epoch (), info.block_count - 1, futurehead::epoch::epoch_0);
			ledger.change_latest (transaction, pending.source, info, new_info);
			ledger.store.block_del (transaction, hash, block_a.type ());
			ledger.store.frontier_del (transaction, hash);
			ledger.store.frontier_put (transaction, block_a.hashables.previous, pending.source);
			ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
			ledger.stats.inc (futurehead::stat::type::rollback, futurehead::stat::detail::send);
		}
	}
	void receive_block (futurehead::receive_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto amount (ledger.amount (transaction, block_a.hashables.source));
		auto destination_account (ledger.account (transaction, hash));
		auto source_account (ledger.account (transaction, block_a.hashables.source));
		futurehead::account_info info;
		auto error (ledger.store.account_get (transaction, destination_account, info));
		(void)error;
		debug_assert (!error);
		ledger.cache.rep_weights.representation_add (info.representative, 0 - amount);
		futurehead::account_info new_info (block_a.hashables.previous, info.representative, info.open_block, ledger.balance (transaction, block_a.hashables.previous), futurehead::seconds_since_epoch (), info.block_count - 1, futurehead::epoch::epoch_0);
		ledger.change_latest (transaction, destination_account, info, new_info);
		ledger.store.block_del (transaction, hash, block_a.type ());
		ledger.store.pending_put (transaction, futurehead::pending_key (destination_account, block_a.hashables.source), { source_account, amount, futurehead::epoch::epoch_0 });
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, destination_account);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (futurehead::stat::type::rollback, futurehead::stat::detail::receive);
	}
	void open_block (futurehead::open_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto amount (ledger.amount (transaction, block_a.hashables.source));
		auto destination_account (ledger.account (transaction, hash));
		auto source_account (ledger.account (transaction, block_a.hashables.source));
		ledger.cache.rep_weights.representation_add (block_a.representative (), 0 - amount);
		futurehead::account_info new_info;
		ledger.change_latest (transaction, destination_account, new_info, new_info);
		ledger.store.block_del (transaction, hash, block_a.type ());
		ledger.store.pending_put (transaction, futurehead::pending_key (destination_account, block_a.hashables.source), { source_account, amount, futurehead::epoch::epoch_0 });
		ledger.store.frontier_del (transaction, hash);
		ledger.stats.inc (futurehead::stat::type::rollback, futurehead::stat::detail::open);
	}
	void change_block (futurehead::change_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto rep_block (ledger.representative (transaction, block_a.hashables.previous));
		auto account (ledger.account (transaction, block_a.hashables.previous));
		futurehead::account_info info;
		auto error (ledger.store.account_get (transaction, account, info));
		(void)error;
		debug_assert (!error);
		auto balance (ledger.balance (transaction, block_a.hashables.previous));
		auto block = ledger.store.block_get (transaction, rep_block);
		release_assert (block != nullptr);
		auto representative = block->representative ();
		ledger.cache.rep_weights.representation_add (block_a.representative (), 0 - balance);
		ledger.cache.rep_weights.representation_add (representative, balance);
		ledger.store.block_del (transaction, hash, block_a.type ());
		futurehead::account_info new_info (block_a.hashables.previous, representative, info.open_block, info.balance, futurehead::seconds_since_epoch (), info.block_count - 1, futurehead::epoch::epoch_0);
		ledger.change_latest (transaction, account, info, new_info);
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, account);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (futurehead::stat::type::rollback, futurehead::stat::detail::change);
	}
	void state_block (futurehead::state_block const & block_a) override
	{
		auto hash (block_a.hash ());
		futurehead::block_hash rep_block_hash (0);
		if (!block_a.hashables.previous.is_zero ())
		{
			rep_block_hash = ledger.representative (transaction, block_a.hashables.previous);
		}
		auto balance (ledger.balance (transaction, block_a.hashables.previous));
		auto is_send (block_a.hashables.balance < balance);
		// Add in amount delta
		ledger.cache.rep_weights.representation_add (block_a.representative (), 0 - block_a.hashables.balance.number ());
		futurehead::account representative{ 0 };
		if (!rep_block_hash.is_zero ())
		{
			// Move existing representation
			auto block (ledger.store.block_get (transaction, rep_block_hash));
			debug_assert (block != nullptr);
			representative = block->representative ();
			ledger.cache.rep_weights.representation_add (representative, balance);
		}

		futurehead::account_info info;
		auto error (ledger.store.account_get (transaction, block_a.hashables.account, info));

		if (is_send)
		{
			futurehead::pending_key key (block_a.hashables.link, hash);
			while (!error && !ledger.store.pending_exists (transaction, key))
			{
				error = ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.link), list);
			}
			ledger.store.pending_del (transaction, key);
			ledger.stats.inc (futurehead::stat::type::rollback, futurehead::stat::detail::send);
		}
		else if (!block_a.hashables.link.is_zero () && !ledger.is_epoch_link (block_a.hashables.link))
		{
			auto source_version (ledger.store.block_version (transaction, block_a.hashables.link));
			futurehead::pending_info pending_info (ledger.account (transaction, block_a.hashables.link), block_a.hashables.balance.number () - balance, source_version);
			ledger.store.pending_put (transaction, futurehead::pending_key (block_a.hashables.account, block_a.hashables.link), pending_info);
			ledger.stats.inc (futurehead::stat::type::rollback, futurehead::stat::detail::receive);
		}

		debug_assert (!error);
		auto previous_version (ledger.store.block_version (transaction, block_a.hashables.previous));
		futurehead::account_info new_info (block_a.hashables.previous, representative, info.open_block, balance, futurehead::seconds_since_epoch (), info.block_count - 1, previous_version);
		ledger.change_latest (transaction, block_a.hashables.account, info, new_info);

		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		if (previous != nullptr)
		{
			ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
			if (previous->type () < futurehead::block_type::state)
			{
				ledger.store.frontier_put (transaction, block_a.hashables.previous, block_a.hashables.account);
			}
		}
		else
		{
			ledger.stats.inc (futurehead::stat::type::rollback, futurehead::stat::detail::open);
		}
		ledger.store.block_del (transaction, hash, block_a.type ());
	}
	futurehead::write_transaction const & transaction;
	futurehead::ledger & ledger;
	std::vector<std::shared_ptr<futurehead::block>> & list;
	bool error{ false };
};

class ledger_processor : public futurehead::mutable_block_visitor
{
public:
	ledger_processor (futurehead::ledger &, futurehead::write_transaction const &, futurehead::signature_verification = futurehead::signature_verification::unknown);
	virtual ~ledger_processor () = default;
	void send_block (futurehead::send_block &) override;
	void receive_block (futurehead::receive_block &) override;
	void open_block (futurehead::open_block &) override;
	void change_block (futurehead::change_block &) override;
	void state_block (futurehead::state_block &) override;
	void state_block_impl (futurehead::state_block &);
	void epoch_block_impl (futurehead::state_block &);
	futurehead::ledger & ledger;
	futurehead::write_transaction const & transaction;
	futurehead::signature_verification verification;
	futurehead::process_return result;

private:
	bool validate_epoch_block (futurehead::state_block const & block_a);
};

// Returns true if this block which has an epoch link is correctly formed.
bool ledger_processor::validate_epoch_block (futurehead::state_block const & block_a)
{
	debug_assert (ledger.is_epoch_link (block_a.hashables.link));
	futurehead::amount prev_balance (0);
	if (!block_a.hashables.previous.is_zero ())
	{
		result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? futurehead::process_result::progress : futurehead::process_result::gap_previous;
		if (result.code == futurehead::process_result::progress)
		{
			prev_balance = ledger.balance (transaction, block_a.hashables.previous);
		}
		else if (result.verified == futurehead::signature_verification::unknown)
		{
			// Check for possible regular state blocks with epoch link (send subtype)
			if (validate_message (block_a.hashables.account, block_a.hash (), block_a.signature))
			{
				// Is epoch block signed correctly
				if (validate_message (ledger.epoch_signer (block_a.link ()), block_a.hash (), block_a.signature))
				{
					result.verified = futurehead::signature_verification::invalid;
					result.code = futurehead::process_result::bad_signature;
				}
				else
				{
					result.verified = futurehead::signature_verification::valid_epoch;
				}
			}
			else
			{
				result.verified = futurehead::signature_verification::valid;
			}
		}
	}
	return (block_a.hashables.balance == prev_balance);
}

void ledger_processor::state_block (futurehead::state_block & block_a)
{
	result.code = futurehead::process_result::progress;
	auto is_epoch_block = false;
	if (ledger.is_epoch_link (block_a.hashables.link))
	{
		// This function also modifies the result variable if epoch is mal-formed
		is_epoch_block = validate_epoch_block (block_a);
	}

	if (result.code == futurehead::process_result::progress)
	{
		if (is_epoch_block)
		{
			epoch_block_impl (block_a);
		}
		else
		{
			state_block_impl (block_a);
		}
	}
}

void ledger_processor::state_block_impl (futurehead::state_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? futurehead::process_result::old : futurehead::process_result::progress; // Have we seen this block before? (Unambiguous)
	if (result.code == futurehead::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != futurehead::signature_verification::valid)
		{
			result.code = validate_message (block_a.hashables.account, hash, block_a.signature) ? futurehead::process_result::bad_signature : futurehead::process_result::progress; // Is this block signed correctly (Unambiguous)
		}
		if (result.code == futurehead::process_result::progress)
		{
			debug_assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result.verified = futurehead::signature_verification::valid;
			result.code = block_a.hashables.account.is_zero () ? futurehead::process_result::opened_burn_account : futurehead::process_result::progress; // Is this for the burn account? (Unambiguous)
			if (result.code == futurehead::process_result::progress)
			{
				futurehead::epoch epoch (futurehead::epoch::epoch_0);
				futurehead::account_info info;
				result.amount = block_a.hashables.balance;
				auto is_send (false);
				auto is_receive (false);
				auto account_error (ledger.store.account_get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					// Account already exists
					epoch = info.epoch ();
					result.previous_balance = info.balance;
					result.code = block_a.hashables.previous.is_zero () ? futurehead::process_result::fork : futurehead::process_result::progress; // Has this account already been opened? (Ambigious)
					if (result.code == futurehead::process_result::progress)
					{
						result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? futurehead::process_result::progress : futurehead::process_result::gap_previous; // Does the previous block exist in the ledger? (Unambigious)
						if (result.code == futurehead::process_result::progress)
						{
							is_send = block_a.hashables.balance < info.balance;
							is_receive = !is_send && !block_a.hashables.link.is_zero ();
							result.amount = is_send ? (info.balance.number () - result.amount.number ()) : (result.amount.number () - info.balance.number ());
							result.code = block_a.hashables.previous == info.head ? futurehead::process_result::progress : futurehead::process_result::fork; // Is the previous block the account's head block? (Ambigious)
						}
					}
				}
				else
				{
					// Account does not yet exists
					result.previous_balance = 0;
					result.code = block_a.previous ().is_zero () ? futurehead::process_result::progress : futurehead::process_result::gap_previous; // Does the first block in an account yield 0 for previous() ? (Unambigious)
					if (result.code == futurehead::process_result::progress)
					{
						is_receive = true;
						result.code = !block_a.hashables.link.is_zero () ? futurehead::process_result::progress : futurehead::process_result::gap_source; // Is the first block receiving from a send ? (Unambigious)
					}
				}
				if (result.code == futurehead::process_result::progress)
				{
					if (!is_send)
					{
						if (!block_a.hashables.link.is_zero ())
						{
							result.code = ledger.store.source_exists (transaction, block_a.hashables.link) ? futurehead::process_result::progress : futurehead::process_result::gap_source; // Have we seen the source block already? (Harmless)
							if (result.code == futurehead::process_result::progress)
							{
								futurehead::pending_key key (block_a.hashables.account, block_a.hashables.link);
								futurehead::pending_info pending;
								result.code = ledger.store.pending_get (transaction, key, pending) ? futurehead::process_result::unreceivable : futurehead::process_result::progress; // Has this source already been received (Malformed)
								if (result.code == futurehead::process_result::progress)
								{
									result.code = result.amount == pending.amount ? futurehead::process_result::progress : futurehead::process_result::balance_mismatch;
									epoch = std::max (epoch, pending.epoch);
								}
							}
						}
						else
						{
							// If there's no link, the balance must remain the same, only the representative can change
							result.code = result.amount.is_zero () ? futurehead::process_result::progress : futurehead::process_result::balance_mismatch;
						}
					}
				}
				if (result.code == futurehead::process_result::progress)
				{
					futurehead::block_details block_details (epoch, is_send, is_receive, false);
					result.code = block_a.difficulty () >= futurehead::work_threshold (block_a.work_version (), block_details) ? futurehead::process_result::progress : futurehead::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
					if (result.code == futurehead::process_result::progress)
					{
						ledger.stats.inc (futurehead::stat::type::ledger, futurehead::stat::detail::state_block);
						block_a.sideband_set (futurehead::block_sideband (block_a.hashables.account /* unused */, 0, 0 /* unused */, info.block_count + 1, futurehead::seconds_since_epoch (), block_details));
						ledger.store.block_put (transaction, hash, block_a);

						if (!info.head.is_zero ())
						{
							// Move existing representation
							ledger.cache.rep_weights.representation_add (info.representative, 0 - info.balance.number ());
						}
						// Add in amount delta
						ledger.cache.rep_weights.representation_add (block_a.representative (), block_a.hashables.balance.number ());

						if (is_send)
						{
							futurehead::pending_key key (block_a.hashables.link, hash);
							futurehead::pending_info info (block_a.hashables.account, result.amount.number (), epoch);
							ledger.store.pending_put (transaction, key, info);
						}
						else if (!block_a.hashables.link.is_zero ())
						{
							ledger.store.pending_del (transaction, futurehead::pending_key (block_a.hashables.account, block_a.hashables.link));
						}

						futurehead::account_info new_info (hash, block_a.representative (), info.open_block.is_zero () ? hash : info.open_block, block_a.hashables.balance, futurehead::seconds_since_epoch (), info.block_count + 1, epoch);
						ledger.change_latest (transaction, block_a.hashables.account, info, new_info);
						if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
						{
							ledger.store.frontier_del (transaction, info.head);
						}
						// Frontier table is unnecessary for state blocks and this also prevents old blocks from being inserted on top of state blocks
						result.account = block_a.hashables.account;
					}
				}
			}
		}
	}
}

void ledger_processor::epoch_block_impl (futurehead::state_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? futurehead::process_result::old : futurehead::process_result::progress; // Have we seen this block before? (Unambiguous)
	if (result.code == futurehead::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != futurehead::signature_verification::valid_epoch)
		{
			result.code = validate_message (ledger.epoch_signer (block_a.hashables.link), hash, block_a.signature) ? futurehead::process_result::bad_signature : futurehead::process_result::progress; // Is this block signed correctly (Unambiguous)
		}
		if (result.code == futurehead::process_result::progress)
		{
			debug_assert (!validate_message (ledger.epoch_signer (block_a.hashables.link), hash, block_a.signature));
			result.verified = futurehead::signature_verification::valid_epoch;
			result.code = block_a.hashables.account.is_zero () ? futurehead::process_result::opened_burn_account : futurehead::process_result::progress; // Is this for the burn account? (Unambiguous)
			if (result.code == futurehead::process_result::progress)
			{
				futurehead::account_info info;
				auto account_error (ledger.store.account_get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					// Account already exists
					result.previous_balance = info.balance;
					result.code = block_a.hashables.previous.is_zero () ? futurehead::process_result::fork : futurehead::process_result::progress; // Has this account already been opened? (Ambigious)
					if (result.code == futurehead::process_result::progress)
					{
						result.code = block_a.hashables.previous == info.head ? futurehead::process_result::progress : futurehead::process_result::fork; // Is the previous block the account's head block? (Ambigious)
						if (result.code == futurehead::process_result::progress)
						{
							result.code = block_a.hashables.representative == info.representative ? futurehead::process_result::progress : futurehead::process_result::representative_mismatch;
						}
					}
				}
				else
				{
					result.previous_balance = 0;
					result.code = block_a.hashables.representative.is_zero () ? futurehead::process_result::progress : futurehead::process_result::representative_mismatch;
					// Non-exisitng account should have pending entries
					if (result.code == futurehead::process_result::progress)
					{
						bool pending_exists = ledger.store.pending_any (transaction, block_a.hashables.account);
						result.code = pending_exists ? futurehead::process_result::progress : futurehead::process_result::block_position;
					}
				}
				if (result.code == futurehead::process_result::progress)
				{
					auto epoch = ledger.network_params.ledger.epochs.epoch (block_a.hashables.link);
					// Must be an epoch for an unopened account or the epoch upgrade must be sequential
					auto is_valid_epoch_upgrade = account_error ? static_cast<std::underlying_type_t<futurehead::epoch>> (epoch) > 0 : futurehead::epochs::is_sequential (info.epoch (), epoch);
					result.code = is_valid_epoch_upgrade ? futurehead::process_result::progress : futurehead::process_result::block_position;
					if (result.code == futurehead::process_result::progress)
					{
						result.code = block_a.hashables.balance == info.balance ? futurehead::process_result::progress : futurehead::process_result::balance_mismatch;
						if (result.code == futurehead::process_result::progress)
						{
							futurehead::block_details block_details (epoch, false, false, true);
							result.code = block_a.difficulty () >= futurehead::work_threshold (block_a.work_version (), block_details) ? futurehead::process_result::progress : futurehead::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
							if (result.code == futurehead::process_result::progress)
							{
								ledger.stats.inc (futurehead::stat::type::ledger, futurehead::stat::detail::epoch_block);
								result.account = block_a.hashables.account;
								result.amount = 0;
								block_a.sideband_set (futurehead::block_sideband (block_a.hashables.account /* unused */, 0, 0 /* unused */, info.block_count + 1, futurehead::seconds_since_epoch (), block_details));
								ledger.store.block_put (transaction, hash, block_a);
								futurehead::account_info new_info (hash, block_a.representative (), info.open_block.is_zero () ? hash : info.open_block, info.balance, futurehead::seconds_since_epoch (), info.block_count + 1, epoch);
								ledger.change_latest (transaction, block_a.hashables.account, info, new_info);
								if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
								{
									ledger.store.frontier_del (transaction, info.head);
								}
								if (epoch == futurehead::epoch::epoch_2)
								{
									if (!ledger.cache.epoch_2_started.exchange (true))
									{
										// The first epoch 2 block has been seen
										if (ledger.epoch_2_started_cb)
										{
											ledger.epoch_2_started_cb ();
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
}

void ledger_processor::change_block (futurehead::change_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? futurehead::process_result::old : futurehead::process_result::progress; // Have we seen this block before? (Harmless)
	if (result.code == futurehead::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? futurehead::process_result::progress : futurehead::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result.code == futurehead::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? futurehead::process_result::progress : futurehead::process_result::block_position;
			if (result.code == futurehead::process_result::progress)
			{
				auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? futurehead::process_result::fork : futurehead::process_result::progress;
				if (result.code == futurehead::process_result::progress)
				{
					futurehead::account_info info;
					auto latest_error (ledger.store.account_get (transaction, account, info));
					(void)latest_error;
					debug_assert (!latest_error);
					debug_assert (info.head == block_a.hashables.previous);
					// Validate block if not verified outside of ledger
					if (result.verified != futurehead::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? futurehead::process_result::bad_signature : futurehead::process_result::progress; // Is this block signed correctly (Malformed)
					}
					if (result.code == futurehead::process_result::progress)
					{
						futurehead::block_details block_details (futurehead::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
						result.code = block_a.difficulty () >= futurehead::work_threshold (block_a.work_version (), block_details) ? futurehead::process_result::progress : futurehead::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
						if (result.code == futurehead::process_result::progress)
						{
							debug_assert (!validate_message (account, hash, block_a.signature));
							result.verified = futurehead::signature_verification::valid;
							block_a.sideband_set (futurehead::block_sideband (account, 0, info.balance, info.block_count + 1, futurehead::seconds_since_epoch (), block_details));
							ledger.store.block_put (transaction, hash, block_a);
							auto balance (ledger.balance (transaction, block_a.hashables.previous));
							ledger.cache.rep_weights.representation_add (block_a.representative (), balance);
							ledger.cache.rep_weights.representation_add (info.representative, 0 - balance);
							futurehead::account_info new_info (hash, block_a.representative (), info.open_block, info.balance, futurehead::seconds_since_epoch (), info.block_count + 1, futurehead::epoch::epoch_0);
							ledger.change_latest (transaction, account, info, new_info);
							ledger.store.frontier_del (transaction, block_a.hashables.previous);
							ledger.store.frontier_put (transaction, hash, account);
							result.account = account;
							result.amount = 0;
							result.previous_balance = info.balance;
							ledger.stats.inc (futurehead::stat::type::ledger, futurehead::stat::detail::change);
						}
					}
				}
			}
		}
	}
}

void ledger_processor::send_block (futurehead::send_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? futurehead::process_result::old : futurehead::process_result::progress; // Have we seen this block before? (Harmless)
	if (result.code == futurehead::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? futurehead::process_result::progress : futurehead::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result.code == futurehead::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? futurehead::process_result::progress : futurehead::process_result::block_position;
			if (result.code == futurehead::process_result::progress)
			{
				auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? futurehead::process_result::fork : futurehead::process_result::progress;
				if (result.code == futurehead::process_result::progress)
				{
					// Validate block if not verified outside of ledger
					if (result.verified != futurehead::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? futurehead::process_result::bad_signature : futurehead::process_result::progress; // Is this block signed correctly (Malformed)
					}
					if (result.code == futurehead::process_result::progress)
					{
						futurehead::block_details block_details (futurehead::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
						result.code = block_a.difficulty () >= futurehead::work_threshold (block_a.work_version (), block_details) ? futurehead::process_result::progress : futurehead::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
						if (result.code == futurehead::process_result::progress)
						{
							debug_assert (!validate_message (account, hash, block_a.signature));
							result.verified = futurehead::signature_verification::valid;
							futurehead::account_info info;
							auto latest_error (ledger.store.account_get (transaction, account, info));
							(void)latest_error;
							debug_assert (!latest_error);
							debug_assert (info.head == block_a.hashables.previous);
							result.code = info.balance.number () >= block_a.hashables.balance.number () ? futurehead::process_result::progress : futurehead::process_result::negative_spend; // Is this trying to spend a negative amount (Malicious)
							if (result.code == futurehead::process_result::progress)
							{
								auto amount (info.balance.number () - block_a.hashables.balance.number ());
								ledger.cache.rep_weights.representation_add (info.representative, 0 - amount);
								block_a.sideband_set (futurehead::block_sideband (account, 0, block_a.hashables.balance /* unused */, info.block_count + 1, futurehead::seconds_since_epoch (), block_details));
								ledger.store.block_put (transaction, hash, block_a);
								futurehead::account_info new_info (hash, info.representative, info.open_block, block_a.hashables.balance, futurehead::seconds_since_epoch (), info.block_count + 1, futurehead::epoch::epoch_0);
								ledger.change_latest (transaction, account, info, new_info);
								ledger.store.pending_put (transaction, futurehead::pending_key (block_a.hashables.destination, hash), { account, amount, futurehead::epoch::epoch_0 });
								ledger.store.frontier_del (transaction, block_a.hashables.previous);
								ledger.store.frontier_put (transaction, hash, account);
								result.account = account;
								result.amount = amount;
								result.pending_account = block_a.hashables.destination;
								result.previous_balance = info.balance;
								ledger.stats.inc (futurehead::stat::type::ledger, futurehead::stat::detail::send);
							}
						}
					}
				}
			}
		}
	}
}

void ledger_processor::receive_block (futurehead::receive_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? futurehead::process_result::old : futurehead::process_result::progress; // Have we seen this block already?  (Harmless)
	if (result.code == futurehead::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? futurehead::process_result::progress : futurehead::process_result::gap_previous;
		if (result.code == futurehead::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? futurehead::process_result::progress : futurehead::process_result::block_position;
			if (result.code == futurehead::process_result::progress)
			{
				auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? futurehead::process_result::gap_previous : futurehead::process_result::progress; //Have we seen the previous block? No entries for account at all (Harmless)
				if (result.code == futurehead::process_result::progress)
				{
					// Validate block if not verified outside of ledger
					if (result.verified != futurehead::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? futurehead::process_result::bad_signature : futurehead::process_result::progress; // Is the signature valid (Malformed)
					}
					if (result.code == futurehead::process_result::progress)
					{
						debug_assert (!validate_message (account, hash, block_a.signature));
						result.verified = futurehead::signature_verification::valid;
						result.code = ledger.store.source_exists (transaction, block_a.hashables.source) ? futurehead::process_result::progress : futurehead::process_result::gap_source; // Have we seen the source block already? (Harmless)
						if (result.code == futurehead::process_result::progress)
						{
							futurehead::account_info info;
							ledger.store.account_get (transaction, account, info);
							result.code = info.head == block_a.hashables.previous ? futurehead::process_result::progress : futurehead::process_result::gap_previous; // Block doesn't immediately follow latest block (Harmless)
							if (result.code == futurehead::process_result::progress)
							{
								futurehead::pending_key key (account, block_a.hashables.source);
								futurehead::pending_info pending;
								result.code = ledger.store.pending_get (transaction, key, pending) ? futurehead::process_result::unreceivable : futurehead::process_result::progress; // Has this source already been received (Malformed)
								if (result.code == futurehead::process_result::progress)
								{
									result.code = pending.epoch == futurehead::epoch::epoch_0 ? futurehead::process_result::progress : futurehead::process_result::unreceivable; // Are we receiving a state-only send? (Malformed)
									if (result.code == futurehead::process_result::progress)
									{
										futurehead::block_details block_details (futurehead::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
										result.code = block_a.difficulty () >= futurehead::work_threshold (block_a.work_version (), block_details) ? futurehead::process_result::progress : futurehead::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
										if (result.code == futurehead::process_result::progress)
										{
											auto new_balance (info.balance.number () + pending.amount.number ());
											futurehead::account_info source_info;
											auto error (ledger.store.account_get (transaction, pending.source, source_info));
											(void)error;
											debug_assert (!error);
											ledger.store.pending_del (transaction, key);
											block_a.sideband_set (futurehead::block_sideband (account, 0, new_balance, info.block_count + 1, futurehead::seconds_since_epoch (), block_details));
											ledger.store.block_put (transaction, hash, block_a);
											futurehead::account_info new_info (hash, info.representative, info.open_block, new_balance, futurehead::seconds_since_epoch (), info.block_count + 1, futurehead::epoch::epoch_0);
											ledger.change_latest (transaction, account, info, new_info);
											ledger.cache.rep_weights.representation_add (info.representative, pending.amount.number ());
											ledger.store.frontier_del (transaction, block_a.hashables.previous);
											ledger.store.frontier_put (transaction, hash, account);
											result.account = account;
											result.amount = pending.amount;
											result.previous_balance = info.balance;
											ledger.stats.inc (futurehead::stat::type::ledger, futurehead::stat::detail::receive);
										}
									}
								}
							}
						}
					}
				}
				else
				{
					result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? futurehead::process_result::fork : futurehead::process_result::gap_previous; // If we have the block but it's not the latest we have a signed fork (Malicious)
				}
			}
		}
	}
}

void ledger_processor::open_block (futurehead::open_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? futurehead::process_result::old : futurehead::process_result::progress; // Have we seen this block already? (Harmless)
	if (result.code == futurehead::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != futurehead::signature_verification::valid)
		{
			result.code = validate_message (block_a.hashables.account, hash, block_a.signature) ? futurehead::process_result::bad_signature : futurehead::process_result::progress; // Is the signature valid (Malformed)
		}
		if (result.code == futurehead::process_result::progress)
		{
			debug_assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result.verified = futurehead::signature_verification::valid;
			result.code = ledger.store.source_exists (transaction, block_a.hashables.source) ? futurehead::process_result::progress : futurehead::process_result::gap_source; // Have we seen the source block? (Harmless)
			if (result.code == futurehead::process_result::progress)
			{
				futurehead::account_info info;
				result.code = ledger.store.account_get (transaction, block_a.hashables.account, info) ? futurehead::process_result::progress : futurehead::process_result::fork; // Has this account already been opened? (Malicious)
				if (result.code == futurehead::process_result::progress)
				{
					futurehead::pending_key key (block_a.hashables.account, block_a.hashables.source);
					futurehead::pending_info pending;
					result.code = ledger.store.pending_get (transaction, key, pending) ? futurehead::process_result::unreceivable : futurehead::process_result::progress; // Has this source already been received (Malformed)
					if (result.code == futurehead::process_result::progress)
					{
						result.code = block_a.hashables.account == ledger.network_params.ledger.burn_account ? futurehead::process_result::opened_burn_account : futurehead::process_result::progress; // Is it burning 0 account? (Malicious)
						if (result.code == futurehead::process_result::progress)
						{
							result.code = pending.epoch == futurehead::epoch::epoch_0 ? futurehead::process_result::progress : futurehead::process_result::unreceivable; // Are we receiving a state-only send? (Malformed)
							if (result.code == futurehead::process_result::progress)
							{
								futurehead::block_details block_details (futurehead::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
								result.code = block_a.difficulty () >= futurehead::work_threshold (block_a.work_version (), block_details) ? futurehead::process_result::progress : futurehead::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
								if (result.code == futurehead::process_result::progress)
								{
									futurehead::account_info source_info;
									auto error (ledger.store.account_get (transaction, pending.source, source_info));
									(void)error;
									debug_assert (!error);
									ledger.store.pending_del (transaction, key);
									block_a.sideband_set (futurehead::block_sideband (block_a.hashables.account, 0, pending.amount, 1, futurehead::seconds_since_epoch (), block_details));
									ledger.store.block_put (transaction, hash, block_a);
									futurehead::account_info new_info (hash, block_a.representative (), hash, pending.amount.number (), futurehead::seconds_since_epoch (), 1, futurehead::epoch::epoch_0);
									ledger.change_latest (transaction, block_a.hashables.account, info, new_info);
									ledger.cache.rep_weights.representation_add (block_a.representative (), pending.amount.number ());
									ledger.store.frontier_put (transaction, hash, block_a.hashables.account);
									result.account = block_a.hashables.account;
									result.amount = pending.amount;
									result.previous_balance = 0;
									ledger.stats.inc (futurehead::stat::type::ledger, futurehead::stat::detail::open);
								}
							}
						}
					}
				}
			}
		}
	}
}

ledger_processor::ledger_processor (futurehead::ledger & ledger_a, futurehead::write_transaction const & transaction_a, futurehead::signature_verification verification_a) :
ledger (ledger_a),
transaction (transaction_a),
verification (verification_a)
{
	result.verified = verification;
}
} // namespace

futurehead::ledger::ledger (futurehead::block_store & store_a, futurehead::stat & stat_a, futurehead::generate_cache const & generate_cache_a, std::function<void()> epoch_2_started_cb_a) :
store (store_a),
stats (stat_a),
check_bootstrap_weights (true),
epoch_2_started_cb (epoch_2_started_cb_a)
{
	if (!store.init_error ())
	{
		auto transaction = store.tx_begin_read ();
		if (generate_cache_a.reps || generate_cache_a.account_count || generate_cache_a.epoch_2)
		{
			bool epoch_2_started_l{ false };
			for (auto i (store.latest_begin (transaction)), n (store.latest_end ()); i != n; ++i)
			{
				futurehead::account_info const & info (i->second);
				cache.rep_weights.representation_add (info.representative, info.balance.number ());
				++cache.account_count;
				epoch_2_started_l = epoch_2_started_l || info.epoch () == futurehead::epoch::epoch_2;
			}
			cache.epoch_2_started.store (epoch_2_started_l);
		}

		if (generate_cache_a.cemented_count)
		{
			for (auto i (store.confirmation_height_begin (transaction)), n (store.confirmation_height_end ()); i != n; ++i)
			{
				cache.cemented_count += i->second.height;
			}
		}

		if (generate_cache_a.unchecked_count)
		{
			cache.unchecked_count = store.unchecked_count (transaction);
		}

		cache.block_count = store.block_count (transaction).sum ();
	}
}

// Balance for account containing hash
futurehead::uint128_t futurehead::ledger::balance (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const
{
	return hash_a.is_zero () ? 0 : store.block_balance (transaction_a, hash_a);
}

// Balance for an account by account number
futurehead::uint128_t futurehead::ledger::account_balance (futurehead::transaction const & transaction_a, futurehead::account const & account_a)
{
	futurehead::uint128_t result (0);
	futurehead::account_info info;
	auto none (store.account_get (transaction_a, account_a, info));
	if (!none)
	{
		result = info.balance.number ();
	}
	return result;
}

futurehead::uint128_t futurehead::ledger::account_pending (futurehead::transaction const & transaction_a, futurehead::account const & account_a)
{
	futurehead::uint128_t result (0);
	futurehead::account end (account_a.number () + 1);
	for (auto i (store.pending_begin (transaction_a, futurehead::pending_key (account_a, 0))), n (store.pending_begin (transaction_a, futurehead::pending_key (end, 0))); i != n; ++i)
	{
		futurehead::pending_info const & info (i->second);
		result += info.amount.number ();
	}
	return result;
}

futurehead::process_return futurehead::ledger::process (futurehead::write_transaction const & transaction_a, futurehead::block & block_a, futurehead::signature_verification verification)
{
	debug_assert (!futurehead::work_validate_entry (block_a) || network_params.network.is_test_network ());
	ledger_processor processor (*this, transaction_a, verification);
	block_a.visit (processor);
	if (processor.result.code == futurehead::process_result::progress)
	{
		++cache.block_count;
	}
	return processor.result;
}

futurehead::block_hash futurehead::ledger::representative (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a)
{
	auto result (representative_calculated (transaction_a, hash_a));
	debug_assert (result.is_zero () || store.block_exists (transaction_a, result));
	return result;
}

futurehead::block_hash futurehead::ledger::representative_calculated (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a)
{
	representative_visitor visitor (transaction_a, store);
	visitor.compute (hash_a);
	return visitor.result;
}

bool futurehead::ledger::block_exists (futurehead::block_hash const & hash_a)
{
	return store.block_exists (store.tx_begin_read (), hash_a);
}

bool futurehead::ledger::block_exists (futurehead::block_type type, futurehead::block_hash const & hash_a)
{
	return store.block_exists (store.tx_begin_read (), type, hash_a);
}

std::string futurehead::ledger::block_text (char const * hash_a)
{
	return block_text (futurehead::block_hash (hash_a));
}

std::string futurehead::ledger::block_text (futurehead::block_hash const & hash_a)
{
	std::string result;
	auto transaction (store.tx_begin_read ());
	auto block (store.block_get (transaction, hash_a));
	if (block != nullptr)
	{
		block->serialize_json (result);
	}
	return result;
}

bool futurehead::ledger::is_send (futurehead::transaction const & transaction_a, futurehead::state_block const & block_a) const
{
	/*
	 * if block_a does not have a sideband, then is_send()
	 * requires that the previous block exists in the database.
	 * This is because it must retrieve the balance of the previous block.
	 */
	debug_assert (block_a.has_sideband () || block_a.hashables.previous.is_zero () || store.block_exists (transaction_a, block_a.hashables.previous));

	bool result (false);
	if (block_a.has_sideband ())
	{
		result = block_a.sideband ().details.is_send;
	}
	else
	{
		futurehead::block_hash previous (block_a.hashables.previous);
		if (!previous.is_zero ())
		{
			if (block_a.hashables.balance < balance (transaction_a, previous))
			{
				result = true;
			}
		}
	}
	return result;
}

futurehead::account const & futurehead::ledger::block_destination (futurehead::transaction const & transaction_a, futurehead::block const & block_a)
{
	futurehead::send_block const * send_block (dynamic_cast<futurehead::send_block const *> (&block_a));
	futurehead::state_block const * state_block (dynamic_cast<futurehead::state_block const *> (&block_a));
	if (send_block != nullptr)
	{
		return send_block->hashables.destination;
	}
	else if (state_block != nullptr && is_send (transaction_a, *state_block))
	{
		return state_block->hashables.link;
	}
	static futurehead::account result (0);
	return result;
}

futurehead::block_hash futurehead::ledger::block_source (futurehead::transaction const & transaction_a, futurehead::block const & block_a)
{
	/*
	 * block_source() requires that the previous block of the block
	 * passed in exist in the database.  This is because it will try
	 * to check account balances to determine if it is a send block.
	 */
	debug_assert (block_a.previous ().is_zero () || store.block_exists (transaction_a, block_a.previous ()));

	// If block_a.source () is nonzero, then we have our source.
	// However, universal blocks will always return zero.
	futurehead::block_hash result (block_a.source ());
	futurehead::state_block const * state_block (dynamic_cast<futurehead::state_block const *> (&block_a));
	if (state_block != nullptr && !is_send (transaction_a, *state_block))
	{
		result = state_block->hashables.link;
	}
	return result;
}

// Vote weight of an account
futurehead::uint128_t futurehead::ledger::weight (futurehead::account const & account_a)
{
	if (check_bootstrap_weights.load ())
	{
		if (cache.block_count < bootstrap_weight_max_blocks)
		{
			auto weight = bootstrap_weights.find (account_a);
			if (weight != bootstrap_weights.end ())
			{
				return weight->second;
			}
		}
		else
		{
			check_bootstrap_weights = false;
		}
	}
	return cache.rep_weights.representation_get (account_a);
}

// Rollback blocks until `block_a' doesn't exist or it tries to penetrate the confirmation height
bool futurehead::ledger::rollback (futurehead::write_transaction const & transaction_a, futurehead::block_hash const & block_a, std::vector<std::shared_ptr<futurehead::block>> & list_a)
{
	debug_assert (store.block_exists (transaction_a, block_a));
	auto account_l (account (transaction_a, block_a));
	auto block_account_height (store.block_account_height (transaction_a, block_a));
	rollback_visitor rollback (transaction_a, *this, list_a);
	futurehead::account_info account_info;
	auto error (false);
	while (!error && store.block_exists (transaction_a, block_a))
	{
		futurehead::confirmation_height_info confirmation_height_info;
		auto latest_error = store.confirmation_height_get (transaction_a, account_l, confirmation_height_info);
		debug_assert (!latest_error);
		(void)latest_error;
		if (block_account_height > confirmation_height_info.height)
		{
			latest_error = store.account_get (transaction_a, account_l, account_info);
			debug_assert (!latest_error);
			auto block (store.block_get (transaction_a, account_info.head));
			list_a.push_back (block);
			block->visit (rollback);
			error = rollback.error;
			if (!error)
			{
				--cache.block_count;
			}
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool futurehead::ledger::rollback (futurehead::write_transaction const & transaction_a, futurehead::block_hash const & block_a)
{
	std::vector<std::shared_ptr<futurehead::block>> rollback_list;
	return rollback (transaction_a, block_a, rollback_list);
}

// Return account containing hash
futurehead::account futurehead::ledger::account (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const
{
	return store.block_account (transaction_a, hash_a);
}

// Return amount decrease or increase for block
futurehead::uint128_t futurehead::ledger::amount (futurehead::transaction const & transaction_a, futurehead::account const & account_a)
{
	release_assert (account_a == network_params.ledger.genesis_account);
	return network_params.ledger.genesis_amount;
}

futurehead::uint128_t futurehead::ledger::amount (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a)
{
	auto block (store.block_get (transaction_a, hash_a));
	auto block_balance (balance (transaction_a, hash_a));
	auto previous_balance (balance (transaction_a, block->previous ()));
	return block_balance > previous_balance ? block_balance - previous_balance : previous_balance - block_balance;
}

// Return latest block for account
futurehead::block_hash futurehead::ledger::latest (futurehead::transaction const & transaction_a, futurehead::account const & account_a)
{
	futurehead::account_info info;
	auto latest_error (store.account_get (transaction_a, account_a, info));
	return latest_error ? 0 : info.head;
}

// Return latest root for account, account number if there are no blocks for this account.
futurehead::root futurehead::ledger::latest_root (futurehead::transaction const & transaction_a, futurehead::account const & account_a)
{
	futurehead::account_info info;
	if (store.account_get (transaction_a, account_a, info))
	{
		return account_a;
	}
	else
	{
		return info.head;
	}
}

void futurehead::ledger::dump_account_chain (futurehead::account const & account_a, std::ostream & stream)
{
	auto transaction (store.tx_begin_read ());
	auto hash (latest (transaction, account_a));
	while (!hash.is_zero ())
	{
		auto block (store.block_get (transaction, hash));
		debug_assert (block != nullptr);
		stream << hash.to_string () << std::endl;
		hash = block->previous ();
	}
}

bool futurehead::ledger::could_fit (futurehead::transaction const & transaction_a, futurehead::block const & block_a)
{
	auto dependencies (dependent_blocks (transaction_a, block_a));
	return std::all_of (dependencies.begin (), dependencies.end (), [this, &transaction_a](futurehead::block_hash const & hash_a) {
		return hash_a.is_zero () || store.block_exists (transaction_a, hash_a);
	});
}

bool futurehead::ledger::can_vote (futurehead::transaction const & transaction_a, futurehead::block const & block_a)
{
	auto dependencies (dependent_blocks (transaction_a, block_a));
	return std::all_of (dependencies.begin (), dependencies.end (), [this, &transaction_a](futurehead::block_hash const & hash_a) {
		auto result (hash_a.is_zero ());
		if (!result)
		{
			result = false;
			auto block (store.block_get (transaction_a, hash_a));
			if (block != nullptr)
			{
				futurehead::confirmation_height_info height;
				auto error = store.confirmation_height_get (transaction_a, block->account ().is_zero () ? block->sideband ().account : block->account (), height);
				debug_assert (!error);
				result = block->sideband ().height <= height.height;
			}
		}
		return result;
	});
}

bool futurehead::ledger::is_epoch_link (futurehead::link const & link_a)
{
	return network_params.ledger.epochs.is_epoch_link (link_a);
}

class dependent_block_visitor : public futurehead::block_visitor
{
public:
	dependent_block_visitor (futurehead::ledger & ledger_a, futurehead::transaction const & transaction_a) :
	ledger (ledger_a),
	transaction (transaction_a),
	result ({ 0, 0 })
	{
	}
	void send_block (futurehead::send_block const & block_a) override
	{
		result[0] = block_a.previous ();
	}
	void receive_block (futurehead::receive_block const & block_a) override
	{
		result[0] = block_a.previous ();
		result[1] = block_a.source ();
	}
	void open_block (futurehead::open_block const & block_a) override
	{
		if (block_a.source () != ledger.network_params.ledger.genesis_account)
		{
			result[0] = block_a.source ();
		}
	}
	void change_block (futurehead::change_block const & block_a) override
	{
		result[0] = block_a.previous ();
	}
	void state_block (futurehead::state_block const & block_a) override
	{
		result[0] = block_a.hashables.previous;
		result[1] = block_a.hashables.link;
		// ledger.is_send will check the sideband first, if block_a has a loaded sideband the check that previous block exists can be skipped
		if (ledger.is_epoch_link (block_a.hashables.link) || ((block_a.has_sideband () || ledger.store.block_exists (transaction, block_a.hashables.previous)) && ledger.is_send (transaction, block_a)))
		{
			result[1].clear ();
		}
	}
	futurehead::ledger & ledger;
	futurehead::transaction const & transaction;
	std::array<futurehead::block_hash, 2> result;
};

std::array<futurehead::block_hash, 2> futurehead::ledger::dependent_blocks (futurehead::transaction const & transaction_a, futurehead::block const & block_a)
{
	dependent_block_visitor visitor (*this, transaction_a);
	block_a.visit (visitor);
	return visitor.result;
}

futurehead::account const & futurehead::ledger::epoch_signer (futurehead::link const & link_a) const
{
	return network_params.ledger.epochs.signer (network_params.ledger.epochs.epoch (link_a));
}

futurehead::link const & futurehead::ledger::epoch_link (futurehead::epoch epoch_a) const
{
	return network_params.ledger.epochs.link (epoch_a);
}

void futurehead::ledger::change_latest (futurehead::write_transaction const & transaction_a, futurehead::account const & account_a, futurehead::account_info const & old_a, futurehead::account_info const & new_a)
{
	if (!new_a.head.is_zero ())
	{
		if (old_a.head.is_zero () && new_a.open_block == new_a.head)
		{
			debug_assert (!store.confirmation_height_exists (transaction_a, account_a));
			store.confirmation_height_put (transaction_a, account_a, { 0, futurehead::block_hash (0) });
			++cache.account_count;
		}
		if (!old_a.head.is_zero () && old_a.epoch () != new_a.epoch ())
		{
			// store.account_put won't erase existing entries if they're in different tables
			store.account_del (transaction_a, account_a);
		}
		store.account_put (transaction_a, account_a, new_a);
	}
	else
	{
		store.confirmation_height_del (transaction_a, account_a);
		store.account_del (transaction_a, account_a);
		debug_assert (cache.account_count > 0);
		--cache.account_count;
	}
}

std::shared_ptr<futurehead::block> futurehead::ledger::successor (futurehead::transaction const & transaction_a, futurehead::qualified_root const & root_a)
{
	futurehead::block_hash successor (0);
	auto get_from_previous = false;
	if (root_a.previous ().is_zero ())
	{
		futurehead::account_info info;
		if (!store.account_get (transaction_a, root_a.root (), info))
		{
			successor = info.open_block;
		}
		else
		{
			get_from_previous = true;
		}
	}
	else
	{
		get_from_previous = true;
	}

	if (get_from_previous)
	{
		successor = store.block_successor (transaction_a, root_a.previous ());
	}
	std::shared_ptr<futurehead::block> result;
	if (!successor.is_zero ())
	{
		result = store.block_get (transaction_a, successor);
	}
	debug_assert (successor.is_zero () || result != nullptr);
	return result;
}

std::shared_ptr<futurehead::block> futurehead::ledger::forked_block (futurehead::transaction const & transaction_a, futurehead::block const & block_a)
{
	debug_assert (!store.block_exists (transaction_a, block_a.type (), block_a.hash ()));
	auto root (block_a.root ());
	debug_assert (store.block_exists (transaction_a, root) || store.account_exists (transaction_a, root));
	auto result (store.block_get (transaction_a, store.block_successor (transaction_a, root)));
	if (result == nullptr)
	{
		futurehead::account_info info;
		auto error (store.account_get (transaction_a, root, info));
		(void)error;
		debug_assert (!error);
		result = store.block_get (transaction_a, info.open_block);
		debug_assert (result != nullptr);
	}
	return result;
}

std::shared_ptr<futurehead::block> futurehead::ledger::backtrack (futurehead::transaction const & transaction_a, std::shared_ptr<futurehead::block> const & start_a, uint64_t jumps_a)
{
	auto block = start_a;
	while (jumps_a > 0 && block != nullptr && !block->previous ().is_zero ())
	{
		block = store.block_get (transaction_a, block->previous ());
		debug_assert (block != nullptr);
		--jumps_a;
	}
	debug_assert (block == nullptr || block->previous ().is_zero () || jumps_a == 0);
	return block;
}

bool futurehead::ledger::block_confirmed (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const
{
	auto confirmed (false);
	auto block_height (store.block_account_height (transaction_a, hash_a));
	if (block_height > 0) // 0 indicates that the block doesn't exist
	{
		futurehead::confirmation_height_info confirmation_height_info;
		release_assert (!store.confirmation_height_get (transaction_a, account (transaction_a, hash_a), confirmation_height_info));
		confirmed = (confirmation_height_info.height >= block_height);
	}
	return confirmed;
}

bool futurehead::ledger::block_not_confirmed_or_not_exists (futurehead::block const & block_a) const
{
	bool result (true);
	auto hash (block_a.hash ());
	auto transaction (store.tx_begin_read ());
	if (store.block_exists (transaction, block_a.type (), hash))
	{
		result = !block_confirmed (transaction, hash);
	}
	return result;
}

std::unique_ptr<futurehead::container_info_component> futurehead::collect_container_info (ledger & ledger, const std::string & name)
{
	auto count = ledger.bootstrap_weights_size.load ();
	auto sizeof_element = sizeof (decltype (ledger.bootstrap_weights)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "bootstrap_weights", count, sizeof_element }));
	composite->add_component (collect_container_info (ledger.cache.rep_weights, "rep_weights"));
	return composite;
}
