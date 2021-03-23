#include <futurehead/core_test/fakes/work_peer.hpp>
#include <futurehead/core_test/testutil.hpp>
#include <futurehead/node/testing.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (distributed_work, stopped)
{
	futurehead::system system (1);
	system.nodes[0]->distributed_work.stop ();
	ASSERT_TRUE (system.nodes[0]->distributed_work.make (futurehead::work_version::work_1, futurehead::block_hash (), {}, futurehead::network_constants ().publish_thresholds.base, {}));
}

TEST (distributed_work, no_peers)
{
	futurehead::system system (1);
	auto node (system.nodes[0]);
	futurehead::block_hash hash{ 1 };
	boost::optional<uint64_t> work;
	std::atomic<bool> done{ false };
	auto callback = [&work, &done](boost::optional<uint64_t> work_a) {
		ASSERT_TRUE (work_a.is_initialized ());
		work = work_a;
		done = true;
	};
	ASSERT_FALSE (node->distributed_work.make (futurehead::work_version::work_1, hash, node->config.work_peers, node->network_params.network.publish_thresholds.base, callback, futurehead::account ()));
	system.deadline_set (5s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_GE (futurehead::work_difficulty (futurehead::work_version::work_1, hash, *work), node->network_params.network.publish_thresholds.base);
	// should only be removed after cleanup
	ASSERT_EQ (1, node->distributed_work.items.size ());
	while (!node->distributed_work.items.empty ())
	{
		node->distributed_work.cleanup_finished ();
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (distributed_work, no_peers_disabled)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.work_threads = 0;
	auto & node = *system.add_node (node_config);
	ASSERT_TRUE (node.distributed_work.make (futurehead::work_version::work_1, futurehead::block_hash (), node.config.work_peers, futurehead::network_constants ().publish_thresholds.base, {}));
}

TEST (distributed_work, no_peers_cancel)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.max_work_generate_multiplier = 1e6;
	auto & node = *system.add_node (node_config);
	futurehead::block_hash hash{ 1 };
	bool done{ false };
	auto callback_to_cancel = [&done](boost::optional<uint64_t> work_a) {
		ASSERT_FALSE (work_a.is_initialized ());
		done = true;
	};
	ASSERT_FALSE (node.distributed_work.make (futurehead::work_version::work_1, hash, node.config.work_peers, futurehead::difficulty::from_multiplier (1e6, node.network_params.network.publish_thresholds.base), callback_to_cancel));
	ASSERT_EQ (1, node.distributed_work.items.size ());
	// cleanup should not cancel or remove an ongoing work
	node.distributed_work.cleanup_finished ();
	ASSERT_EQ (1, node.distributed_work.items.size ());

	// manually cancel
	node.distributed_work.cancel (hash, true); // forces local stop
	system.deadline_set (20s);
	while (!done || !node.distributed_work.items.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// now using observer
	done = false;
	ASSERT_FALSE (node.distributed_work.make (futurehead::work_version::work_1, hash, node.config.work_peers, futurehead::difficulty::from_multiplier (1e6, node.network_params.network.publish_thresholds.base), callback_to_cancel));
	ASSERT_EQ (1, node.distributed_work.items.size ());
	node.observers.work_cancel.notify (hash);
	system.deadline_set (20s);
	while (!done || !node.distributed_work.items.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (distributed_work, no_peers_multi)
{
	futurehead::system system (1);
	auto node (system.nodes[0]);
	futurehead::block_hash hash{ 1 };
	unsigned total{ 10 };
	std::atomic<unsigned> count{ 0 };
	auto callback = [&count](boost::optional<uint64_t> work_a) {
		ASSERT_TRUE (work_a.is_initialized ());
		++count;
	};
	// Test many works for the same root
	for (unsigned i{ 0 }; i < total; ++i)
	{
		ASSERT_FALSE (node->distributed_work.make (futurehead::work_version::work_1, hash, node->config.work_peers, futurehead::difficulty::from_multiplier (10, node->network_params.network.publish_thresholds.base), callback));
	}
	// 1 root, and _total_ requests for that root are expected, but some may have already finished
	ASSERT_EQ (1, node->distributed_work.items.size ());
	{
		auto requests (node->distributed_work.items.begin ());
		ASSERT_EQ (hash, requests->first);
		ASSERT_GE (requests->second.size (), total - 4);
	}
	system.deadline_set (5s);
	while (count < total)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.deadline_set (5s);
	while (!node->distributed_work.items.empty ())
	{
		node->distributed_work.cleanup_finished ();
		ASSERT_NO_ERROR (system.poll ());
	}
	count = 0;
	// Test many works for different roots
	for (unsigned i{ 0 }; i < total; ++i)
	{
		futurehead::block_hash hash_i (i + 1);
		ASSERT_FALSE (node->distributed_work.make (futurehead::work_version::work_1, hash_i, node->config.work_peers, node->network_params.network.publish_thresholds.base, callback));
	}
	// 10 roots expected with 1 work each, but some may have completed so test for some
	ASSERT_GT (node->distributed_work.items.size (), 5);
	for (auto & requests : node->distributed_work.items)
	{
		ASSERT_EQ (1, requests.second.size ());
	}
	system.deadline_set (5s);
	while (count < total)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.deadline_set (5s);
	while (!node->distributed_work.items.empty ())
	{
		node->distributed_work.cleanup_finished ();
		ASSERT_NO_ERROR (system.poll ());
	}
	count = 0;
}

TEST (distributed_work, peer)
{
	futurehead::system system;
	futurehead::node_config node_config;
	node_config.peering_port = futurehead::get_available_port ();
	// Disable local work generation
	node_config.work_threads = 0;
	auto node (system.add_node (node_config));
	ASSERT_FALSE (node->local_work_generation_enabled ());
	futurehead::block_hash hash{ 1 };
	boost::optional<uint64_t> work;
	std::atomic<bool> done{ false };
	auto callback = [&work, &done](boost::optional<uint64_t> work_a) {
		ASSERT_TRUE (work_a.is_initialized ());
		work = work_a;
		done = true;
	};
	auto work_peer (std::make_shared<fake_work_peer> (node->work, node->io_ctx, futurehead::get_available_port (), work_peer_type::good));
	work_peer->start ();
	decltype (node->config.work_peers) peers;
	peers.emplace_back ("::ffff:127.0.0.1", work_peer->port ());
	ASSERT_FALSE (node->distributed_work.make (futurehead::work_version::work_1, hash, peers, node->network_params.network.publish_thresholds.base, callback, futurehead::account ()));
	system.deadline_set (5s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_GE (futurehead::work_difficulty (futurehead::work_version::work_1, hash, *work), node->network_params.network.publish_thresholds.base);
	ASSERT_EQ (1, work_peer->generations_good);
	ASSERT_EQ (0, work_peer->generations_bad);
	ASSERT_NO_ERROR (system.poll ());
	ASSERT_EQ (0, work_peer->cancels);
}

TEST (distributed_work, peer_malicious)
{
	futurehead::system system (1);
	auto node (system.nodes[0]);
	ASSERT_TRUE (node->local_work_generation_enabled ());
	futurehead::block_hash hash{ 1 };
	boost::optional<uint64_t> work;
	std::atomic<bool> done{ false };
	auto callback = [&work, &done](boost::optional<uint64_t> work_a) {
		ASSERT_TRUE (work_a.is_initialized ());
		work = work_a;
		done = true;
	};
	auto malicious_peer (std::make_shared<fake_work_peer> (node->work, node->io_ctx, futurehead::get_available_port (), work_peer_type::malicious));
	malicious_peer->start ();
	decltype (node->config.work_peers) peers;
	peers.emplace_back ("::ffff:127.0.0.1", malicious_peer->port ());
	ASSERT_FALSE (node->distributed_work.make (futurehead::work_version::work_1, hash, peers, node->network_params.network.publish_thresholds.base, callback, futurehead::account ()));
	system.deadline_set (5s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_GE (futurehead::work_difficulty (futurehead::work_version::work_1, hash, *work), node->network_params.network.publish_thresholds.base);
	system.deadline_set (5s);
	while (malicious_peer->generations_bad < 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// make sure it was *not* the malicious peer that replied
	ASSERT_EQ (0, malicious_peer->generations_good);
	// initial generation + the second time when it also starts doing local generation
	// it is possible local work generation finishes before the second request is sent, only 1 failure can be required to pass
	ASSERT_GE (malicious_peer->generations_bad, 1);
	// this peer should not receive a cancel
	ASSERT_EQ (0, malicious_peer->cancels);
	// Test again with no local work generation enabled to make sure the malicious peer is sent more than one request
	node->config.work_threads = 0;
	ASSERT_FALSE (node->local_work_generation_enabled ());
	auto malicious_peer2 (std::make_shared<fake_work_peer> (node->work, node->io_ctx, futurehead::get_available_port (), work_peer_type::malicious));
	malicious_peer2->start ();
	peers[0].second = malicious_peer2->port ();
	ASSERT_FALSE (node->distributed_work.make (futurehead::work_version::work_1, hash, peers, node->network_params.network.publish_thresholds.base, {}, futurehead::account ()));
	system.deadline_set (5s);
	while (malicious_peer2->generations_bad < 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node->distributed_work.cancel (hash);
	ASSERT_EQ (0, malicious_peer2->cancels);
}

TEST (distributed_work, peer_multi)
{
	futurehead::system system (1);
	auto node (system.nodes[0]);
	ASSERT_TRUE (node->local_work_generation_enabled ());
	futurehead::block_hash hash{ 1 };
	boost::optional<uint64_t> work;
	std::atomic<bool> done{ false };
	auto callback = [&work, &done](boost::optional<uint64_t> work_a) {
		ASSERT_TRUE (work_a.is_initialized ());
		work = work_a;
		done = true;
	};
	auto good_peer (std::make_shared<fake_work_peer> (node->work, node->io_ctx, futurehead::get_available_port (), work_peer_type::good));
	auto malicious_peer (std::make_shared<fake_work_peer> (node->work, node->io_ctx, futurehead::get_available_port (), work_peer_type::malicious));
	auto slow_peer (std::make_shared<fake_work_peer> (node->work, node->io_ctx, futurehead::get_available_port (), work_peer_type::slow));
	good_peer->start ();
	malicious_peer->start ();
	slow_peer->start ();
	decltype (node->config.work_peers) peers;
	peers.emplace_back ("localhost", malicious_peer->port ());
	peers.emplace_back ("localhost", slow_peer->port ());
	peers.emplace_back ("localhost", good_peer->port ());
	ASSERT_FALSE (node->distributed_work.make (futurehead::work_version::work_1, hash, peers, node->network_params.network.publish_thresholds.base, callback, futurehead::account ()));
	system.deadline_set (5s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_GE (futurehead::work_difficulty (futurehead::work_version::work_1, hash, *work), node->network_params.network.publish_thresholds.base);
	system.deadline_set (5s);
	while (slow_peer->cancels < 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (0, malicious_peer->generations_good);
	ASSERT_EQ (1, malicious_peer->generations_bad);
	ASSERT_EQ (0, malicious_peer->cancels);

	ASSERT_EQ (0, slow_peer->generations_good);
	ASSERT_EQ (0, slow_peer->generations_bad);
	ASSERT_EQ (1, slow_peer->cancels);

	ASSERT_EQ (1, good_peer->generations_good);
	ASSERT_EQ (0, good_peer->generations_bad);
	ASSERT_EQ (0, good_peer->cancels);
}

TEST (distributed_work, fail_resolve)
{
	futurehead::system system (1);
	auto node (system.nodes[0]);
	futurehead::block_hash hash{ 1 };
	boost::optional<uint64_t> work;
	std::atomic<bool> done{ false };
	auto callback = [&work, &done](boost::optional<uint64_t> work_a) {
		ASSERT_TRUE (work_a.is_initialized ());
		work = work_a;
		done = true;
	};
	decltype (node->config.work_peers) peers;
	peers.emplace_back ("beeb.boop.123z", 0);
	ASSERT_FALSE (node->distributed_work.make (futurehead::work_version::work_1, hash, peers, node->network_params.network.publish_thresholds.base, callback, futurehead::account ()));
	system.deadline_set (5s);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_GE (futurehead::work_difficulty (futurehead::work_version::work_1, hash, *work), node->network_params.network.publish_thresholds.base);
}
