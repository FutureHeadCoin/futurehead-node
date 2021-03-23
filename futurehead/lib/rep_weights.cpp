#include <futurehead/lib/rep_weights.hpp>
#include <futurehead/secure/blockstore.hpp>

void futurehead::rep_weights::representation_add (futurehead::account const & source_rep, futurehead::uint128_t const & amount_a)
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	auto source_previous (get (source_rep));
	put (source_rep, source_previous + amount_a);
}

void futurehead::rep_weights::representation_put (futurehead::account const & account_a, futurehead::uint128_union const & representation_a)
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	put (account_a, representation_a);
}

futurehead::uint128_t futurehead::rep_weights::representation_get (futurehead::account const & account_a)
{
	futurehead::lock_guard<std::mutex> lk (mutex);
	return get (account_a);
}

/** Makes a copy */
std::unordered_map<futurehead::account, futurehead::uint128_t> futurehead::rep_weights::get_rep_amounts ()
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	return rep_amounts;
}

void futurehead::rep_weights::put (futurehead::account const & account_a, futurehead::uint128_union const & representation_a)
{
	auto it = rep_amounts.find (account_a);
	auto amount = representation_a.number ();
	if (it != rep_amounts.end ())
	{
		it->second = amount;
	}
	else
	{
		rep_amounts.emplace (account_a, amount);
	}
}

futurehead::uint128_t futurehead::rep_weights::get (futurehead::account const & account_a)
{
	auto it = rep_amounts.find (account_a);
	if (it != rep_amounts.end ())
	{
		return it->second;
	}
	else
	{
		return futurehead::uint128_t{ 0 };
	}
}

std::unique_ptr<futurehead::container_info_component> futurehead::collect_container_info (futurehead::rep_weights & rep_weights, const std::string & name)
{
	size_t rep_amounts_count;

	{
		futurehead::lock_guard<std::mutex> guard (rep_weights.mutex);
		rep_amounts_count = rep_weights.rep_amounts.size ();
	}
	auto sizeof_element = sizeof (decltype (rep_weights.rep_amounts)::value_type);
	auto composite = std::make_unique<futurehead::container_info_composite> (name);
	composite->add_component (std::make_unique<futurehead::container_info_leaf> (container_info{ "rep_amounts", rep_amounts_count, sizeof_element }));
	return composite;
}
