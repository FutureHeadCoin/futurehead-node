#include <futurehead/node/peer_exclusion.hpp>

constexpr std::chrono::hours futurehead::peer_exclusion::exclude_time_hours;
constexpr std::chrono::hours futurehead::peer_exclusion::exclude_remove_hours;
constexpr size_t futurehead::peer_exclusion::size_max;
constexpr double futurehead::peer_exclusion::peers_percentage_limit;

uint64_t futurehead::peer_exclusion::add (futurehead::tcp_endpoint const & endpoint_a, size_t const network_peers_count_a)
{
	uint64_t result (0);
	futurehead::lock_guard<std::mutex> guard (mutex);
	// Clean old excluded peers
	auto limited = limited_size (network_peers_count_a);
	while (peers.size () > 1 && peers.size () > limited)
	{
		peers.get<tag_exclusion> ().erase (peers.get<tag_exclusion> ().begin ());
	}
	debug_assert (peers.size () <= size_max);
	auto & peers_by_endpoint (peers.get<tag_endpoint> ());
	auto address = endpoint_a.address ();
	auto existing (peers_by_endpoint.find (address));
	if (existing == peers_by_endpoint.end ())
	{
		// Insert new endpoint
		auto inserted (peers.emplace (peer_exclusion::item{ std::chrono::steady_clock::steady_clock::now () + exclude_time_hours, address, 1 }));
		(void)inserted;
		debug_assert (inserted.second);
		result = 1;
	}
	else
	{
		// Update existing endpoint
		peers_by_endpoint.modify (existing, [&result](peer_exclusion::item & item_a) {
			++item_a.score;
			result = item_a.score;
			if (item_a.score == peer_exclusion::score_limit)
			{
				item_a.exclude_until = std::chrono::steady_clock::now () + peer_exclusion::exclude_time_hours;
			}
			else if (item_a.score > peer_exclusion::score_limit)
			{
				item_a.exclude_until = std::chrono::steady_clock::now () + peer_exclusion::exclude_time_hours * item_a.score * 2;
			}
		});
	}
	return result;
}

bool futurehead::peer_exclusion::check (futurehead::tcp_endpoint const & endpoint_a)
{
	bool excluded (false);
	futurehead::lock_guard<std::mutex> guard (mutex);
	auto & peers_by_endpoint (peers.get<tag_endpoint> ());
	auto existing (peers_by_endpoint.find (endpoint_a.address ()));
	if (existing != peers_by_endpoint.end () && existing->score >= score_limit)
	{
		if (existing->exclude_until > std::chrono::steady_clock::now ())
		{
			excluded = true;
		}
		else if (existing->exclude_until + exclude_remove_hours * existing->score < std::chrono::steady_clock::now ())
		{
			peers_by_endpoint.erase (existing);
		}
	}
	return excluded;
}

void futurehead::peer_exclusion::remove (futurehead::tcp_endpoint const & endpoint_a)
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	peers.get<tag_endpoint> ().erase (endpoint_a.address ());
}

size_t futurehead::peer_exclusion::limited_size (size_t const network_peers_count_a) const
{
	return std::min<size_t> (size_max, network_peers_count_a * peers_percentage_limit);
}

size_t futurehead::peer_exclusion::size () const
{
	futurehead::lock_guard<std::mutex> guard (mutex);
	return peers.size ();
}

std::unique_ptr<futurehead::container_info_component> futurehead::collect_container_info (futurehead::peer_exclusion const & excluded_peers, const std::string & name)
{
	auto composite = std::make_unique<container_info_composite> (name);

	size_t excluded_peers_count = excluded_peers.size ();
	auto sizeof_excluded_peers_element = sizeof (futurehead::peer_exclusion::ordered_endpoints::value_type);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "peers", excluded_peers_count, sizeof_excluded_peers_element }));

	return composite;
}
