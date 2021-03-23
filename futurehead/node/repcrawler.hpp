#pragma once

#include <futurehead/node/common.hpp>
#include <futurehead/node/transport/transport.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/optional.hpp>

#include <chrono>
#include <memory>
#include <unordered_set>

namespace mi = boost::multi_index;

namespace futurehead
{
class node;

/**
 * A representative picked up during repcrawl.
 */
class representative
{
public:
	representative () = default;
	representative (futurehead::account account_a, futurehead::amount weight_a, std::shared_ptr<futurehead::transport::channel> channel_a) :
	account (account_a), weight (weight_a), channel (channel_a)
	{
		debug_assert (channel != nullptr);
	}
	std::reference_wrapper<futurehead::transport::channel const> channel_ref () const
	{
		return *channel;
	};
	bool operator== (futurehead::representative const & other_a) const
	{
		return account == other_a.account;
	}
	futurehead::account account{ 0 };
	futurehead::amount weight{ 0 };
	std::shared_ptr<futurehead::transport::channel> channel;
	std::chrono::steady_clock::time_point last_request{ std::chrono::steady_clock::time_point () };
	std::chrono::steady_clock::time_point last_response{ std::chrono::steady_clock::time_point () };
};

/**
 * Crawls the network for representatives. Queries are performed by requesting confirmation of a
 * random block and observing the corresponding vote.
 */
class rep_crawler final
{
	friend std::unique_ptr<container_info_component> collect_container_info (rep_crawler & rep_crawler, const std::string & name);

	// clang-format off
	class tag_account {};
	class tag_channel_ref {};
	class tag_last_request {};
	class tag_weight {};

	using probably_rep_t = boost::multi_index_container<representative,
	mi::indexed_by<
		mi::hashed_unique<mi::member<representative, futurehead::account, &representative::account>>,
		mi::random_access<>,
		mi::ordered_non_unique<mi::tag<tag_last_request>,
			mi::member<representative, std::chrono::steady_clock::time_point, &representative::last_request>>,
		mi::ordered_non_unique<mi::tag<tag_weight>,
			mi::member<representative, futurehead::amount, &representative::weight>, std::greater<futurehead::amount>>,
		mi::hashed_non_unique<mi::tag<tag_channel_ref>,
			mi::const_mem_fun<representative, std::reference_wrapper<futurehead::transport::channel const>, &representative::channel_ref>>>>;
	// clang-format on

public:
	rep_crawler (futurehead::node & node_a);

	/** Start crawling */
	void start ();

	/** Remove block hash from list of active rep queries */
	void remove (futurehead::block_hash const &);

	/** Attempt to determine if the peer manages one or more representative accounts */
	void query (std::vector<std::shared_ptr<futurehead::transport::channel>> const & channels_a);

	/** Attempt to determine if the peer manages one or more representative accounts */
	void query (std::shared_ptr<futurehead::transport::channel> const & channel_a);

	/** Query if a peer manages a principle representative */
	bool is_pr (futurehead::transport::channel const &) const;

	/**
	 * Called when a non-replay vote on a block previously sent by query() is received. This indiciates
	 * with high probability that the endpoint is a representative node.
	 * @return false if the vote corresponded to any active hash.
	 */
	bool response (std::shared_ptr<futurehead::transport::channel> &, std::shared_ptr<futurehead::vote> &);

	/** Get total available weight from representatives */
	futurehead::uint128_t total_weight () const;

	/** Request a list of the top \p count_a known representatives in descending order of weight, with at least \p weight_a voting weight, and optionally with a minimum version \p opt_version_min_a */
	std::vector<representative> representatives (size_t count_a = std::numeric_limits<size_t>::max (), futurehead::uint128_t const weight_a = 0, boost::optional<decltype (futurehead::protocol_constants::protocol_version)> const & opt_version_min_a = boost::none);

	/** Request a list of the top \p count_a known principal representatives in descending order of weight, optionally with a minimum version \p opt_version_min_a */
	std::vector<representative> principal_representatives (size_t count_a = std::numeric_limits<size_t>::max (), boost::optional<decltype (futurehead::protocol_constants::protocol_version)> const & opt_version_min_a = boost::none);

	/** Request a list of the top \p count_a known representative endpoints. */
	std::vector<std::shared_ptr<futurehead::transport::channel>> representative_endpoints (size_t count_a);

	/** Total number of representatives */
	size_t representative_count ();

private:
	futurehead::node & node;

	/** Protects the active-hash container */
	std::mutex active_mutex;

	/** We have solicted votes for these random blocks */
	std::unordered_set<futurehead::block_hash> active;

	// Validate responses to see if they're reps
	void validate ();

	/** Called continuously to crawl for representatives */
	void ongoing_crawl ();

	/** Returns a list of endpoints to crawl. The total weight is passed in to avoid computing it twice. */
	std::vector<std::shared_ptr<futurehead::transport::channel>> get_crawl_targets (futurehead::uint128_t total_weight_a);

	/** When a rep request is made, this is called to update the last-request timestamp. */
	void on_rep_request (std::shared_ptr<futurehead::transport::channel> channel_a);

	/** Clean representatives with inactive channels */
	void cleanup_reps ();

	/** Update representatives weights from ledger */
	void update_weights ();

	/** Protects the probable_reps container */
	mutable std::mutex probable_reps_mutex;

	/** Probable representatives */
	probably_rep_t probable_reps;

	friend class active_transactions_confirm_active_Test;
	friend class active_transactions_confirm_frontier_Test;

	std::deque<std::pair<std::shared_ptr<futurehead::transport::channel>, std::shared_ptr<futurehead::vote>>> responses;
};
}
