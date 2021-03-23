#pragma once

#include <futurehead/lib/numbers.hpp>

#include <type_traits>
#include <unordered_map>

namespace futurehead
{
/**
 * Tag for which epoch an entry belongs to
 */
enum class epoch : uint8_t
{
	invalid = 0,
	unspecified = 1,
	epoch_begin = 2,
	epoch_0 = 2,
	epoch_1 = 3,
	epoch_2 = 4,
	max = epoch_2
};

/* This turns epoch_0 into 0 for instance */
std::underlying_type_t<futurehead::epoch> normalized_epoch (futurehead::epoch epoch_a);
}
namespace std
{
template <>
struct hash<::futurehead::epoch>
{
	std::size_t operator() (::futurehead::epoch const & epoch_a) const
	{
		std::hash<std::underlying_type_t<::futurehead::epoch>> hash;
		return hash (static_cast<std::underlying_type_t<::futurehead::epoch>> (epoch_a));
	}
};
}
namespace futurehead
{
class epoch_info
{
public:
	futurehead::public_key signer;
	futurehead::link link;
};
class epochs
{
public:
	bool is_epoch_link (futurehead::link const & link_a) const;
	futurehead::link const & link (futurehead::epoch epoch_a) const;
	futurehead::public_key const & signer (futurehead::epoch epoch_a) const;
	futurehead::epoch epoch (futurehead::link const & link_a) const;
	void add (futurehead::epoch epoch_a, futurehead::public_key const & signer_a, futurehead::link const & link_a);
	/** Checks that new_epoch is 1 version higher than epoch */
	static bool is_sequential (futurehead::epoch epoch_a, futurehead::epoch new_epoch_a);

private:
	std::unordered_map<futurehead::epoch, futurehead::epoch_info> epochs_m;
};
}
