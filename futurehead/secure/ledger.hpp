#pragma once

#include <futurehead/lib/rep_weights.hpp>
#include <futurehead/secure/common.hpp>

#include <map>

namespace futurehead
{
class block_store;
class stat;
class write_transaction;

using tally_t = std::map<futurehead::uint128_t, std::shared_ptr<futurehead::block>, std::greater<futurehead::uint128_t>>;
class ledger final
{
public:
	ledger (futurehead::block_store &, futurehead::stat &, futurehead::generate_cache const & = futurehead::generate_cache (), std::function<void()> = nullptr);
	futurehead::account account (futurehead::transaction const &, futurehead::block_hash const &) const;
	futurehead::uint128_t amount (futurehead::transaction const &, futurehead::account const &);
	futurehead::uint128_t amount (futurehead::transaction const &, futurehead::block_hash const &);
	futurehead::uint128_t balance (futurehead::transaction const &, futurehead::block_hash const &) const;
	futurehead::uint128_t account_balance (futurehead::transaction const &, futurehead::account const &);
	futurehead::uint128_t account_pending (futurehead::transaction const &, futurehead::account const &);
	futurehead::uint128_t weight (futurehead::account const &);
	std::shared_ptr<futurehead::block> successor (futurehead::transaction const &, futurehead::qualified_root const &);
	std::shared_ptr<futurehead::block> forked_block (futurehead::transaction const &, futurehead::block const &);
	std::shared_ptr<futurehead::block> backtrack (futurehead::transaction const &, std::shared_ptr<futurehead::block> const &, uint64_t);
	bool block_confirmed (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const;
	bool block_not_confirmed_or_not_exists (futurehead::block const & block_a) const;
	futurehead::block_hash latest (futurehead::transaction const &, futurehead::account const &);
	futurehead::root latest_root (futurehead::transaction const &, futurehead::account const &);
	futurehead::block_hash representative (futurehead::transaction const &, futurehead::block_hash const &);
	futurehead::block_hash representative_calculated (futurehead::transaction const &, futurehead::block_hash const &);
	bool block_exists (futurehead::block_hash const &);
	bool block_exists (futurehead::block_type, futurehead::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (futurehead::block_hash const &);
	bool is_send (futurehead::transaction const &, futurehead::state_block const &) const;
	futurehead::account const & block_destination (futurehead::transaction const &, futurehead::block const &);
	futurehead::block_hash block_source (futurehead::transaction const &, futurehead::block const &);
	futurehead::process_return process (futurehead::write_transaction const &, futurehead::block &, futurehead::signature_verification = futurehead::signature_verification::unknown);
	bool rollback (futurehead::write_transaction const &, futurehead::block_hash const &, std::vector<std::shared_ptr<futurehead::block>> &);
	bool rollback (futurehead::write_transaction const &, futurehead::block_hash const &);
	void change_latest (futurehead::write_transaction const &, futurehead::account const &, futurehead::account_info const &, futurehead::account_info const &);
	void dump_account_chain (futurehead::account const &, std::ostream & = std::cout);
	bool could_fit (futurehead::transaction const &, futurehead::block const &);
	bool can_vote (futurehead::transaction const &, futurehead::block const &);
	bool is_epoch_link (futurehead::link const &);
	std::array<futurehead::block_hash, 2> dependent_blocks (futurehead::transaction const &, futurehead::block const &);
	futurehead::account const & epoch_signer (futurehead::link const &) const;
	futurehead::link const & epoch_link (futurehead::epoch) const;
	static futurehead::uint128_t const unit;
	futurehead::network_params network_params;
	futurehead::block_store & store;
	futurehead::ledger_cache cache;
	futurehead::stat & stats;
	std::unordered_map<futurehead::account, futurehead::uint128_t> bootstrap_weights;
	std::atomic<size_t> bootstrap_weights_size{ 0 };
	uint64_t bootstrap_weight_max_blocks{ 1 };
	std::atomic<bool> check_bootstrap_weights;
	std::function<void()> epoch_2_started_cb;
};

std::unique_ptr<container_info_component> collect_container_info (ledger & ledger, const std::string & name);
}
