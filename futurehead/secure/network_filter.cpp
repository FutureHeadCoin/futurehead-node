#include <futurehead/crypto_lib/random_pool.hpp>
#include <futurehead/lib/locks.hpp>
#include <futurehead/secure/buffer.hpp>
#include <futurehead/secure/common.hpp>
#include <futurehead/secure/network_filter.hpp>

futurehead::network_filter::network_filter (size_t size_a) :
items (size_a, futurehead::uint128_t{ 0 })
{
	futurehead::random_pool::generate_block (key, key.size ());
}

bool futurehead::network_filter::apply (uint8_t const * bytes_a, size_t count_a, futurehead::uint128_t * digest_a)
{
	// Get hash before locking
	auto digest (hash (bytes_a, count_a));

	futurehead::lock_guard<std::mutex> lock (mutex);
	auto & element (get_element (digest));
	bool existed (element == digest);
	if (!existed)
	{
		// Replace likely old element with a new one
		element = digest;
	}
	if (digest_a)
	{
		*digest_a = digest;
	}
	return existed;
}

void futurehead::network_filter::clear (futurehead::uint128_t const & digest_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	auto & element (get_element (digest_a));
	if (element == digest_a)
	{
		element = futurehead::uint128_t{ 0 };
	}
}

void futurehead::network_filter::clear (std::vector<futurehead::uint128_t> const & digests_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	for (auto const & digest : digests_a)
	{
		auto & element (get_element (digest));
		if (element == digest)
		{
			element = futurehead::uint128_t{ 0 };
		}
	}
}

void futurehead::network_filter::clear (uint8_t const * bytes_a, size_t count_a)
{
	clear (hash (bytes_a, count_a));
}

template <typename OBJECT>
void futurehead::network_filter::clear (OBJECT const & object_a)
{
	clear (hash (object_a));
}

void futurehead::network_filter::clear ()
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	items.assign (items.size (), futurehead::uint128_t{ 0 });
}

template <typename OBJECT>
futurehead::uint128_t futurehead::network_filter::hash (OBJECT const & object_a) const
{
	std::vector<uint8_t> bytes;
	{
		futurehead::vectorstream stream (bytes);
		object_a->serialize (stream);
	}
	return hash (bytes.data (), bytes.size ());
}

futurehead::uint128_t & futurehead::network_filter::get_element (futurehead::uint128_t const & hash_a)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (items.size () > 0);
	size_t index (hash_a % items.size ());
	return items[index];
}

futurehead::uint128_t futurehead::network_filter::hash (uint8_t const * bytes_a, size_t count_a) const
{
	futurehead::uint128_union digest{ 0 };
	siphash_t siphash (key, static_cast<unsigned int> (key.size ()));
	siphash.CalculateDigest (digest.bytes.data (), bytes_a, count_a);
	return digest.number ();
}

// Explicitly instantiate
template futurehead::uint128_t futurehead::network_filter::hash (std::shared_ptr<futurehead::block> const &) const;
template void futurehead::network_filter::clear (std::shared_ptr<futurehead::block> const &);
