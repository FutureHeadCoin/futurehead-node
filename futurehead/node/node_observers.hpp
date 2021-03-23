#pragma once

#include <futurehead/lib/numbers.hpp>
#include <futurehead/lib/utility.hpp>
#include <futurehead/node/active_transactions.hpp>
#include <futurehead/node/transport/transport.hpp>

namespace futurehead
{
class telemetry;
class node_observers final
{
public:
	using blocks_t = futurehead::observer_set<futurehead::election_status const &, futurehead::account const &, futurehead::uint128_t const &, bool>;
	blocks_t blocks;
	futurehead::observer_set<bool> wallet;
	futurehead::observer_set<std::shared_ptr<futurehead::vote>, std::shared_ptr<futurehead::transport::channel>, futurehead::vote_code> vote;
	futurehead::observer_set<futurehead::block_hash const &> active_stopped;
	futurehead::observer_set<futurehead::account const &, bool> account_balance;
	futurehead::observer_set<std::shared_ptr<futurehead::transport::channel>> endpoint;
	futurehead::observer_set<> disconnect;
	futurehead::observer_set<uint64_t> difficulty;
	futurehead::observer_set<futurehead::root const &> work_cancel;
	futurehead::observer_set<futurehead::telemetry_data const &, futurehead::endpoint const &> telemetry;
};

std::unique_ptr<container_info_component> collect_container_info (node_observers & node_observers, const std::string & name);
}
