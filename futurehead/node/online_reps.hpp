#pragma once

#include <futurehead/lib/numbers.hpp>
#include <futurehead/lib/utility.hpp>

#include <memory>
#include <unordered_set>
#include <vector>

namespace futurehead
{
class ledger;
class network_params;
class transaction;

/** Track online representatives and trend online weight */
class online_reps final
{
public:
	online_reps (futurehead::ledger & ledger_a, futurehead::network_params & network_params_a, futurehead::uint128_t minimum_a);
	/** Add voting account \p rep_account to the set of online representatives */
	void observe (futurehead::account const & rep_account);
	/** Called periodically to sample online weight */
	void sample ();
	/** Returns the trended online stake, but never less than configured minimum */
	futurehead::uint128_t online_stake () const;
	/** List of online representatives */
	std::vector<futurehead::account> list ();

private:
	futurehead::uint128_t trend (futurehead::transaction &);
	mutable std::mutex mutex;
	futurehead::ledger & ledger;
	futurehead::network_params & network_params;
	std::unordered_set<futurehead::account> reps;
	futurehead::uint128_t online;
	futurehead::uint128_t minimum;

	friend std::unique_ptr<container_info_component> collect_container_info (online_reps & online_reps, const std::string & name);
};

std::unique_ptr<container_info_component> collect_container_info (online_reps & online_reps, const std::string & name);
}
