#include <futurehead/lib/epoch.hpp>
#include <futurehead/lib/utility.hpp>

futurehead::link const & futurehead::epochs::link (futurehead::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).link;
}

bool futurehead::epochs::is_epoch_link (futurehead::link const & link_a) const
{
	return std::any_of (epochs_m.begin (), epochs_m.end (), [&link_a](auto const & item_a) { return item_a.second.link == link_a; });
}

futurehead::public_key const & futurehead::epochs::signer (futurehead::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).signer;
}

futurehead::epoch futurehead::epochs::epoch (futurehead::link const & link_a) const
{
	auto existing (std::find_if (epochs_m.begin (), epochs_m.end (), [&link_a](auto const & item_a) { return item_a.second.link == link_a; }));
	debug_assert (existing != epochs_m.end ());
	return existing->first;
}

void futurehead::epochs::add (futurehead::epoch epoch_a, futurehead::public_key const & signer_a, futurehead::link const & link_a)
{
	debug_assert (epochs_m.find (epoch_a) == epochs_m.end ());
	epochs_m[epoch_a] = { signer_a, link_a };
}

bool futurehead::epochs::is_sequential (futurehead::epoch epoch_a, futurehead::epoch new_epoch_a)
{
	auto head_epoch = std::underlying_type_t<futurehead::epoch> (epoch_a);
	bool is_valid_epoch (head_epoch >= std::underlying_type_t<futurehead::epoch> (futurehead::epoch::epoch_0));
	return is_valid_epoch && (std::underlying_type_t<futurehead::epoch> (new_epoch_a) == (head_epoch + 1));
}

std::underlying_type_t<futurehead::epoch> futurehead::normalized_epoch (futurehead::epoch epoch_a)
{
	// Currently assumes that the epoch versions in the enum are sequential.
	auto start = std::underlying_type_t<futurehead::epoch> (futurehead::epoch::epoch_0);
	auto end = std::underlying_type_t<futurehead::epoch> (epoch_a);
	debug_assert (end >= start);
	return end - start;
}
