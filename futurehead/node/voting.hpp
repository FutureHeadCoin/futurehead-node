#pragma once

#include <futurehead/lib/locks.hpp>
#include <futurehead/lib/numbers.hpp>
#include <futurehead/lib/utility.hpp>
#include <futurehead/node/wallet.hpp>
#include <futurehead/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace futurehead
{
class ledger;
class network;
class node_config;
class vote_processor;
class votes_cache;
class wallets;

class vote_generator final
{
public:
	vote_generator (futurehead::node_config const & config_a, futurehead::ledger &, futurehead::wallets & wallets_a, futurehead::vote_processor & vote_processor_a, futurehead::votes_cache & votes_cache_a, futurehead::network & network_a);
	void add (futurehead::block_hash const &);
	void stop ();

private:
	void run ();
	void send (futurehead::unique_lock<std::mutex> &);
	futurehead::node_config const & config;
	futurehead::ledger & ledger;
	futurehead::wallets & wallets;
	futurehead::vote_processor & vote_processor;
	futurehead::votes_cache & votes_cache;
	futurehead::network & network;
	std::mutex mutex;
	futurehead::condition_variable condition;
	std::deque<futurehead::block_hash> hashes;
	futurehead::network_params network_params;
	bool stopped{ false };
	bool started{ false };
	std::thread thread;

	friend std::unique_ptr<container_info_component> collect_container_info (vote_generator & vote_generator, const std::string & name);
};

class vote_generator_session final
{
public:
	vote_generator_session (vote_generator & vote_generator_a);
	void add (futurehead::block_hash const &);
	void flush ();

private:
	futurehead::vote_generator & generator;
	std::vector<futurehead::block_hash> hashes;
};

std::unique_ptr<container_info_component> collect_container_info (vote_generator & vote_generator, const std::string & name);
class cached_votes final
{
public:
	futurehead::block_hash hash;
	std::vector<std::shared_ptr<futurehead::vote>> votes;
};
class votes_cache final
{
public:
	votes_cache (futurehead::wallets & wallets_a);
	void add (std::shared_ptr<futurehead::vote> const &);
	std::vector<std::shared_ptr<futurehead::vote>> find (futurehead::block_hash const &);
	void remove (futurehead::block_hash const &);

private:
	std::mutex cache_mutex;
	// clang-format off
	class tag_sequence {};
	class tag_hash {};
	boost::multi_index_container<futurehead::cached_votes,
	boost::multi_index::indexed_by<
		boost::multi_index::sequenced<boost::multi_index::tag<tag_sequence>>,
		boost::multi_index::hashed_unique<boost::multi_index::tag<tag_hash>,
			boost::multi_index::member<futurehead::cached_votes, futurehead::block_hash, &futurehead::cached_votes::hash>>>>
	cache;
	// clang-format on
	futurehead::network_params network_params;
	futurehead::wallets & wallets;
	friend std::unique_ptr<container_info_component> collect_container_info (votes_cache & votes_cache, const std::string & name);
};

std::unique_ptr<container_info_component> collect_container_info (votes_cache & votes_cache, const std::string & name);
}
