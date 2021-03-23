#include <futurehead/core_test/testutil.hpp>
#include <futurehead/lib/jsonconfig.hpp>
#include <futurehead/node/confirmation_solicitor.hpp>
#include <futurehead/node/testing.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (confirmation_solicitor, batches)
{
	futurehead::system system;
	futurehead::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_rep_crawler = true;
	node_flags.disable_udp = false;
	auto & node1 = *system.add_node (node_flags);
	node_flags.disable_request_loop = true;
	auto & node2 = *system.add_node (node_flags);
	auto channel1 (node2.network.udp_channels.create (node1.network.endpoint ()));
	// Solicitor will only solicit from this representative
	futurehead::representative representative (futurehead::test_genesis_key.pub, futurehead::genesis_amount, channel1);
	std::vector<futurehead::representative> representatives{ representative };
	futurehead::confirmation_solicitor solicitor (node2.network, node2.network_params.network);
	solicitor.prepare (representatives);
	// Ensure the representatives are correct
	ASSERT_EQ (1, representatives.size ());
	ASSERT_EQ (channel1, representatives.front ().channel);
	ASSERT_EQ (futurehead::test_genesis_key.pub, representatives.front ().account);
	ASSERT_TIMELY (3s, node2.network.size () == 1);
	auto send (std::make_shared<futurehead::send_block> (futurehead::genesis_hash, futurehead::keypair ().pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (futurehead::genesis_hash)));
	send->sideband_set ({});
	{
		futurehead::lock_guard<std::mutex> guard (node2.active.mutex);
		for (size_t i (0); i < futurehead::network::confirm_req_hashes_max; ++i)
		{
			auto election (std::make_shared<futurehead::election> (node2, send, nullptr, false));
			ASSERT_FALSE (solicitor.add (*election));
		}
		ASSERT_EQ (1, solicitor.max_confirm_req_batches);
		// Reached the maximum amount of requests for the channel
		auto election (std::make_shared<futurehead::election> (node2, send, nullptr, false));
		ASSERT_TRUE (solicitor.add (*election));
		// Broadcasting should be immediate
		ASSERT_EQ (0, node2.stats.count (futurehead::stat::type::message, futurehead::stat::detail::publish, futurehead::stat::dir::out));
		ASSERT_FALSE (solicitor.broadcast (*election));
	}
	// One publish through directed broadcasting and another through random flooding
	ASSERT_EQ (2, node2.stats.count (futurehead::stat::type::message, futurehead::stat::detail::publish, futurehead::stat::dir::out));
	solicitor.flush ();
	ASSERT_EQ (1, node2.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_req, futurehead::stat::dir::out));
}

TEST (confirmation_solicitor, different_hash)
{
	futurehead::system system;
	futurehead::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_rep_crawler = true;
	node_flags.disable_udp = false;
	auto & node1 = *system.add_node (node_flags);
	auto & node2 = *system.add_node (node_flags);
	auto channel1 (node2.network.udp_channels.create (node1.network.endpoint ()));
	// Solicitor will only solicit from this representative
	futurehead::representative representative (futurehead::test_genesis_key.pub, futurehead::genesis_amount, channel1);
	std::vector<futurehead::representative> representatives{ representative };
	futurehead::confirmation_solicitor solicitor (node2.network, node2.network_params.network);
	solicitor.prepare (representatives);
	// Ensure the representatives are correct
	ASSERT_EQ (1, representatives.size ());
	ASSERT_EQ (channel1, representatives.front ().channel);
	ASSERT_EQ (futurehead::test_genesis_key.pub, representatives.front ().account);
	ASSERT_TIMELY (3s, node2.network.size () == 1);
	auto send (std::make_shared<futurehead::send_block> (futurehead::genesis_hash, futurehead::keypair ().pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (futurehead::genesis_hash)));
	send->sideband_set ({});
	{
		futurehead::lock_guard<std::mutex> guard (node2.active.mutex);
		auto election (std::make_shared<futurehead::election> (node2, send, nullptr, false));
		// Add a vote for something else, not the winner
		election->last_votes[representative.account] = { std::chrono::steady_clock::now (), 1, 1 };
		// Ensure the request and broadcast goes through
		ASSERT_FALSE (solicitor.add (*election));
		ASSERT_FALSE (solicitor.broadcast (*election));
	}
	// One publish through directed broadcasting and another through random flooding
	ASSERT_EQ (2, node2.stats.count (futurehead::stat::type::message, futurehead::stat::detail::publish, futurehead::stat::dir::out));
	solicitor.flush ();
	ASSERT_EQ (1, node2.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_req, futurehead::stat::dir::out));
}
