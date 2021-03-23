#pragma once

#include <futurehead/node/node_observers.hpp>

#include <string>

namespace futurehead
{
class node;

enum class payment_status
{
	not_a_status,
	unknown,
	nothing, // Timeout and nothing was received
	//insufficient, // Timeout and not enough was received
	//over, // More than requested received
	//success_fork, // Amount received but it involved a fork
	success // Amount received
};
class json_payment_observer final : public std::enable_shared_from_this<futurehead::json_payment_observer>
{
public:
	json_payment_observer (futurehead::node &, std::function<void(std::string const &)> const &, futurehead::account const &, futurehead::amount const &);
	void start (uint64_t);
	void observe ();
	void complete (futurehead::payment_status);
	std::mutex mutex;
	futurehead::condition_variable condition;
	futurehead::node & node;
	futurehead::account account;
	futurehead::amount amount;
	std::function<void(std::string const &)> response;
	std::atomic_flag completed;
};
}
