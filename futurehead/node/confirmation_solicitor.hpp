#pragma once

#include <futurehead/node/network.hpp>
#include <futurehead/node/repcrawler.hpp>

#include <unordered_map>

namespace futurehead
{
class election;
class node;
/** This class accepts elections that need further votes before they can be confirmed and bundles them in to single confirm_req packets */
class confirmation_solicitor final
{
public:
	confirmation_solicitor (futurehead::network &, futurehead::network_constants const &);
	/** Prepare object for batching election confirmation requests*/
	void prepare (std::vector<futurehead::representative> const &);
	/** Broadcast the winner of an election if the broadcast limit has not been reached. Returns false if the broadcast was performed */
	bool broadcast (futurehead::election const &);
	/** Add an election that needs to be confirmed. Returns false if successfully added */
	bool add (futurehead::election const &);
	/** Dispatch bundled requests to each channel*/
	void flush ();
	/** Maximum amount of confirmation requests (batches) to be sent to each channel */
	size_t const max_confirm_req_batches;
	/** Global maximum amount of block broadcasts */
	size_t const max_block_broadcasts;
	/** Maximum amount of requests to be sent per election */
	size_t const max_election_requests;
	/** Maximum amount of directed broadcasts to be sent per election */
	size_t const max_election_broadcasts;

private:
	futurehead::network & network;

	unsigned rebroadcasted{ 0 };
	std::vector<futurehead::representative> representatives_requests;
	std::vector<futurehead::representative> representatives_broadcasts;
	using vector_root_hashes = std::vector<std::pair<futurehead::block_hash, futurehead::root>>;
	std::unordered_map<std::shared_ptr<futurehead::transport::channel>, vector_root_hashes> requests;
	bool prepared{ false };
};
}
