#pragma once

#include <futurehead/lib/numbers.hpp>
#include <futurehead/lib/utility.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>

namespace futurehead
{
class block_store;
class transaction;

class rep_weights
{
public:
	void representation_add (futurehead::account const & source_a, futurehead::uint128_t const & amount_a);
	futurehead::uint128_t representation_get (futurehead::account const & account_a);
	void representation_put (futurehead::account const & account_a, futurehead::uint128_union const & representation_a);
	std::unordered_map<futurehead::account, futurehead::uint128_t> get_rep_amounts ();

private:
	std::mutex mutex;
	std::unordered_map<futurehead::account, futurehead::uint128_t> rep_amounts;
	void put (futurehead::account const & account_a, futurehead::uint128_union const & representation_a);
	futurehead::uint128_t get (futurehead::account const & account_a);

	friend std::unique_ptr<container_info_component> collect_container_info (rep_weights &, const std::string &);
};

std::unique_ptr<container_info_component> collect_container_info (rep_weights &, const std::string &);
}
