#include <futurehead/node/confirmation_solicitor.hpp>
#include <futurehead/node/election.hpp>

using namespace std::chrono_literals;

futurehead::confirmation_solicitor::confirmation_solicitor (futurehead::network & network_a, futurehead::network_constants const & params_a) :
max_confirm_req_batches (params_a.is_test_network () ? 1 : 20),
max_block_broadcasts (params_a.is_test_network () ? 4 : 30),
max_election_requests (30),
max_election_broadcasts (std::max<size_t> (network_a.fanout () / 2, 1)),
network (network_a)
{
}

void futurehead::confirmation_solicitor::prepare (std::vector<futurehead::representative> const & representatives_a)
{
	debug_assert (!prepared);
	requests.clear ();
	rebroadcasted = 0;
	/** Two copies are required as representatives can be erased from \p representatives_requests */
	representatives_requests = representatives_a;
	representatives_broadcasts = representatives_a;
	prepared = true;
}

bool futurehead::confirmation_solicitor::broadcast (futurehead::election const & election_a)
{
	debug_assert (prepared);
	bool error (true);
	if (rebroadcasted++ < max_block_broadcasts)
	{
		auto const & hash (election_a.status.winner->hash ());
		futurehead::publish winner (election_a.status.winner);
		unsigned count = 0;
		// Directed broadcasting to principal representatives
		for (auto i (representatives_broadcasts.begin ()), n (representatives_broadcasts.end ()); i != n && count < max_election_broadcasts; ++i)
		{
			auto existing (election_a.last_votes.find (i->account));
			if (existing == election_a.last_votes.end () || existing->second.hash != hash)
			{
				i->channel->send (winner);
				++count;
			}
		}
		// Random flood for block propagation
		network.flood_message (winner, futurehead::buffer_drop_policy::limiter, 0.5f);
		error = false;
	}
	return error;
}

bool futurehead::confirmation_solicitor::add (futurehead::election const & election_a)
{
	debug_assert (prepared);
	auto const max_channel_requests (max_confirm_req_batches * futurehead::network::confirm_req_hashes_max);
	unsigned count = 0;
	auto const & hash (election_a.status.winner->hash ());
	for (auto i (representatives_requests.begin ()); i != representatives_requests.end () && count < max_election_requests;)
	{
		bool full_queue (false);
		auto rep (*i);
		auto existing (election_a.last_votes.find (rep.account));
		if (existing == election_a.last_votes.end () || existing->second.hash != hash)
		{
			auto & request_queue (requests[rep.channel]);
			if (request_queue.size () < max_channel_requests)
			{
				request_queue.emplace_back (election_a.status.winner->hash (), election_a.status.winner->root ());
				++count;
			}
			else
			{
				full_queue = true;
			}
		}
		i = !full_queue ? i + 1 : representatives_requests.erase (i);
	}
	return count == 0;
}

void futurehead::confirmation_solicitor::flush ()
{
	debug_assert (prepared);
	for (auto const & request_queue : requests)
	{
		auto const & channel (request_queue.first);
		std::vector<std::pair<futurehead::block_hash, futurehead::root>> roots_hashes_l;
		for (auto const & root_hash : request_queue.second)
		{
			roots_hashes_l.push_back (root_hash);
			if (roots_hashes_l.size () == futurehead::network::confirm_req_hashes_max)
			{
				futurehead::confirm_req req (roots_hashes_l);
				channel->send (req);
				roots_hashes_l.clear ();
			}
		}
		if (!roots_hashes_l.empty ())
		{
			futurehead::confirm_req req (roots_hashes_l);
			channel->send (req);
		}
	}
	prepared = false;
}
