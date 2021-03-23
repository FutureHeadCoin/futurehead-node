#include <futurehead/core_test/testutil.hpp>
#include <futurehead/lib/jsonconfig.hpp>
#include <futurehead/node/request_aggregator.hpp>
#include <futurehead/node/testing.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (request_aggregator, one)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	futurehead::genesis genesis;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	std::vector<std::pair<futurehead::block_hash, futurehead::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	// Not yet in the ledger
	ASSERT_TIMELY (3s, 1 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_unknown));
	ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// In the ledger but no vote generated yet
	ASSERT_TIMELY (3s, 0 < node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes));
	ASSERT_TRUE (node.aggregator.empty ());
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// Already cached
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (3, node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 1 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 1 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cached_votes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 2 == node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out));
}

TEST (request_aggregator, one_update)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	futurehead::genesis genesis;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	std::vector<std::pair<futurehead::block_hash, futurehead::root>> request;
	request.emplace_back (genesis.hash (), genesis.open->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	node.aggregator.add (channel, request);
	request.clear ();
	request.emplace_back (send1->hash (), send1->root ());
	// Update the pool of requests with another hash
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// In the ledger but no vote generated yet
	ASSERT_TIMELY (3s, 0 < node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes))
	ASSERT_TRUE (node.aggregator.empty ());
	ASSERT_EQ (2, node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 0 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 2 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cached_votes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 1 == node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out));
}

TEST (request_aggregator, two)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	futurehead::genesis genesis;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	std::vector<std::pair<futurehead::block_hash, futurehead::root>> request;
	request.emplace_back (genesis.hash (), genesis.open->root ());
	request.emplace_back (send1->hash (), send1->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	// Process both blocks
	ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// One vote should be generated for both blocks
	ASSERT_TIMELY (3s, 0 < node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes));
	ASSERT_TRUE (node.aggregator.empty ());
	// The same request should now send the cached vote
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (2, node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 0 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 2 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, 2 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cached_votes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 2 == node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out));
	// Make sure the cached vote is for both hashes
	auto vote2 (node.votes_cache.find (genesis.hash ()));
	auto vote1 (node.votes_cache.find (send1->hash ()));
	ASSERT_EQ (1, vote1.size ());
	ASSERT_EQ (1, vote2.size ());
	ASSERT_EQ (vote1.front (), vote2.front ());
}

TEST (request_aggregator, two_endpoints)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	futurehead::node_flags node_flags;
	node_flags.disable_rep_crawler = true;
	auto & node1 (*system.add_node (node_config, node_flags));
	node_config.peering_port = futurehead::get_available_port ();
	auto & node2 (*system.add_node (node_config, node_flags));
	futurehead::genesis genesis;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node1.work_generate_blocking (genesis.hash ())));
	std::vector<std::pair<futurehead::block_hash, futurehead::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (node1.store.tx_begin_write (), *send1).code);
	auto channel1 (node1.network.udp_channels.create (node1.network.endpoint ()));
	auto channel2 (node2.network.udp_channels.create (node2.network.endpoint ()));
	ASSERT_NE (futurehead::transport::map_endpoint_to_v6 (channel1->get_endpoint ()), futurehead::transport::map_endpoint_to_v6 (channel2->get_endpoint ()));
	// Use the aggregator from node1 only, making requests from both nodes
	node1.aggregator.add (channel1, request);
	node1.aggregator.add (channel2, request);
	ASSERT_EQ (2, node1.aggregator.size ());
	// For the first request it generates the vote, for the second it uses the generated vote
	ASSERT_TIMELY (3s, node1.aggregator.empty ());
	ASSERT_EQ (2, node1.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node1.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 0 == node1.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 1 == node1.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 1 == node1.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, 1 == node1.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY (3s, 1 == node1.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cached_votes));
	ASSERT_TIMELY (3s, 0 == node1.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cannot_vote));
}

TEST (request_aggregator, split)
{
	constexpr size_t max_vbh = futurehead::network::confirm_ack_hashes_max;
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	futurehead::genesis genesis;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	std::vector<std::pair<futurehead::block_hash, futurehead::root>> request;
	std::vector<std::shared_ptr<futurehead::block>> blocks;
	auto previous = genesis.hash ();
	// Add max_vbh + 1 blocks and request votes for them
	for (size_t i (0); i <= max_vbh; ++i)
	{
		futurehead::block_builder builder;
		blocks.push_back (builder
		                  .state ()
		                  .account (futurehead::test_genesis_key.pub)
		                  .previous (previous)
		                  .representative (futurehead::test_genesis_key.pub)
		                  .balance (futurehead::genesis_amount - (i + 1))
		                  .link (futurehead::test_genesis_key.pub)
		                  .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
		                  .work (*system.work.generate (previous))
		                  .build ());
		auto const & block = blocks.back ();
		previous = block->hash ();
		ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *block).code);
		request.emplace_back (block->hash (), block->root ());
	}
	// Confirm all blocks
	node.block_confirm (blocks.back ());
	{
		auto election (node.active.election (blocks.back ()->qualified_root ()));
		ASSERT_NE (nullptr, election);
		futurehead::lock_guard<std::mutex> guard (node.active.mutex);
		election->confirm_once ();
	}
	ASSERT_TIMELY (5s, max_vbh + 2 == node.ledger.cache.cemented_count);
	ASSERT_EQ (max_vbh + 1, request.size ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// In the ledger but no vote generated yet
	ASSERT_TIMELY (3s, 2 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes));
	ASSERT_TRUE (node.aggregator.empty ());
	// Two votes were sent, the first one for 12 hashes and the second one for 1 hash
	ASSERT_EQ (1, node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 13 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 2 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 0 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 2 == node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out));
}

TEST (request_aggregator, channel_lifetime)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	futurehead::genesis genesis;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	std::vector<std::pair<futurehead::block_hash, futurehead::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	{
		// The aggregator should extend the lifetime of the channel
		auto channel (node.network.udp_channels.create (node.network.endpoint ()));
		node.aggregator.add (channel, request);
	}
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, 0 < node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes));
}

TEST (request_aggregator, channel_update)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	futurehead::genesis genesis;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	std::vector<std::pair<futurehead::block_hash, futurehead::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	std::weak_ptr<futurehead::transport::channel> channel1_w;
	{
		auto channel1 (node.network.udp_channels.create (node.network.endpoint ()));
		channel1_w = channel1;
		node.aggregator.add (channel1, request);
		auto channel2 (node.network.udp_channels.create (node.network.endpoint ()));
		// The aggregator then hold channel2 and drop channel1
		node.aggregator.add (channel2, request);
	}
	// Both requests were for the same endpoint, so only one pool should exist
	ASSERT_EQ (1, node.aggregator.size ());
	// channel1 is not being held anymore
	ASSERT_EQ (nullptr, channel1_w.lock ());
	ASSERT_TIMELY (3s, 0 < node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes) == 0);
}

TEST (request_aggregator, channel_max_queue)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	node_config.max_queued_requests = 1;
	auto & node (*system.add_node (node_config));
	futurehead::genesis genesis;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	std::vector<std::pair<futurehead::block_hash, futurehead::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.aggregator.add (channel, request);
	node.aggregator.add (channel, request);
	ASSERT_TIMELY (3s, 1 == node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_dropped));
}

TEST (request_aggregator, unique)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	futurehead::genesis genesis;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	std::vector<std::pair<futurehead::block_hash, futurehead::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.aggregator.add (channel, request);
	node.aggregator.add (channel, request);
	node.aggregator.add (channel, request);
	node.aggregator.add (channel, request);
	ASSERT_TIMELY (3s, 1 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes));
}

TEST (request_aggregator, cannot_vote)
{
	futurehead::system system;
	futurehead::node_flags flags;
	flags.disable_request_loop = true;
	auto & node (*system.add_node (flags));
	futurehead::genesis genesis;
	futurehead::state_block_builder builder;
	std::shared_ptr<futurehead::state_block> send1 = builder.make_block ()
	                                           .account (futurehead::test_genesis_key.pub)
	                                           .previous (futurehead::genesis_hash)
	                                           .representative (futurehead::test_genesis_key.pub)
	                                           .balance (futurehead::genesis_amount - 1)
	                                           .link (futurehead::test_genesis_key.pub)
	                                           .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                           .work (*system.work.generate (futurehead::genesis_hash))
	                                           .build ();
	std::shared_ptr<futurehead::state_block> send2 = builder.make_block ()
	                                           .from (*send1)
	                                           .previous (send1->hash ())
	                                           .balance (send1->balance ().number () - 1)
	                                           .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                           .work (*system.work.generate (send1->hash ()))
	                                           .build ();
	ASSERT_EQ (futurehead::process_result::progress, node.process (*send1).code);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*send2).code);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	ASSERT_FALSE (node.ledger.can_vote (node.store.tx_begin_read (), *send2));

	std::vector<std::pair<futurehead::block_hash, futurehead::root>> request;
	// Correct hash, correct root
	request.emplace_back (send2->hash (), send2->root ());
	// Incorrect hash, correct root
	request.emplace_back (1, send2->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (1, node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 2 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cannot_vote));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cached_votes));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_unknown));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out));

	// With an ongoing election
	node.block_confirm (send2);
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (2, node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 4 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cannot_vote));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cached_votes));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_unknown));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out));

	// Confirm send1
	node.block_confirm (send1);
	{
		auto election (node.active.election (send1->qualified_root ()));
		ASSERT_NE (nullptr, election);
		futurehead::lock_guard<std::mutex> guard (node.active.mutex);
		election->confirm_once ();
	}
	ASSERT_TIMELY (3s, node.ledger.can_vote (node.store.tx_begin_read (), *send2));
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (3, node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::aggregator, futurehead::stat::detail::aggregator_dropped));
	ASSERT_EQ (4, node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 2 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (futurehead::stat::type::requests, futurehead::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 1 == node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out));
}
