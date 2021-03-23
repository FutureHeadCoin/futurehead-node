#pragma once

#include <futurehead/node/node_observers.hpp>

namespace futurehead
{
class json_payment_observer;

class payment_observer_processor final
{
public:
	explicit payment_observer_processor (futurehead::node_observers::blocks_t & blocks);
	void observer_action (futurehead::account const & account_a);
	void add (futurehead::account const & account_a, std::shared_ptr<futurehead::json_payment_observer> payment_observer_a);
	void erase (futurehead::account & account_a);

private:
	std::mutex mutex;
	std::unordered_map<futurehead::account, std::shared_ptr<futurehead::json_payment_observer>> payment_observers;
};
}
