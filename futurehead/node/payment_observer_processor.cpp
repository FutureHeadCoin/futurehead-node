#include <futurehead/node/json_payment_observer.hpp>
#include <futurehead/node/payment_observer_processor.hpp>

futurehead::payment_observer_processor::payment_observer_processor (futurehead::node_observers::blocks_t & blocks)
{
	blocks.add ([this](futurehead::election_status const &, futurehead::account const & account_a, futurehead::uint128_t const &, bool) {
		observer_action (account_a);
	});
}

void futurehead::payment_observer_processor::observer_action (futurehead::account const & account_a)
{
	std::shared_ptr<futurehead::json_payment_observer> observer;
	{
		futurehead::lock_guard<std::mutex> lock (mutex);
		auto existing (payment_observers.find (account_a));
		if (existing != payment_observers.end ())
		{
			observer = existing->second;
		}
	}
	if (observer != nullptr)
	{
		observer->observe ();
	}
}

void futurehead::payment_observer_processor::add (futurehead::account const & account_a, std::shared_ptr<futurehead::json_payment_observer> payment_observer_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	debug_assert (payment_observers.find (account_a) == payment_observers.end ());
	payment_observers[account_a] = payment_observer_a;
}

void futurehead::payment_observer_processor::erase (futurehead::account & account_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	debug_assert (payment_observers.find (account_a) != payment_observers.end ());
	payment_observers.erase (account_a);
}
