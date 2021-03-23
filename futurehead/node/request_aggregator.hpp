#pragma once

#include <futurehead/lib/locks.hpp>
#include <futurehead/lib/numbers.hpp>
#include <futurehead/node/transport/transport.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <thread>
#include <unordered_map>

namespace mi = boost::multi_index;

namespace futurehead
{
class active_transactions;
class ledger;
class node_config;
class stat;
class votes_cache;
class wallets;
/**
 * Pools together confirmation requests, separately for each endpoint.
 * Requests are added from network messages, and aggregated to minimize bandwidth and vote generation. Example:
 * * Two votes are cached, one for hashes {1,2,3} and another for hashes {4,5,6}
 * * A request arrives for hashes {1,4,5}. Another request arrives soon afterwards for hashes {2,3,6}
 * * The aggregator will reply with the two cached votes
 * Votes are generated for uncached hashes.
 */
class request_aggregator final
{
	/**
	 * Holds a buffer of incoming requests from an endpoint.
	 * Extends the lifetime of the corresponding channel. The channel is updated on a new request arriving from the same endpoint, such that only the newest channel is held
	 */
	struct channel_pool final
	{
		channel_pool () = delete;
		explicit channel_pool (std::shared_ptr<futurehead::transport::channel> & channel_a) :
		channel (channel_a),
		endpoint (futurehead::transport::map_endpoint_to_v6 (channel_a->get_endpoint ()))
		{
		}
		std::vector<std::pair<futurehead::block_hash, futurehead::root>> hashes_roots;
		std::shared_ptr<futurehead::transport::channel> channel;
		futurehead::endpoint endpoint;
		std::chrono::steady_clock::time_point const start{ std::chrono::steady_clock::now () };
		std::chrono::steady_clock::time_point deadline;
	};

	// clang-format off
	class tag_endpoint {};
	class tag_deadline {};
	// clang-format on

public:
	request_aggregator () = delete;
	request_aggregator (futurehead::network_constants const &, futurehead::node_config const & config, futurehead::stat & stats_a, futurehead::votes_cache &, futurehead::ledger &, futurehead::wallets &, futurehead::active_transactions &);

	/** Add a new request by \p channel_a for hashes \p hashes_roots_a */
	void add (std::shared_ptr<futurehead::transport::channel> & channel_a, std::vector<std::pair<futurehead::block_hash, futurehead::root>> const & hashes_roots_a);
	void stop ();
	/** Returns the number of currently queued request pools */
	size_t size ();
	bool empty ();

	const std::chrono::milliseconds max_delay;
	const std::chrono::milliseconds small_delay;
	const size_t max_channel_requests;

private:
	void run ();
	/** Remove duplicate requests **/
	void erase_duplicates (std::vector<std::pair<futurehead::block_hash, futurehead::root>> &) const;
	/** Aggregate \p requests_a and send cached votes to \p channel_a . Return the remaining hashes that need vote generation **/
	std::vector<futurehead::block_hash> aggregate (futurehead::transaction const &, std::vector<std::pair<futurehead::block_hash, futurehead::root>> const & requests_a, std::shared_ptr<futurehead::transport::channel> & channel_a) const;
	/** Generate votes from \p hashes_a and send to \p channel_a **/
	void generate (futurehead::transaction const &, std::vector<futurehead::block_hash> const & hashes_a, std::shared_ptr<futurehead::transport::channel> & channel_a) const;

	futurehead::stat & stats;
	futurehead::votes_cache & votes_cache;
	futurehead::ledger & ledger;
	futurehead::wallets & wallets;
	futurehead::active_transactions & active;

	// clang-format off
	boost::multi_index_container<channel_pool,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_endpoint>,
			mi::member<channel_pool, futurehead::endpoint, &channel_pool::endpoint>>,
		mi::ordered_non_unique<mi::tag<tag_deadline>,
			mi::member<channel_pool, std::chrono::steady_clock::time_point, &channel_pool::deadline>>>>
	requests;
	// clang-format on

	bool stopped{ false };
	bool started{ false };
	futurehead::condition_variable condition;
	std::mutex mutex;
	std::thread thread;

	friend std::unique_ptr<container_info_component> collect_container_info (request_aggregator &, const std::string &);
};
std::unique_ptr<container_info_component> collect_container_info (request_aggregator &, const std::string &);
}
