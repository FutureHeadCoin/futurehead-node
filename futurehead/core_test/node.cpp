#include <futurehead/core_test/testutil.hpp>
#include <futurehead/lib/jsonconfig.hpp>
#include <futurehead/node/election.hpp>
#include <futurehead/node/testing.hpp>
#include <futurehead/node/transport/udp.hpp>

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>
#include <boost/variant.hpp>

#include <numeric>

using namespace std::chrono_literals;

namespace
{
void add_required_children_node_config_tree (futurehead::jsonconfig & tree);
}

TEST (node, stop)
{
	futurehead::system system (1);
	ASSERT_NE (system.nodes[0]->wallets.items.end (), system.nodes[0]->wallets.items.begin ());
	system.nodes[0]->stop ();
	system.io_ctx.run ();
	ASSERT_TRUE (true);
}

TEST (node, work_generate)
{
	futurehead::system system (1);
	auto & node (*system.nodes[0]);
	futurehead::block_hash root{ 1 };
	futurehead::work_version version{ futurehead::work_version::work_1 };
	{
		auto difficulty = futurehead::difficulty::from_multiplier (1.5, node.network_params.network.publish_thresholds.base);
		auto work = node.work_generate_blocking (version, root, difficulty);
		ASSERT_TRUE (work.is_initialized ());
		ASSERT_TRUE (futurehead::work_difficulty (version, root, *work) >= difficulty);
	}
	{
		auto difficulty = futurehead::difficulty::from_multiplier (0.5, node.network_params.network.publish_thresholds.base);
		boost::optional<uint64_t> work;
		do
		{
			work = node.work_generate_blocking (version, root, difficulty);
		} while (futurehead::work_difficulty (version, root, *work) >= node.network_params.network.publish_thresholds.base);
		ASSERT_TRUE (work.is_initialized ());
		ASSERT_TRUE (futurehead::work_difficulty (version, root, *work) >= difficulty);
		ASSERT_FALSE (futurehead::work_difficulty (version, root, *work) >= node.network_params.network.publish_thresholds.base);
	}
}

TEST (node, block_store_path_failure)
{
	auto service (boost::make_shared<boost::asio::io_context> ());
	futurehead::alarm alarm (*service);
	auto path (futurehead::unique_path ());
	futurehead::logging logging;
	logging.init (path);
	futurehead::work_pool work (std::numeric_limits<unsigned>::max ());
	auto node (std::make_shared<futurehead::node> (*service, futurehead::get_available_port (), path, alarm, logging, work));
	ASSERT_TRUE (node->wallets.items.empty ());
	node->stop ();
}

TEST (node, password_fanout)
{
	auto service (boost::make_shared<boost::asio::io_context> ());
	futurehead::alarm alarm (*service);
	auto path (futurehead::unique_path ());
	futurehead::node_config config;
	config.peering_port = futurehead::get_available_port ();
	config.logging.init (path);
	futurehead::work_pool work (std::numeric_limits<unsigned>::max ());
	config.password_fanout = 10;
	auto node (std::make_shared<futurehead::node> (*service, path, alarm, config, work));
	auto wallet (node->wallets.create (100));
	ASSERT_EQ (10, wallet->store.password.values.size ());
	node->stop ();
}

TEST (node, balance)
{
	futurehead::system system (1);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto transaction (system.nodes[0]->store.tx_begin_write ());
	ASSERT_EQ (std::numeric_limits<futurehead::uint128_t>::max (), system.nodes[0]->ledger.account_balance (transaction, futurehead::test_genesis_key.pub));
}

TEST (node, representative)
{
	futurehead::system system (1);
	auto block1 (system.nodes[0]->rep_block (futurehead::test_genesis_key.pub));
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		ASSERT_TRUE (system.nodes[0]->ledger.store.block_exists (transaction, block1));
	}
	futurehead::keypair key;
	ASSERT_TRUE (system.nodes[0]->rep_block (key.pub).is_zero ());
}

TEST (node, send_unkeyed)
{
	futurehead::system system (1);
	futurehead::keypair key2;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	system.wallet (0)->store.password.value_set (futurehead::keypair ().prv);
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
}

TEST (node, send_self)
{
	futurehead::system system (1);
	futurehead::keypair key2;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (std::numeric_limits<futurehead::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (futurehead::test_genesis_key.pub));
}

TEST (node, send_single)
{
	futurehead::system system (2);
	futurehead::keypair key2;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<futurehead::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (futurehead::test_genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, send_single_observing_peer)
{
	futurehead::system system (3);
	futurehead::keypair key2;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<futurehead::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (futurehead::test_genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	system.deadline_set (10s);
	while (std::any_of (system.nodes.begin (), system.nodes.end (), [&](std::shared_ptr<futurehead::node> const & node_a) { return node_a->balance (key2.pub).is_zero (); }))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, send_single_many_peers)
{
	futurehead::system system (10);
	futurehead::keypair key2;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<futurehead::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (futurehead::test_genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	system.deadline_set (3.5min);
	while (std::any_of (system.nodes.begin (), system.nodes.end (), [&](std::shared_ptr<futurehead::node> const & node_a) { return node_a->balance (key2.pub).is_zero (); }))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.stop ();
	for (auto node : system.nodes)
	{
		ASSERT_TRUE (node->stopped);
		ASSERT_TRUE (node->network.tcp_channels.node_id_handhake_sockets_empty ());
	}
}

TEST (node, send_out_of_order)
{
	futurehead::system system (2);
	auto & node1 (*system.nodes[0]);
	futurehead::keypair key2;
	futurehead::genesis genesis;
	futurehead::send_block send1 (genesis.hash (), key2.pub, std::numeric_limits<futurehead::uint128_t>::max () - node1.config.receive_minimum.number (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ()));
	futurehead::send_block send2 (send1.hash (), key2.pub, std::numeric_limits<futurehead::uint128_t>::max () - node1.config.receive_minimum.number () * 2, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (send1.hash ()));
	futurehead::send_block send3 (send2.hash (), key2.pub, std::numeric_limits<futurehead::uint128_t>::max () - node1.config.receive_minimum.number () * 3, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (send2.hash ()));
	node1.process_active (std::make_shared<futurehead::send_block> (send3));
	node1.process_active (std::make_shared<futurehead::send_block> (send2));
	node1.process_active (std::make_shared<futurehead::send_block> (send1));
	system.deadline_set (10s);
	while (std::any_of (system.nodes.begin (), system.nodes.end (), [&](std::shared_ptr<futurehead::node> const & node_a) { return node_a->balance (futurehead::test_genesis_key.pub) != futurehead::genesis_amount - node1.config.receive_minimum.number () * 3; }))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, quick_confirm)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::keypair key;
	futurehead::block_hash previous (node1.latest (futurehead::test_genesis_key.pub));
	auto genesis_start_balance (node1.balance (futurehead::test_genesis_key.pub));
	system.wallet (0)->insert_adhoc (key.prv);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto send (std::make_shared<futurehead::send_block> (previous, key.pub, node1.config.online_weight_minimum.number () + 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (previous)));
	node1.process_active (send);
	system.deadline_set (10s);
	while (node1.balance (key.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (node1.balance (futurehead::test_genesis_key.pub), node1.config.online_weight_minimum.number () + 1);
	ASSERT_EQ (node1.balance (key.pub), genesis_start_balance - (node1.config.online_weight_minimum.number () + 1));
}

TEST (node, node_receive_quorum)
{
	futurehead::system system;
	futurehead::node_flags node_flags;
	node_flags.disable_udp = false;
	auto & node1 = *system.add_node (node_flags);
	futurehead::keypair key;
	futurehead::block_hash previous (node1.latest (futurehead::test_genesis_key.pub));
	system.wallet (0)->insert_adhoc (key.prv);
	auto send (std::make_shared<futurehead::send_block> (previous, key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (previous)));
	node1.process_active (send);
	system.deadline_set (10s);
	while (!node1.ledger.block_exists (send->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	{
		futurehead::lock_guard<std::mutex> guard (node1.active.mutex);
		auto info (node1.active.roots.find (futurehead::qualified_root (previous, previous)));
		ASSERT_NE (node1.active.roots.end (), info);
		ASSERT_FALSE (info->election->confirmed ());
		ASSERT_EQ (1, info->election->last_votes.size ());
	}
	futurehead::system system2;
	system2.add_node (node_flags);

	system2.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	ASSERT_TRUE (node1.balance (key.pub).is_zero ());
	auto channel (std::make_shared<futurehead::transport::channel_udp> (node1.network.udp_channels, system2.nodes[0]->network.endpoint (), node1.network_params.protocol.protocol_version));
	node1.network.send_keepalive (channel);
	while (node1.balance (key.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
		ASSERT_NO_ERROR (system2.poll ());
	}
}

TEST (node, auto_bootstrap)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	futurehead::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_udp = false;
	auto node0 = system.add_node (config, node_flags);
	futurehead::keypair key2;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	auto send1 (system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, node0->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	system.deadline_set (10s);
	while (node0->balance (key2.pub) != node0->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto node1 (std::make_shared<futurehead::node> (system.io_ctx, futurehead::get_available_port (), futurehead::unique_path (), system.alarm, system.logging, system.work, node_flags));
	ASSERT_FALSE (node1->init_error ());
	auto channel (std::make_shared<futurehead::transport::channel_udp> (node1->network.udp_channels, node0->network.endpoint (), node1->network_params.protocol.protocol_version));
	node1->network.send_keepalive (channel);
	node1->start ();
	system.nodes.push_back (node1);
	system.deadline_set (10s);
	while (!node1->bootstrap_initiator.in_progress ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.deadline_set (10s);
	while (node1->balance (key2.pub) != node0->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.deadline_set (10s);
	while (node1->bootstrap_initiator.in_progress ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_TRUE (node1->ledger.block_exists (send1->hash ()));
	// Wait block receive
	system.deadline_set (5s);
	while (node1->ledger.cache.block_count < 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Confirmation for all blocks
	system.deadline_set (5s);
	while (node1->ledger.cache.cemented_count < 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	auto transaction = node1->store.tx_begin_read ();
	ASSERT_EQ (node1->ledger.cache.unchecked_count, node1->store.unchecked_count (transaction));

	node1->stop ();
}

TEST (node, auto_bootstrap_reverse)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	futurehead::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_udp = false;
	auto node0 = system.add_node (config, node_flags);
	futurehead::keypair key2;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	auto node1 (std::make_shared<futurehead::node> (system.io_ctx, futurehead::get_available_port (), futurehead::unique_path (), system.alarm, system.logging, system.work, node_flags));
	ASSERT_FALSE (node1->init_error ());
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, node0->config.receive_minimum.number ()));
	auto channel (std::make_shared<futurehead::transport::channel_udp> (node0->network.udp_channels, node1->network.endpoint (), node0->network_params.protocol.protocol_version));
	node0->network.send_keepalive (channel);
	node1->start ();
	system.nodes.push_back (node1);
	system.deadline_set (10s);
	while (node1->balance (key2.pub) != node0->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

TEST (node, receive_gap)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	ASSERT_EQ (0, node1.gap_cache.size ());
	auto block (std::make_shared<futurehead::send_block> (5, 1, 2, futurehead::keypair ().prv, 4, 0));
	node1.work_generate_blocking (*block);
	futurehead::publish message (block);
	node1.network.process_message (message, node1.network.udp_channels.create (node1.network.endpoint ()));
	node1.block_processor.flush ();
	ASSERT_EQ (1, node1.gap_cache.size ());
}

TEST (node, merge_peers)
{
	futurehead::system system (1);
	std::array<futurehead::endpoint, 8> endpoints;
	endpoints.fill (futurehead::endpoint (boost::asio::ip::address_v6::loopback (), futurehead::get_available_port ()));
	endpoints[0] = futurehead::endpoint (boost::asio::ip::address_v6::loopback (), futurehead::get_available_port ());
	system.nodes[0]->network.merge_peers (endpoints);
	ASSERT_EQ (0, system.nodes[0]->network.size ());
}

TEST (node, search_pending)
{
	futurehead::system system (1);
	auto node (system.nodes[0]);
	futurehead::keypair key2;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_pending ());
	system.deadline_set (10s);
	while (node->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, search_pending_same)
{
	futurehead::system system (1);
	auto node (system.nodes[0]);
	futurehead::keypair key2;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_pending ());
	system.deadline_set (10s);
	while (node->balance (key2.pub) != 2 * node->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, search_pending_multiple)
{
	futurehead::system system (1);
	auto node (system.nodes[0]);
	futurehead::keypair key2;
	futurehead::keypair key3;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key3.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key3.pub, node->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (node->balance (key3.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (key3.pub, key2.pub, node->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_pending ());
	system.deadline_set (10s);
	while (node->balance (key2.pub) != 2 * node->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, search_pending_confirmed)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto node = system.add_node (node_config);
	futurehead::keypair key2;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto send1 (system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	auto send2 (system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send2);
	system.deadline_set (10s);
	while (!node->active.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	bool confirmed (false);
	system.deadline_set (5s);
	while (!confirmed)
	{
		auto transaction (node->store.tx_begin_read ());
		confirmed = node->ledger.block_confirmed (transaction, send2->hash ());
		ASSERT_NO_ERROR (system.poll ());
	}
	{
		auto transaction (node->wallets.tx_begin_write ());
		system.wallet (0)->store.erase (transaction, futurehead::test_genesis_key.pub);
	}
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_pending ());
	{
		futurehead::lock_guard<std::mutex> guard (node->active.mutex);
		auto existing1 (node->active.blocks.find (send1->hash ()));
		ASSERT_EQ (node->active.blocks.end (), existing1);
		auto existing2 (node->active.blocks.find (send2->hash ()));
		ASSERT_EQ (node->active.blocks.end (), existing2);
	}
	system.deadline_set (10s);
	while (node->balance (key2.pub) != 2 * node->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, unlock_search)
{
	futurehead::system system (1);
	auto node (system.nodes[0]);
	futurehead::keypair key2;
	futurehead::uint128_t balance (node->balance (futurehead::test_genesis_key.pub));
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_write ());
		system.wallet (0)->store.rekey (transaction, "");
	}
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (node->balance (futurehead::test_genesis_key.pub) == balance)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!node->active.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.wallet (0)->insert_adhoc (key2.prv);
	{
		futurehead::lock_guard<std::recursive_mutex> lock (system.wallet (0)->store.mutex);
		system.wallet (0)->store.password.value_set (futurehead::keypair ().prv);
	}
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_write ());
		ASSERT_FALSE (system.wallet (0)->enter_password (transaction, ""));
	}
	system.deadline_set (10s);
	while (node->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, connect_after_junk)
{
	futurehead::system system;
	futurehead::node_flags node_flags;
	node_flags.disable_udp = false;
	auto node0 = system.add_node (node_flags);
	auto node1 (std::make_shared<futurehead::node> (system.io_ctx, futurehead::get_available_port (), futurehead::unique_path (), system.alarm, system.logging, system.work, node_flags));
	std::vector<uint8_t> junk_buffer;
	junk_buffer.push_back (0);
	auto channel1 (std::make_shared<futurehead::transport::channel_udp> (node1->network.udp_channels, node0->network.endpoint (), node1->network_params.protocol.protocol_version));
	channel1->send_buffer (futurehead::shared_const_buffer (std::move (junk_buffer)), futurehead::stat::detail::bulk_pull, [](boost::system::error_code const &, size_t) {});
	system.deadline_set (10s);
	while (node0->stats.count (futurehead::stat::type::error) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->start ();
	system.nodes.push_back (node1);
	auto channel2 (std::make_shared<futurehead::transport::channel_udp> (node1->network.udp_channels, node0->network.endpoint (), node1->network_params.protocol.protocol_version));
	node1->network.send_keepalive (channel2);
	system.deadline_set (10s);
	while (node1->network.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

TEST (node, working)
{
	auto path (futurehead::working_path ());
	ASSERT_FALSE (path.empty ());
}

TEST (node, price)
{
	futurehead::system system (1);
	auto price1 (system.nodes[0]->price (futurehead::Gxrb_ratio, 1));
	ASSERT_EQ (futurehead::node::price_max * 100.0, price1);
	auto price2 (system.nodes[0]->price (futurehead::Gxrb_ratio * int(futurehead::node::free_cutoff + 1), 1));
	ASSERT_EQ (0, price2);
	auto price3 (system.nodes[0]->price (futurehead::Gxrb_ratio * int(futurehead::node::free_cutoff + 2) / 2, 1));
	ASSERT_EQ (futurehead::node::price_max * 100.0 / 2, price3);
	auto price4 (system.nodes[0]->price (futurehead::Gxrb_ratio * int(futurehead::node::free_cutoff) * 2, 1));
	ASSERT_EQ (0, price4);
}

TEST (node, confirm_locked)
{
	futurehead::system system (1);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto transaction (system.wallet (0)->wallets.tx_begin_read ());
	system.wallet (0)->enter_password (transaction, "1");
	auto block (std::make_shared<futurehead::send_block> (0, 0, 0, futurehead::keypair ().prv, 0, 0));
	system.nodes[0]->network.flood_block (block);
}

TEST (node_config, serialization)
{
	auto path (futurehead::unique_path ());
	futurehead::logging logging1;
	logging1.init (path);
	futurehead::node_config config1 (100, logging1);
	config1.bootstrap_fraction_numerator = 10;
	config1.receive_minimum = 10;
	config1.online_weight_minimum = 10;
	config1.online_weight_quorum = 10;
	config1.password_fanout = 20;
	config1.enable_voting = false;
	config1.callback_address = "test";
	config1.callback_port = 10;
	config1.callback_target = "test";
	config1.deprecated_lmdb_max_dbs = 256;
	futurehead::jsonconfig tree;
	config1.serialize_json (tree);
	futurehead::logging logging2;
	logging2.init (path);
	logging2.node_lifetime_tracing_value = !logging2.node_lifetime_tracing_value;
	futurehead::node_config config2 (50, logging2);
	ASSERT_NE (config2.bootstrap_fraction_numerator, config1.bootstrap_fraction_numerator);
	ASSERT_NE (config2.peering_port, config1.peering_port);
	ASSERT_NE (config2.logging.node_lifetime_tracing_value, config1.logging.node_lifetime_tracing_value);
	ASSERT_NE (config2.online_weight_minimum, config1.online_weight_minimum);
	ASSERT_NE (config2.online_weight_quorum, config1.online_weight_quorum);
	ASSERT_NE (config2.password_fanout, config1.password_fanout);
	ASSERT_NE (config2.enable_voting, config1.enable_voting);
	ASSERT_NE (config2.callback_address, config1.callback_address);
	ASSERT_NE (config2.callback_port, config1.callback_port);
	ASSERT_NE (config2.callback_target, config1.callback_target);
	ASSERT_NE (config2.deprecated_lmdb_max_dbs, config1.deprecated_lmdb_max_dbs);

	ASSERT_FALSE (tree.get_optional<std::string> ("epoch_block_link"));
	ASSERT_FALSE (tree.get_optional<std::string> ("epoch_block_signer"));

	bool upgraded (false);
	ASSERT_FALSE (config2.deserialize_json (upgraded, tree));
	ASSERT_FALSE (upgraded);
	ASSERT_EQ (config2.bootstrap_fraction_numerator, config1.bootstrap_fraction_numerator);
	ASSERT_EQ (config2.peering_port, config1.peering_port);
	ASSERT_EQ (config2.logging.node_lifetime_tracing_value, config1.logging.node_lifetime_tracing_value);
	ASSERT_EQ (config2.online_weight_minimum, config1.online_weight_minimum);
	ASSERT_EQ (config2.online_weight_quorum, config1.online_weight_quorum);
	ASSERT_EQ (config2.password_fanout, config1.password_fanout);
	ASSERT_EQ (config2.enable_voting, config1.enable_voting);
	ASSERT_EQ (config2.callback_address, config1.callback_address);
	ASSERT_EQ (config2.callback_port, config1.callback_port);
	ASSERT_EQ (config2.callback_target, config1.callback_target);
	ASSERT_EQ (config2.deprecated_lmdb_max_dbs, config1.deprecated_lmdb_max_dbs);
}

TEST (node_config, v1_v2_upgrade)
{
	auto path (futurehead::unique_path ());
	futurehead::logging logging1;
	logging1.init (path);
	futurehead::jsonconfig tree;
	tree.put ("peering_port", std::to_string (0));
	tree.put ("packet_delay_microseconds", std::to_string (0));
	tree.put ("bootstrap_fraction_numerator", std::to_string (0));
	tree.put ("creation_rebroadcast", std::to_string (0));
	tree.put ("rebroadcast_delay", std::to_string (0));
	tree.put ("receive_minimum", futurehead::amount (0).to_string_dec ());
	futurehead::jsonconfig logging_l;
	logging1.serialize_json (logging_l);
	tree.put_child ("logging", logging_l);
	futurehead::jsonconfig preconfigured_peers_l;
	tree.put_child ("preconfigured_peers", preconfigured_peers_l);
	futurehead::jsonconfig preconfigured_representatives_l;
	tree.put_child ("preconfigured_representatives", preconfigured_representatives_l);
	bool upgraded (false);
	futurehead::node_config config1;
	config1.logging.init (path);
	ASSERT_FALSE (tree.get_optional_child ("work_peers"));
	config1.deserialize_json (upgraded, tree);
	ASSERT_TRUE (upgraded);
	ASSERT_TRUE (!!tree.get_optional_child ("work_peers"));
}

TEST (node_config, v2_v3_upgrade)
{
	futurehead::jsonconfig tree;
	add_required_children_node_config_tree (tree);
	tree.put ("peering_port", std::to_string (0));
	tree.put ("packet_delay_microseconds", std::to_string (0));
	tree.put ("bootstrap_fraction_numerator", std::to_string (0));
	tree.put ("creation_rebroadcast", std::to_string (0));
	tree.put ("rebroadcast_delay", std::to_string (0));
	tree.put ("receive_minimum", futurehead::amount (0).to_string_dec ());
	tree.put ("version", "2");

	futurehead::jsonconfig preconfigured_representatives_l;
	preconfigured_representatives_l.push ("TR6ZJ4pdp6HC76xMRpVDny5x2s8AEbrhFue3NKVxYYdmKuTEib");
	tree.replace_child ("preconfigured_representatives", preconfigured_representatives_l);

	bool upgraded (false);
	futurehead::node_config config1;
	auto path (futurehead::unique_path ());
	config1.logging.init (path);
	ASSERT_FALSE (tree.get_optional<std::string> ("inactive_supply"));
	ASSERT_FALSE (tree.get_optional<std::string> ("password_fanout"));
	ASSERT_FALSE (tree.get_optional<std::string> ("io_threads"));
	ASSERT_FALSE (tree.get_optional<std::string> ("work_threads"));
	config1.deserialize_json (upgraded, tree);
	//ASSERT_EQ (futurehead::uint128_union (0).to_string_dec (), tree.get<std::string> ("inactive_supply"));
	ASSERT_EQ ("1024", tree.get<std::string> ("password_fanout"));
	ASSERT_NE (0, std::stoul (tree.get<std::string> ("password_fanout")));
	ASSERT_TRUE (upgraded);
	auto version (tree.get<std::string> ("version"));
	ASSERT_GT (std::stoull (version), 2);
}

TEST (node_config, v15_v16_upgrade)
{
	auto test_upgrade = [](auto old_preconfigured_peers_url, auto new_preconfigured_peers_url) {
		auto path (futurehead::unique_path ());
		futurehead::jsonconfig tree;
		add_required_children_node_config_tree (tree);
		tree.put ("version", "15");

		const char * dummy_peer = "127.5.2.1";
		futurehead::jsonconfig preconfigured_peers_json;
		preconfigured_peers_json.push (old_preconfigured_peers_url);
		preconfigured_peers_json.push (dummy_peer);
		tree.replace_child ("preconfigured_peers", preconfigured_peers_json);

		auto upgraded (false);
		futurehead::node_config config;
		config.logging.init (path);
		// These config options should not be present at version 15
		ASSERT_FALSE (tree.get_optional_child ("allow_local_peers"));
		ASSERT_FALSE (tree.get_optional_child ("signature_checker_threads"));
		ASSERT_FALSE (tree.get_optional_child ("vote_minimum"));
		config.deserialize_json (upgraded, tree);
		// The config options should be added after the upgrade
		ASSERT_TRUE (!!tree.get_optional_child ("allow_local_peers"));
		ASSERT_TRUE (!!tree.get_optional_child ("signature_checker_threads"));
		ASSERT_TRUE (!!tree.get_optional_child ("vote_minimum"));

		ASSERT_TRUE (upgraded);
		auto version (tree.get<std::string> ("version"));

		auto read_preconfigured_peers_json (tree.get_required_child ("preconfigured_peers"));
		std::vector<std::string> preconfigured_peers;
		read_preconfigured_peers_json.array_entries<std::string> ([&preconfigured_peers](const auto & entry) {
			preconfigured_peers.push_back (entry);
		});

		// Check that the new peer is updated while the other peer is untouched
		ASSERT_EQ (preconfigured_peers.size (), 2);
		ASSERT_EQ (preconfigured_peers.front (), new_preconfigured_peers_url);
		ASSERT_EQ (preconfigured_peers.back (), dummy_peer);

		// Check version is updated
		ASSERT_GT (std::stoull (version), 15);
	};

	// Check that upgrades work with both
	//test_upgrade ("rai.raiblocks.net", "peering.futurehead.org");
	//test_upgrade ("rai-beta.raiblocks.net", "peering-beta.futurehead.org");
}

TEST (node_config, v16_values)
{
	futurehead::jsonconfig tree;
	add_required_children_node_config_tree (tree);

	auto path (futurehead::unique_path ());
	auto upgraded (false);
	futurehead::node_config config;
	config.logging.init (path);

	// Check config is correct
	tree.put ("allow_local_peers", false);
	tree.put ("signature_checker_threads", 1);
	tree.put ("vote_minimum", futurehead::Gxrb_ratio.convert_to<std::string> ());
	config.deserialize_json (upgraded, tree);
	ASSERT_FALSE (upgraded);
	ASSERT_FALSE (config.allow_local_peers);
	ASSERT_EQ (config.signature_checker_threads, 1);
	ASSERT_EQ (config.vote_minimum.number (), futurehead::Gxrb_ratio);

	// Check config is correct with other values
	tree.put ("allow_local_peers", true);
	tree.put ("signature_checker_threads", 4);
	tree.put ("vote_minimum", (std::numeric_limits<futurehead::uint128_t>::max () - 100).convert_to<std::string> ());
	upgraded = false;
	config.deserialize_json (upgraded, tree);
	ASSERT_FALSE (upgraded);
	ASSERT_TRUE (config.allow_local_peers);
	ASSERT_EQ (config.signature_checker_threads, 4);
	ASSERT_EQ (config.vote_minimum.number (), std::numeric_limits<futurehead::uint128_t>::max () - 100);
}

TEST (node_config, v16_v17_upgrade)
{
	auto path (futurehead::unique_path ());
	futurehead::jsonconfig tree;
	add_required_children_node_config_tree (tree);
	tree.put ("version", "16");

	auto upgraded (false);
	futurehead::node_config config;
	config.logging.init (path);
	// These config options should not be present
	ASSERT_FALSE (tree.get_optional_child ("tcp_io_timeout"));
	ASSERT_FALSE (tree.get_optional_child ("pow_sleep_interval"));
	ASSERT_FALSE (tree.get_optional_child ("external_address"));
	ASSERT_FALSE (tree.get_optional_child ("external_port"));
	ASSERT_FALSE (tree.get_optional_child ("tcp_incoming_connections_max"));
	ASSERT_FALSE (tree.get_optional_child ("vote_generator_delay"));
	ASSERT_FALSE (tree.get_optional_child ("vote_generator_threshold"));
	ASSERT_FALSE (tree.get_optional_child ("diagnostics"));
	ASSERT_FALSE (tree.get_optional_child ("use_memory_pools"));
	ASSERT_FALSE (tree.get_optional_child ("confirmation_history_size"));
	ASSERT_FALSE (tree.get_optional_child ("active_elections_size"));
	ASSERT_FALSE (tree.get_optional_child ("bandwidth_limit"));
	ASSERT_FALSE (tree.get_optional_child ("conf_height_processor_batch_min_time"));

	config.deserialize_json (upgraded, tree);
	// The config options should be added after the upgrade
	ASSERT_TRUE (!!tree.get_optional_child ("tcp_io_timeout"));
	ASSERT_TRUE (!!tree.get_optional_child ("pow_sleep_interval"));
	ASSERT_TRUE (!!tree.get_optional_child ("external_address"));
	ASSERT_TRUE (!!tree.get_optional_child ("external_port"));
	ASSERT_TRUE (!!tree.get_optional_child ("tcp_incoming_connections_max"));
	ASSERT_TRUE (!!tree.get_optional_child ("vote_generator_delay"));
	ASSERT_TRUE (!!tree.get_optional_child ("vote_generator_threshold"));
	ASSERT_TRUE (!!tree.get_optional_child ("diagnostics"));
	ASSERT_TRUE (!!tree.get_optional_child ("use_memory_pools"));
	ASSERT_TRUE (!!tree.get_optional_child ("confirmation_history_size"));
	ASSERT_TRUE (!!tree.get_optional_child ("active_elections_size"));
	ASSERT_TRUE (!!tree.get_optional_child ("bandwidth_limit"));
	ASSERT_TRUE (!!tree.get_optional_child ("conf_height_processor_batch_min_time"));

	ASSERT_TRUE (upgraded);
	auto version (tree.get<std::string> ("version"));

	// Check version is updated
	ASSERT_GT (std::stoull (version), 16);
}

TEST (node_config, v17_values)
{
	futurehead::jsonconfig tree;
	add_required_children_node_config_tree (tree);

	auto path (futurehead::unique_path ());
	auto upgraded (false);
	futurehead::node_config config;
	config.logging.init (path);

	// Check config is correct
	{
		tree.put ("tcp_io_timeout", 1);
		tree.put ("pow_sleep_interval", 0);
		tree.put ("external_address", "::1");
		tree.put ("external_port", 0);
		tree.put ("tcp_incoming_connections_max", 1);
		tree.put ("vote_generator_delay", 50);
		tree.put ("vote_generator_threshold", 3);
		futurehead::jsonconfig txn_tracking_l;
		txn_tracking_l.put ("enable", false);
		txn_tracking_l.put ("min_read_txn_time", 0);
		txn_tracking_l.put ("min_write_txn_time", 0);
		txn_tracking_l.put ("ignore_writes_below_block_processor_max_time", true);
		futurehead::jsonconfig diagnostics_l;
		diagnostics_l.put_child ("txn_tracking", txn_tracking_l);
		tree.put_child ("diagnostics", diagnostics_l);
		tree.put ("use_memory_pools", true);
		tree.put ("confirmation_history_size", 2048);
		tree.put ("active_elections_size", 50000);
		tree.put ("bandwidth_limit", 10485760);
		tree.put ("conf_height_processor_batch_min_time", 0);
	}

	config.deserialize_json (upgraded, tree);
	ASSERT_FALSE (upgraded);
	ASSERT_EQ (config.tcp_io_timeout.count (), 1);
	ASSERT_EQ (config.pow_sleep_interval.count (), 0);
	ASSERT_EQ (config.external_address, "::1");
	ASSERT_EQ (config.external_port, 0);
	ASSERT_EQ (config.tcp_incoming_connections_max, 1);
	ASSERT_FALSE (config.diagnostics_config.txn_tracking.enable);
	ASSERT_EQ (config.diagnostics_config.txn_tracking.min_read_txn_time.count (), 0);
	ASSERT_EQ (config.diagnostics_config.txn_tracking.min_write_txn_time.count (), 0);
	ASSERT_TRUE (config.diagnostics_config.txn_tracking.ignore_writes_below_block_processor_max_time);
	ASSERT_TRUE (config.use_memory_pools);
	ASSERT_EQ (config.confirmation_history_size, 2048);
	ASSERT_EQ (config.active_elections_size, 50000);
	ASSERT_EQ (config.bandwidth_limit, 10485760);
	ASSERT_EQ (config.conf_height_processor_batch_min_time.count (), 0);

	// Check config is correct with other values
	tree.put ("tcp_io_timeout", std::numeric_limits<unsigned long>::max () - 100);
	tree.put ("pow_sleep_interval", std::numeric_limits<unsigned long>::max () - 100);
	tree.put ("external_address", "::ffff:192.168.1.1");
	tree.put ("external_port", std::numeric_limits<uint16_t>::max () - 1);
	tree.put ("tcp_incoming_connections_max", std::numeric_limits<unsigned>::max ());
	tree.put ("vote_generator_delay", std::numeric_limits<unsigned long>::max () - 100);
	tree.put ("vote_generator_threshold", 10);
	futurehead::jsonconfig txn_tracking_l;
	txn_tracking_l.put ("enable", true);
	txn_tracking_l.put ("min_read_txn_time", 1234);
	txn_tracking_l.put ("min_write_txn_time", std::numeric_limits<unsigned>::max ());
	txn_tracking_l.put ("ignore_writes_below_block_processor_max_time", false);
	futurehead::jsonconfig diagnostics_l;
	diagnostics_l.replace_child ("txn_tracking", txn_tracking_l);
	tree.replace_child ("diagnostics", diagnostics_l);
	tree.put ("use_memory_pools", false);
	tree.put ("confirmation_history_size", std::numeric_limits<unsigned long long>::max ());
	tree.put ("active_elections_size", std::numeric_limits<unsigned long long>::max ());
	tree.put ("bandwidth_limit", std::numeric_limits<size_t>::max ());
	tree.put ("conf_height_processor_batch_min_time", 500);

	upgraded = false;
	config.deserialize_json (upgraded, tree);
	ASSERT_FALSE (upgraded);
	ASSERT_EQ (config.tcp_io_timeout.count (), std::numeric_limits<unsigned long>::max () - 100);
	ASSERT_EQ (config.pow_sleep_interval.count (), std::numeric_limits<unsigned long>::max () - 100);
	ASSERT_EQ (config.external_address, "::ffff:192.168.1.1");
	ASSERT_EQ (config.external_port, std::numeric_limits<uint16_t>::max () - 1);
	ASSERT_EQ (config.tcp_incoming_connections_max, std::numeric_limits<unsigned>::max ());
	ASSERT_EQ (config.vote_generator_delay.count (), std::numeric_limits<unsigned long>::max () - 100);
	ASSERT_EQ (config.vote_generator_threshold, 10);
	ASSERT_TRUE (config.diagnostics_config.txn_tracking.enable);
	ASSERT_EQ (config.diagnostics_config.txn_tracking.min_read_txn_time.count (), 1234);
	ASSERT_EQ (config.tcp_incoming_connections_max, std::numeric_limits<unsigned>::max ());
	ASSERT_EQ (config.diagnostics_config.txn_tracking.min_write_txn_time.count (), std::numeric_limits<unsigned>::max ());
	ASSERT_FALSE (config.diagnostics_config.txn_tracking.ignore_writes_below_block_processor_max_time);
	ASSERT_FALSE (config.use_memory_pools);
	ASSERT_EQ (config.confirmation_history_size, std::numeric_limits<unsigned long long>::max ());
	ASSERT_EQ (config.active_elections_size, std::numeric_limits<unsigned long long>::max ());
	ASSERT_EQ (config.bandwidth_limit, std::numeric_limits<size_t>::max ());
	ASSERT_EQ (config.conf_height_processor_batch_min_time.count (), 500);
}

TEST (node_config, v17_v18_upgrade)
{
	auto path (futurehead::unique_path ());
	futurehead::jsonconfig tree;
	add_required_children_node_config_tree (tree);
	tree.put ("version", "17");

	auto upgraded (false);
	futurehead::node_config config;
	config.logging.init (path);

	// Initial values for configs that should be upgraded
	config.active_elections_size = 50000;
	config.vote_generator_delay = 500ms;

	// These config options should not be present
	ASSERT_FALSE (tree.get_optional_child ("backup_before_upgrade"));
	ASSERT_FALSE (tree.get_optional_child ("work_watcher_period"));

	config.deserialize_json (upgraded, tree);

	// These configs should have been upgraded
	ASSERT_EQ (100, tree.get<unsigned> ("vote_generator_delay"));
	ASSERT_EQ (10000, tree.get<unsigned long long> ("active_elections_size"));

	// The config options should be added after the upgrade
	ASSERT_TRUE (!!tree.get_optional_child ("backup_before_upgrade"));
	ASSERT_TRUE (!!tree.get_optional_child ("work_watcher_period"));

	ASSERT_TRUE (upgraded);
	auto version (tree.get<std::string> ("version"));

	// Check version is updated
	ASSERT_GT (std::stoull (version), 17);
}

TEST (node_config, v18_values)
{
	futurehead::jsonconfig tree;
	add_required_children_node_config_tree (tree);

	auto path (futurehead::unique_path ());
	auto upgraded (false);
	futurehead::node_config config;
	config.logging.init (path);

	// Check config is correct
	{
		tree.put ("active_elections_size", 10000);
		tree.put ("vote_generator_delay", 100);
		tree.put ("backup_before_upgrade", true);
		tree.put ("work_watcher_period", 5);
	}

	config.deserialize_json (upgraded, tree);
	ASSERT_FALSE (upgraded);
	ASSERT_EQ (config.active_elections_size, 10000);
	ASSERT_EQ (config.vote_generator_delay.count (), 100);
	ASSERT_EQ (config.backup_before_upgrade, true);
	ASSERT_EQ (config.work_watcher_period.count (), 5);

	// Check config is correct with other values
	tree.put ("active_elections_size", 5);
	tree.put ("vote_generator_delay", std::numeric_limits<unsigned long>::max () - 100);
	tree.put ("backup_before_upgrade", false);
	tree.put ("work_watcher_period", 999);

	upgraded = false;
	config.deserialize_json (upgraded, tree);
	ASSERT_FALSE (upgraded);
	ASSERT_EQ (config.active_elections_size, 5);
	ASSERT_EQ (config.vote_generator_delay.count (), std::numeric_limits<unsigned long>::max () - 100);
	ASSERT_EQ (config.backup_before_upgrade, false);
	ASSERT_EQ (config.work_watcher_period.count (), 999);
}

// Regression test to ensure that deserializing includes changes node via get_required_child
TEST (node_config, required_child)
{
	auto path (futurehead::unique_path ());
	futurehead::logging logging1;
	futurehead::logging logging2;
	logging1.init (path);
	futurehead::jsonconfig tree;

	futurehead::jsonconfig logging_l;
	logging1.serialize_json (logging_l);
	tree.put_child ("logging", logging_l);
	auto child_l (tree.get_required_child ("logging"));
	child_l.put<bool> ("flush", !logging1.flush);
	bool upgraded (false);
	logging2.deserialize_json (upgraded, child_l);

	ASSERT_NE (logging1.flush, logging2.flush);
}

TEST (node_config, random_rep)
{
	auto path (futurehead::unique_path ());
	futurehead::logging logging1;
	logging1.init (path);
	futurehead::node_config config1 (100, logging1);
	auto rep (config1.random_representative ());
	ASSERT_NE (config1.preconfigured_representatives.end (), std::find (config1.preconfigured_representatives.begin (), config1.preconfigured_representatives.end (), rep));
}

class json_initial_value_test final
{
public:
	explicit json_initial_value_test (std::string const & text_a) :
	text (text_a)
	{
	}
	futurehead::error serialize_json (futurehead::jsonconfig & json)
	{
		json.put ("thing", text);
		return json.get_error ();
	}
	std::string text;
};

class json_upgrade_test final
{
public:
	futurehead::error deserialize_json (bool & upgraded, futurehead::jsonconfig & json)
	{
		if (!json.empty ())
		{
			auto text_l (json.get<std::string> ("thing"));
			if (text_l == "junktest" || text_l == "created")
			{
				upgraded = true;
				text_l = "changed";
				json.put ("thing", text_l);
			}
			if (text_l == "error")
			{
				json.get_error () = futurehead::error_common::generic;
			}
			text = text_l;
		}
		else
		{
			upgraded = true;
			text = "created";
			json.put ("thing", text);
		}
		return json.get_error ();
	}
	std::string text;
};

/** Both create and upgrade via read_and_update() */
TEST (json, create_and_upgrade)
{
	auto path (futurehead::unique_path ());
	futurehead::jsonconfig json;
	json_upgrade_test object1;
	ASSERT_FALSE (json.read_and_update (object1, path));
	ASSERT_EQ ("created", object1.text);

	futurehead::jsonconfig json2;
	json_upgrade_test object2;
	ASSERT_FALSE (json2.read_and_update (object2, path));
	ASSERT_EQ ("changed", object2.text);
}

/** Create config manually, then upgrade via read_and_update() with multiple calls to test idempotence */
TEST (json, upgrade_from_existing)
{
	auto path (futurehead::unique_path ());
	futurehead::jsonconfig json;
	json_initial_value_test junktest ("junktest");
	junktest.serialize_json (json);
	json.write (path);
	json_upgrade_test object1;
	ASSERT_FALSE (json.read_and_update (object1, path));
	ASSERT_EQ ("changed", object1.text);
	ASSERT_FALSE (json.read_and_update (object1, path));
	ASSERT_EQ ("changed", object1.text);
}

/** Test that backups are made only when there is an upgrade */
TEST (json, backup)
{
	auto dir (futurehead::unique_path ());
	namespace fs = boost::filesystem;
	fs::create_directory (dir);
	auto path = dir / dir.leaf ();

	// Create json file
	futurehead::jsonconfig json;
	json_upgrade_test object1;
	ASSERT_FALSE (json.read_and_update (object1, path));
	ASSERT_EQ ("created", object1.text);

	/** Returns 'dir' if backup file cannot be found */
	auto get_backup_path = [&dir]() {
		for (fs::directory_iterator itr (dir); itr != fs::directory_iterator (); ++itr)
		{
			if (itr->path ().filename ().string ().find ("_backup_") != std::string::npos)
			{
				return itr->path ();
			}
		}
		return dir;
	};

	auto get_file_count = [&dir]() {
		return std::count_if (boost::filesystem::directory_iterator (dir), boost::filesystem::directory_iterator (), static_cast<bool (*) (const boost::filesystem::path &)> (boost::filesystem::is_regular_file));
	};

	// There should only be the original file in this directory
	ASSERT_EQ (get_file_count (), 1);
	ASSERT_EQ (get_backup_path (), dir);

	// Upgrade, check that there is a backup which matches the first object
	ASSERT_FALSE (json.read_and_update (object1, path));
	ASSERT_EQ (get_file_count (), 2);
	ASSERT_NE (get_backup_path (), path);

	// Check there is a backup which has the same contents as the original file
	futurehead::jsonconfig json1;
	ASSERT_FALSE (json1.read (get_backup_path ()));
	ASSERT_EQ (json1.get<std::string> ("thing"), "created");

	// Try and upgrade an already upgraded file, should not create any backups
	ASSERT_FALSE (json.read_and_update (object1, path));
	ASSERT_EQ (get_file_count (), 2);
}

TEST (node_flags, disable_tcp_realtime)
{
	futurehead::system system;
	futurehead::node_flags node_flags;
	node_flags.disable_udp = false;
	auto node1 = system.add_node (node_flags);
	node_flags.disable_tcp_realtime = true;
	auto node2 = system.add_node (node_flags);
	ASSERT_EQ (1, node1->network.size ());
	auto list1 (node1->network.list (2));
	ASSERT_EQ (node2->network.endpoint (), list1[0]->get_endpoint ());
	ASSERT_EQ (futurehead::transport::transport_type::udp, list1[0]->get_type ());
	ASSERT_EQ (1, node2->network.size ());
	auto list2 (node2->network.list (2));
	ASSERT_EQ (node1->network.endpoint (), list2[0]->get_endpoint ());
	ASSERT_EQ (futurehead::transport::transport_type::udp, list2[0]->get_type ());
}

TEST (node_flags, disable_tcp_realtime_and_bootstrap_listener)
{
	futurehead::system system;
	futurehead::node_flags node_flags;
	node_flags.disable_udp = false;
	auto node1 = system.add_node (node_flags);
	node_flags.disable_tcp_realtime = true;
	node_flags.disable_bootstrap_listener = true;
	auto node2 = system.add_node (node_flags);
	ASSERT_EQ (futurehead::tcp_endpoint (boost::asio::ip::address_v6::loopback (), 0), node2->bootstrap.endpoint ());
	ASSERT_NE (futurehead::endpoint (boost::asio::ip::address_v6::loopback (), 0), node2->network.endpoint ());
	ASSERT_EQ (1, node1->network.size ());
	auto list1 (node1->network.list (2));
	ASSERT_EQ (node2->network.endpoint (), list1[0]->get_endpoint ());
	ASSERT_EQ (futurehead::transport::transport_type::udp, list1[0]->get_type ());
	ASSERT_EQ (1, node2->network.size ());
	auto list2 (node2->network.list (2));
	ASSERT_EQ (node1->network.endpoint (), list2[0]->get_endpoint ());
	ASSERT_EQ (futurehead::transport::transport_type::udp, list2[0]->get_type ());
}

// UDP is disabled by default
TEST (node_flags, disable_udp)
{
	futurehead::system system;
	futurehead::node_flags node_flags;
	node_flags.disable_udp = false;
	auto node1 = system.add_node (node_flags);
	auto node2 (std::make_shared<futurehead::node> (system.io_ctx, futurehead::unique_path (), system.alarm, futurehead::node_config (futurehead::get_available_port (), system.logging), system.work));
	system.nodes.push_back (node2);
	node2->start ();
	ASSERT_EQ (futurehead::endpoint (boost::asio::ip::address_v6::loopback (), 0), node2->network.udp_channels.get_local_endpoint ());
	ASSERT_NE (futurehead::endpoint (boost::asio::ip::address_v6::loopback (), 0), node2->network.endpoint ());
	// Send UDP message
	auto channel (std::make_shared<futurehead::transport::channel_udp> (node1->network.udp_channels, node2->network.endpoint (), node2->network_params.protocol.protocol_version));
	node1->network.send_keepalive (channel);
	std::this_thread::sleep_for (std::chrono::milliseconds (500));
	// Check empty network
	ASSERT_EQ (0, node1->network.size ());
	ASSERT_EQ (0, node2->network.size ());
	// Send TCP handshake
	node1->network.merge_peer (node2->network.endpoint ());
	system.deadline_set (5s);
	while (node1->bootstrap.realtime_count != 1 || node2->bootstrap.realtime_count != 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (1, node1->network.size ());
	auto list1 (node1->network.list (2));
	ASSERT_EQ (node2->network.endpoint (), list1[0]->get_endpoint ());
	ASSERT_EQ (futurehead::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_EQ (1, node2->network.size ());
	auto list2 (node2->network.list (2));
	ASSERT_EQ (node1->network.endpoint (), list2[0]->get_endpoint ());
	ASSERT_EQ (futurehead::transport::transport_type::tcp, list2[0]->get_type ());
	node2->stop ();
}

TEST (node, fork_publish)
{
	std::weak_ptr<futurehead::node> node0;
	{
		futurehead::system system (1);
		node0 = system.nodes[0];
		auto & node1 (*system.nodes[0]);
		system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
		futurehead::keypair key1;
		futurehead::genesis genesis;
		auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
		node1.work_generate_blocking (*send1);
		futurehead::keypair key2;
		auto send2 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
		node1.work_generate_blocking (*send2);
		node1.process_active (send1);
		node1.block_processor.flush ();
		ASSERT_EQ (1, node1.active.size ());
		futurehead::unique_lock<std::mutex> lock (node1.active.mutex);
		auto existing (node1.active.roots.find (send1->qualified_root ()));
		ASSERT_NE (node1.active.roots.end (), existing);
		auto election (existing->election);
		lock.unlock ();
		system.deadline_set (1s);
		// Wait until the genesis rep activated & makes vote
		while (election->last_votes_size () != 2)
		{
			node1.vote_processor.flush ();
			ASSERT_NO_ERROR (system.poll ());
		}
		node1.process_active (send2);
		node1.block_processor.flush ();
		lock.lock ();
		auto existing1 (election->last_votes.find (futurehead::test_genesis_key.pub));
		ASSERT_NE (election->last_votes.end (), existing1);
		ASSERT_EQ (send1->hash (), existing1->second.hash);
		auto transaction (node1.store.tx_begin_read ());
		auto winner (*election->tally ().begin ());
		ASSERT_EQ (*send1, *winner.second);
		ASSERT_EQ (futurehead::genesis_amount - 100, winner.first);
	}
	ASSERT_TRUE (node0.expired ());
}

// Tests that an election gets started correctly from a fork
TEST (node, fork_publish_inactive)
{
	futurehead::system system (1);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	futurehead::keypair key2;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, send1->block_work ()));
	auto & node (*system.nodes[0]);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*send1).code);
	ASSERT_EQ (futurehead::process_result::fork, node.process_local (send2).code);
	auto election (node.active.election (send1->qualified_root ()));
	ASSERT_NE (election, nullptr);
	{
		futurehead::lock_guard<std::mutex> guard (node.active.mutex);
		auto & blocks (election->blocks);
		ASSERT_NE (blocks.end (), blocks.find (send1->hash ()));
		ASSERT_NE (blocks.end (), blocks.find (send2->hash ()));
		ASSERT_NE (election->status.winner, send1);
		ASSERT_NE (election->status.winner, send2);
	}
}

TEST (node, fork_keep)
{
	futurehead::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	futurehead::keypair key1;
	futurehead::keypair key2;
	futurehead::genesis genesis;
	// send1 and send2 fork to different accounts
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	node1.process_active (send1);
	node1.block_processor.flush ();
	node2.process_active (send1);
	node2.block_processor.flush ();
	ASSERT_EQ (1, node1.active.size ());
	ASSERT_EQ (1, node2.active.size ());
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	node1.process_active (send2);
	node1.block_processor.flush ();
	node2.process_active (send2);
	node2.block_processor.flush ();
	futurehead::unique_lock<std::mutex> lock (node2.active.mutex);
	auto conflict (node2.active.roots.find (futurehead::qualified_root (genesis.hash (), genesis.hash ())));
	ASSERT_NE (node2.active.roots.end (), conflict);
	auto votes1 (conflict->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (1, votes1->last_votes.size ());
	lock.unlock ();
	{
		auto transaction0 (node1.store.tx_begin_read ());
		auto transaction1 (node2.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction0, send1->hash ()));
		ASSERT_TRUE (node2.store.block_exists (transaction1, send1->hash ()));
	}
	system.deadline_set (1.5min);
	// Wait until the genesis rep makes a vote
	while (votes1->last_votes_size () == 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto transaction0 (node1.store.tx_begin_read ());
	auto transaction1 (node2.store.tx_begin_read ());
	// The vote should be in agreement with what we already have.
	lock.lock ();
	auto winner (*votes1->tally ().begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (futurehead::genesis_amount - 100, winner.first);
	ASSERT_TRUE (node1.store.block_exists (transaction0, send1->hash ()));
	ASSERT_TRUE (node2.store.block_exists (transaction1, send1->hash ()));
}

TEST (node, fork_flip)
{
	futurehead::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	futurehead::keypair key1;
	futurehead::genesis genesis;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	futurehead::publish publish1 (send1);
	futurehead::keypair key2;
	auto send2 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	futurehead::publish publish2 (send2);
	auto channel1 (node1.network.udp_channels.create (node1.network.endpoint ()));
	node1.network.process_message (publish1, channel1);
	node1.block_processor.flush ();
	auto channel2 (node2.network.udp_channels.create (node1.network.endpoint ()));
	node2.network.process_message (publish2, channel2);
	node2.block_processor.flush ();
	ASSERT_EQ (1, node1.active.size ());
	ASSERT_EQ (1, node2.active.size ());
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	node1.network.process_message (publish2, channel1);
	node1.block_processor.flush ();
	node2.network.process_message (publish1, channel2);
	node2.block_processor.flush ();
	futurehead::unique_lock<std::mutex> lock (node2.active.mutex);
	auto conflict (node2.active.roots.find (futurehead::qualified_root (genesis.hash (), genesis.hash ())));
	ASSERT_NE (node2.active.roots.end (), conflict);
	auto votes1 (conflict->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (1, votes1->last_votes.size ());
	lock.unlock ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, publish1.block->hash ()));
	}
	{
		auto transaction (node2.store.tx_begin_read ());
		ASSERT_TRUE (node2.store.block_exists (transaction, publish2.block->hash ()));
	}
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
		done = node2.ledger.block_exists (publish1.block->hash ());
	}
	auto transaction1 (node1.store.tx_begin_read ());
	auto transaction2 (node2.store.tx_begin_read ());
	lock.lock ();
	auto winner (*votes1->tally ().begin ());
	ASSERT_EQ (*publish1.block, *winner.second);
	ASSERT_EQ (futurehead::genesis_amount - 100, winner.first);
	ASSERT_TRUE (node1.store.block_exists (transaction1, publish1.block->hash ()));
	ASSERT_TRUE (node2.store.block_exists (transaction2, publish1.block->hash ()));
	ASSERT_FALSE (node2.store.block_exists (transaction2, publish2.block->hash ()));
}

TEST (node, fork_multi_flip)
{
	std::vector<futurehead::transport::transport_type> types{ futurehead::transport::transport_type::tcp, futurehead::transport::transport_type::udp };
	for (auto & type : types)
	{
		futurehead::system system;
		futurehead::node_flags node_flags;
		if (type == futurehead::transport::transport_type::udp)
		{
			node_flags.disable_tcp_realtime = true;
			node_flags.disable_bootstrap_listener = true;
			node_flags.disable_udp = false;
		}
		futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
		node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
		auto & node1 (*system.add_node (node_config, node_flags, type));
		node_config.peering_port = futurehead::get_available_port ();
		auto & node2 (*system.add_node (node_config, node_flags, type));
		ASSERT_EQ (1, node1.network.size ());
		futurehead::keypair key1;
		futurehead::genesis genesis;
		auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
		futurehead::publish publish1 (send1);
		futurehead::keypair key2;
		auto send2 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
		futurehead::publish publish2 (send2);
		auto send3 (std::make_shared<futurehead::send_block> (publish2.block->hash (), key2.pub, futurehead::genesis_amount - 100, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (publish2.block->hash ())));
		futurehead::publish publish3 (send3);
		node1.network.process_message (publish1, node1.network.udp_channels.create (node1.network.endpoint ()));
		node2.network.process_message (publish2, node2.network.udp_channels.create (node2.network.endpoint ()));
		node2.network.process_message (publish3, node2.network.udp_channels.create (node2.network.endpoint ()));
		node1.block_processor.flush ();
		node2.block_processor.flush ();
		ASSERT_EQ (1, node1.active.size ());
		ASSERT_EQ (1, node2.active.size ());
		system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
		node1.network.process_message (publish2, node1.network.udp_channels.create (node1.network.endpoint ()));
		node1.network.process_message (publish3, node1.network.udp_channels.create (node1.network.endpoint ()));
		node1.block_processor.flush ();
		node2.network.process_message (publish1, node2.network.udp_channels.create (node2.network.endpoint ()));
		node2.block_processor.flush ();
		futurehead::unique_lock<std::mutex> lock (node2.active.mutex);
		auto conflict (node2.active.roots.find (futurehead::qualified_root (genesis.hash (), genesis.hash ())));
		ASSERT_NE (node2.active.roots.end (), conflict);
		auto votes1 (conflict->election);
		ASSERT_NE (nullptr, votes1);
		ASSERT_EQ (1, votes1->last_votes.size ());
		lock.unlock ();
		{
			auto transaction (node1.store.tx_begin_read ());
			ASSERT_TRUE (node1.store.block_exists (transaction, publish1.block->hash ()));
		}
		{
			auto transaction (node2.store.tx_begin_read ());
			ASSERT_TRUE (node2.store.block_exists (transaction, publish2.block->hash ()));
			ASSERT_TRUE (node2.store.block_exists (transaction, publish3.block->hash ()));
		}
		system.deadline_set (10s);
		auto done (false);
		while (!done)
		{
			ASSERT_NO_ERROR (system.poll ());
			done = node2.ledger.block_exists (publish1.block->hash ());
		}
		auto transaction1 (node1.store.tx_begin_read ());
		auto transaction2 (node2.store.tx_begin_read ());
		lock.lock ();
		auto winner (*votes1->tally ().begin ());
		ASSERT_EQ (*publish1.block, *winner.second);
		ASSERT_EQ (futurehead::genesis_amount - 100, winner.first);
		ASSERT_TRUE (node1.store.block_exists (transaction1, publish1.block->hash ()));
		ASSERT_TRUE (node2.store.block_exists (transaction2, publish1.block->hash ()));
		ASSERT_FALSE (node2.store.block_exists (transaction2, publish2.block->hash ()));
		ASSERT_FALSE (node2.store.block_exists (transaction2, publish3.block->hash ()));
	}
}

// Blocks that are no longer actively being voted on should be able to be evicted through bootstrapping.
// This could happen if a fork wasn't resolved before the process previously shut down
TEST (node, fork_bootstrap_flip)
{
	futurehead::system system0;
	futurehead::system system1;
	futurehead::node_config config0 (futurehead::get_available_port (), system0.logging);
	config0.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	futurehead::node_flags node_flags;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_udp = false;
	auto & node1 (*system0.add_node (config0, node_flags));
	futurehead::node_config config1 (futurehead::get_available_port (), system1.logging);
	config1.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto & node2 (*system1.add_node (config1, node_flags));
	system0.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::block_hash latest (node1.latest (futurehead::test_genesis_key.pub));
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::send_block> (latest, key1.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system0.work.generate (latest)));
	futurehead::keypair key2;
	auto send2 (std::make_shared<futurehead::send_block> (latest, key2.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system0.work.generate (latest)));
	// Insert but don't rebroadcast, simulating settled blocks
	node1.block_processor.add (send1, futurehead::seconds_since_epoch ());
	node1.block_processor.flush ();
	node2.block_processor.add (send2, futurehead::seconds_since_epoch ());
	node2.block_processor.flush ();
	{
		auto transaction (node2.store.tx_begin_read ());
		ASSERT_TRUE (node2.store.block_exists (transaction, send2->hash ()));
	}
	auto channel (std::make_shared<futurehead::transport::channel_udp> (node1.network.udp_channels, node2.network.endpoint (), node2.network_params.protocol.protocol_version));
	node1.network.send_keepalive (channel);
	system1.deadline_set (50s);
	while (node2.network.empty ())
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
	node2.bootstrap_initiator.bootstrap (node1.network.endpoint ());
	auto again (true);
	system1.deadline_set (50s);
	while (again)
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
		auto transaction (node2.store.tx_begin_read ());
		again = !node2.store.block_exists (transaction, send1->hash ());
	}
}

TEST (node, fork_open)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::keypair key1;
	futurehead::genesis genesis;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	futurehead::publish publish1 (send1);
	auto channel1 (node1.network.udp_channels.create (node1.network.endpoint ()));
	node1.network.process_message (publish1, channel1);
	node1.block_processor.flush ();
	{
		auto election = node1.active.election (publish1.block->qualified_root ());
		futurehead::lock_guard<std::mutex> guard (node1.active.mutex);
		election->confirm_once ();
	}
	ASSERT_TIMELY (3s, node1.active.empty () && node1.block_confirmed (publish1.block->hash ()));
	auto open1 (std::make_shared<futurehead::open_block> (publish1.block->hash (), 1, key1.pub, key1.prv, key1.pub, *system.work.generate (key1.pub)));
	futurehead::publish publish2 (open1);
	node1.network.process_message (publish2, channel1);
	node1.block_processor.flush ();
	ASSERT_EQ (1, node1.active.size ());
	auto open2 (std::make_shared<futurehead::open_block> (publish1.block->hash (), 2, key1.pub, key1.prv, key1.pub, *system.work.generate (key1.pub)));
	futurehead::publish publish3 (open2);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	node1.network.process_message (publish3, channel1);
	node1.block_processor.flush ();
	{
		auto election = node1.active.election (publish3.block->qualified_root ());
		futurehead::lock_guard<std::mutex> guard (node1.active.mutex);
		ASSERT_EQ (2, election->blocks.size ());
		ASSERT_EQ (publish2.block->hash (), election->status.winner->hash ());
		ASSERT_FALSE (election->confirmed ());
	}
	ASSERT_TRUE (node1.block (publish2.block->hash ()));
	ASSERT_FALSE (node1.block (publish3.block->hash ()));
}

TEST (node, fork_open_flip)
{
	futurehead::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	futurehead::keypair key1;
	futurehead::genesis genesis;
	futurehead::keypair rep1;
	futurehead::keypair rep2;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, futurehead::genesis_amount - 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	// A copy is necessary to avoid data races during ledger processing, which sets the sideband
	auto send1_copy (std::make_shared<futurehead::send_block> (*send1));
	node1.process_active (send1);
	node2.process_active (send1_copy);
	// We should be keeping this block
	auto open1 (std::make_shared<futurehead::open_block> (send1->hash (), rep1.pub, key1.pub, key1.prv, key1.pub, *system.work.generate (key1.pub)));
	// This block should be evicted
	auto open2 (std::make_shared<futurehead::open_block> (send1->hash (), rep2.pub, key1.pub, key1.prv, key1.pub, *system.work.generate (key1.pub)));
	ASSERT_FALSE (*open1 == *open2);
	// node1 gets copy that will remain
	node1.process_active (open1);
	node1.block_processor.flush ();
	node1.block_confirm (open1);
	// node2 gets copy that will be evicted
	node2.process_active (open2);
	node2.block_processor.flush ();
	node2.block_confirm (open2);
	ASSERT_EQ (2, node1.active.size ());
	ASSERT_EQ (2, node2.active.size ());
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	// Notify both nodes that a fork exists
	node1.process_active (open2);
	node1.block_processor.flush ();
	node2.process_active (open1);
	node2.block_processor.flush ();
	futurehead::unique_lock<std::mutex> lock (node2.active.mutex);
	auto conflict (node2.active.roots.find (open1->qualified_root ()));
	ASSERT_NE (node2.active.roots.end (), conflict);
	auto votes1 (conflict->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (1, votes1->last_votes.size ());
	lock.unlock ();
	ASSERT_TRUE (node1.block (open1->hash ()) != nullptr);
	ASSERT_TRUE (node2.block (open2->hash ()) != nullptr);
	system.deadline_set (10s);
	// Node2 should eventually settle on open1
	while (node2.block (open1->hash ()) == nullptr)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node2.block_processor.flush ();
	auto transaction1 (node1.store.tx_begin_read ());
	auto transaction2 (node2.store.tx_begin_read ());
	lock.lock ();
	auto winner (*votes1->tally ().begin ());
	ASSERT_EQ (*open1, *winner.second);
	ASSERT_EQ (futurehead::genesis_amount - 1, winner.first);
	ASSERT_TRUE (node1.store.block_exists (transaction1, open1->hash ()));
	ASSERT_TRUE (node2.store.block_exists (transaction2, open1->hash ()));
	ASSERT_FALSE (node2.store.block_exists (transaction2, open2->hash ()));
}

TEST (node, coherent_observer)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	node1.observers.blocks.add ([&node1](futurehead::election_status const & status_a, futurehead::account const &, futurehead::uint128_t const &, bool) {
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, status_a.winner->hash ()));
	});
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key;
	system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key.pub, 1);
}

TEST (node, fork_no_vote_quorum)
{
	futurehead::system system (3);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	auto & node3 (*system.nodes[2]);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto key4 (system.wallet (0)->deterministic_insert ());
	system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key4, futurehead::genesis_amount / 4);
	auto key1 (system.wallet (1)->deterministic_insert ());
	{
		auto transaction (system.wallet (1)->wallets.tx_begin_write ());
		system.wallet (1)->store.representative_set (transaction, key1);
	}
	auto block (system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key1, node1.config.receive_minimum.number ()));
	ASSERT_NE (nullptr, block);
	system.deadline_set (30s);
	while (node3.balance (key1) != node1.config.receive_minimum.number () || node2.balance (key1) != node1.config.receive_minimum.number () || node1.balance (key1) != node1.config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (node1.config.receive_minimum.number (), node1.weight (key1));
	ASSERT_EQ (node1.config.receive_minimum.number (), node2.weight (key1));
	ASSERT_EQ (node1.config.receive_minimum.number (), node3.weight (key1));
	futurehead::state_block send1 (futurehead::test_genesis_key.pub, block->hash (), futurehead::test_genesis_key.pub, (futurehead::genesis_amount / 4) - (node1.config.receive_minimum.number () * 2), key1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (block->hash ()));
	ASSERT_EQ (futurehead::process_result::progress, node1.process (send1).code);
	ASSERT_EQ (futurehead::process_result::progress, node2.process (send1).code);
	ASSERT_EQ (futurehead::process_result::progress, node3.process (send1).code);
	auto key2 (system.wallet (2)->deterministic_insert ());
	auto send2 (std::make_shared<futurehead::send_block> (block->hash (), key2, (futurehead::genesis_amount / 4) - (node1.config.receive_minimum.number () * 2), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (block->hash ())));
	futurehead::raw_key key3;
	auto transaction (system.wallet (1)->wallets.tx_begin_read ());
	ASSERT_FALSE (system.wallet (1)->store.fetch (transaction, key1, key3));
	auto vote (std::make_shared<futurehead::vote> (key1, key3, 0, send2));
	futurehead::confirm_ack confirm (vote);
	std::vector<uint8_t> buffer;
	{
		futurehead::vectorstream stream (buffer);
		confirm.serialize (stream, false);
	}
	futurehead::transport::channel_udp channel (node2.network.udp_channels, node3.network.endpoint (), node1.network_params.protocol.protocol_version);
	channel.send_buffer (futurehead::shared_const_buffer (std::move (buffer)), futurehead::stat::detail::confirm_ack);
	while (node3.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::in) < 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_TRUE (node1.latest (futurehead::test_genesis_key.pub) == send1.hash ());
	ASSERT_TRUE (node2.latest (futurehead::test_genesis_key.pub) == send1.hash ());
	ASSERT_TRUE (node3.latest (futurehead::test_genesis_key.pub) == send1.hash ());
}

// Disabled because it sometimes takes way too long (but still eventually finishes)
TEST (node, DISABLED_fork_pre_confirm)
{
	futurehead::system system (3);
	auto & node0 (*system.nodes[0]);
	auto & node1 (*system.nodes[1]);
	auto & node2 (*system.nodes[2]);
	futurehead::genesis genesis;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key1;
	system.wallet (1)->insert_adhoc (key1.prv);
	{
		auto transaction (system.wallet (1)->wallets.tx_begin_write ());
		system.wallet (1)->store.representative_set (transaction, key1.pub);
	}
	futurehead::keypair key2;
	system.wallet (2)->insert_adhoc (key2.prv);
	{
		auto transaction (system.wallet (2)->wallets.tx_begin_write ());
		system.wallet (2)->store.representative_set (transaction, key2.pub);
	}
	system.deadline_set (30s);
	auto block0 (system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key1.pub, futurehead::genesis_amount / 3));
	ASSERT_NE (nullptr, block0);
	while (node0.balance (key1.pub) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto block1 (system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, futurehead::genesis_amount / 3));
	ASSERT_NE (nullptr, block1);
	while (node0.balance (key2.pub) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	futurehead::keypair key3;
	futurehead::keypair key4;
	auto block2 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, node0.latest (futurehead::test_genesis_key.pub), key3.pub, node0.balance (futurehead::test_genesis_key.pub), 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	auto block3 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, node0.latest (futurehead::test_genesis_key.pub), key4.pub, node0.balance (futurehead::test_genesis_key.pub), 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node0.work_generate_blocking (*block2);
	node0.work_generate_blocking (*block3);
	node0.process_active (block2);
	node1.process_active (block2);
	node2.process_active (block3);
	auto done (false);
	// Extend deadline; we must finish within a total of 100 seconds
	system.deadline_set (70s);
	while (!done)
	{
		done |= node0.latest (futurehead::test_genesis_key.pub) == block2->hash () && node1.latest (futurehead::test_genesis_key.pub) == block2->hash () && node2.latest (futurehead::test_genesis_key.pub) == block2->hash ();
		done |= node0.latest (futurehead::test_genesis_key.pub) == block3->hash () && node1.latest (futurehead::test_genesis_key.pub) == block3->hash () && node2.latest (futurehead::test_genesis_key.pub) == block3->hash ();
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Sometimes hangs on the bootstrap_initiator.bootstrap call
TEST (node, DISABLED_fork_stale)
{
	futurehead::system system1 (1);
	system1.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::system system2 (1);
	auto & node1 (*system1.nodes[0]);
	auto & node2 (*system2.nodes[0]);
	node2.bootstrap_initiator.bootstrap (node1.network.endpoint ());
	std::shared_ptr<futurehead::transport::channel> channel (std::make_shared<futurehead::transport::channel_udp> (node2.network.udp_channels, node1.network.endpoint (), node2.network_params.protocol.protocol_version));
	auto vote = std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 0, std::vector<futurehead::block_hash> ());
	node2.rep_crawler.response (channel, vote);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	futurehead::keypair key2;
	auto send3 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Mxrb_ratio, key1.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send3);
	node1.process_active (send3);
	system2.deadline_set (10s);
	while (node2.block (send3->hash ()) == nullptr)
	{
		system1.poll ();
		ASSERT_NO_ERROR (system2.poll ());
	}
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send3->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 2 * futurehead::Mxrb_ratio, key1.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto send2 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send3->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 2 * futurehead::Mxrb_ratio, key2.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	{
		auto transaction1 (node1.store.tx_begin_write ());
		ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction1, *send1).code);
		auto transaction2 (node2.store.tx_begin_write ());
		ASSERT_EQ (futurehead::process_result::progress, node2.ledger.process (transaction2, *send2).code);
	}
	node1.process_active (send1);
	node1.process_active (send2);
	node2.process_active (send1);
	node2.process_active (send2);
	node2.bootstrap_initiator.bootstrap (node1.network.endpoint ());
	while (node2.block (send1->hash ()) == nullptr)
	{
		system1.poll ();
		ASSERT_NO_ERROR (system2.poll ());
	}
}

TEST (node, broadcast_elected)
{
	std::vector<futurehead::transport::transport_type> types{ futurehead::transport::transport_type::tcp, futurehead::transport::transport_type::udp };
	for (auto & type : types)
	{
		futurehead::node_flags node_flags;
		if (type == futurehead::transport::transport_type::udp)
		{
			node_flags.disable_tcp_realtime = true;
			node_flags.disable_bootstrap_listener = true;
			node_flags.disable_udp = false;
		}
		futurehead::system system;
		futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
		node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
		auto node0 = system.add_node (node_config, node_flags, type);
		node_config.peering_port = futurehead::get_available_port ();
		auto node1 = system.add_node (node_config, node_flags, type);
		node_config.peering_port = futurehead::get_available_port ();
		auto node2 = system.add_node (node_config, node_flags, type);
		futurehead::keypair rep_big;
		futurehead::keypair rep_small;
		futurehead::keypair rep_other;
		{
			auto transaction0 (node0->store.tx_begin_write ());
			auto transaction1 (node1->store.tx_begin_write ());
			auto transaction2 (node2->store.tx_begin_write ());
			futurehead::send_block fund_big (node0->ledger.latest (transaction0, futurehead::test_genesis_key.pub), rep_big.pub, futurehead::Gxrb_ratio * 5, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0);
			futurehead::open_block open_big (fund_big.hash (), rep_big.pub, rep_big.pub, rep_big.prv, rep_big.pub, 0);
			futurehead::send_block fund_small (fund_big.hash (), rep_small.pub, futurehead::Gxrb_ratio * 2, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0);
			futurehead::open_block open_small (fund_small.hash (), rep_small.pub, rep_small.pub, rep_small.prv, rep_small.pub, 0);
			futurehead::send_block fund_other (fund_small.hash (), rep_other.pub, futurehead::Gxrb_ratio * 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0);
			futurehead::open_block open_other (fund_other.hash (), rep_other.pub, rep_other.pub, rep_other.prv, rep_other.pub, 0);
			node0->work_generate_blocking (fund_big);
			node0->work_generate_blocking (open_big);
			node0->work_generate_blocking (fund_small);
			node0->work_generate_blocking (open_small);
			node0->work_generate_blocking (fund_other);
			node0->work_generate_blocking (open_other);
			ASSERT_EQ (futurehead::process_result::progress, node0->ledger.process (transaction0, fund_big).code);
			ASSERT_EQ (futurehead::process_result::progress, node1->ledger.process (transaction1, fund_big).code);
			ASSERT_EQ (futurehead::process_result::progress, node2->ledger.process (transaction2, fund_big).code);
			ASSERT_EQ (futurehead::process_result::progress, node0->ledger.process (transaction0, open_big).code);
			ASSERT_EQ (futurehead::process_result::progress, node1->ledger.process (transaction1, open_big).code);
			ASSERT_EQ (futurehead::process_result::progress, node2->ledger.process (transaction2, open_big).code);
			ASSERT_EQ (futurehead::process_result::progress, node0->ledger.process (transaction0, fund_small).code);
			ASSERT_EQ (futurehead::process_result::progress, node1->ledger.process (transaction1, fund_small).code);
			ASSERT_EQ (futurehead::process_result::progress, node2->ledger.process (transaction2, fund_small).code);
			ASSERT_EQ (futurehead::process_result::progress, node0->ledger.process (transaction0, open_small).code);
			ASSERT_EQ (futurehead::process_result::progress, node1->ledger.process (transaction1, open_small).code);
			ASSERT_EQ (futurehead::process_result::progress, node2->ledger.process (transaction2, open_small).code);
			ASSERT_EQ (futurehead::process_result::progress, node0->ledger.process (transaction0, fund_other).code);
			ASSERT_EQ (futurehead::process_result::progress, node1->ledger.process (transaction1, fund_other).code);
			ASSERT_EQ (futurehead::process_result::progress, node2->ledger.process (transaction2, fund_other).code);
			ASSERT_EQ (futurehead::process_result::progress, node0->ledger.process (transaction0, open_other).code);
			ASSERT_EQ (futurehead::process_result::progress, node1->ledger.process (transaction1, open_other).code);
			ASSERT_EQ (futurehead::process_result::progress, node2->ledger.process (transaction2, open_other).code);
		}
		// Confirm blocks to allow voting
		for (auto & node : system.nodes)
		{
			auto block (node->block (node->latest (futurehead::test_genesis_key.pub)));
			ASSERT_NE (nullptr, block);
			node->block_confirm (block);
			auto election (node->active.election (block->qualified_root ()));
			ASSERT_NE (nullptr, election);
			{
				futurehead::lock_guard<std::mutex> guard (node->active.mutex);
				election->confirm_once ();
			}
			ASSERT_TIMELY (5s, 4 == node->ledger.cache.cemented_count)
		}

		system.wallet (0)->insert_adhoc (rep_big.prv);
		system.wallet (1)->insert_adhoc (rep_small.prv);
		system.wallet (2)->insert_adhoc (rep_other.prv);
		auto fork0 (std::make_shared<futurehead::send_block> (node2->latest (futurehead::test_genesis_key.pub), rep_small.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
		node0->work_generate_blocking (*fork0);
		// A copy is necessary to avoid data races during ledger processing, which sets the sideband
		auto fork0_copy (std::make_shared<futurehead::send_block> (*fork0));
		node0->process_active (fork0);
		node1->process_active (fork0_copy);
		auto fork1 (std::make_shared<futurehead::send_block> (node2->latest (futurehead::test_genesis_key.pub), rep_big.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
		node0->work_generate_blocking (*fork1);
		system.wallet (2)->insert_adhoc (rep_small.prv);
		node2->process_active (fork1);
		system.deadline_set (10s);
		while (!node0->ledger.block_exists (fork0->hash ()) || !node1->ledger.block_exists (fork0->hash ()))
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		system.deadline_set (50s);
		while (!node2->ledger.block_exists (fork0->hash ()))
		{
			auto ec = system.poll ();
			ASSERT_TRUE (node0->ledger.block_exists (fork0->hash ()));
			ASSERT_TRUE (node1->ledger.block_exists (fork0->hash ()));
			ASSERT_NO_ERROR (ec);
		}
		system.deadline_set (5s);
		while (node1->stats.count (futurehead::stat::type::confirmation_observer, futurehead::stat::detail::inactive_conf_height, futurehead::stat::dir::out) == 0)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}
}

TEST (node, rep_self_vote)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.online_weight_minimum = std::numeric_limits<futurehead::uint128_t>::max ();
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto node0 = system.add_node (node_config);
	futurehead::keypair rep_big;
	futurehead::send_block fund_big (node0->ledger.latest (node0->store.tx_begin_read (), futurehead::test_genesis_key.pub), rep_big.pub, futurehead::uint128_t ("0xb0000000000000000000000000000000"), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0);
	futurehead::open_block open_big (fund_big.hash (), rep_big.pub, rep_big.pub, rep_big.prv, rep_big.pub, 0);
	node0->work_generate_blocking (fund_big);
	node0->work_generate_blocking (open_big);
	ASSERT_EQ (futurehead::process_result::progress, node0->process (fund_big).code);
	ASSERT_EQ (futurehead::process_result::progress, node0->process (open_big).code);
	// Confirm both blocks, allowing voting on the upcoming block
	node0->block_confirm (node0->block (open_big.hash ()));
	{
		auto election = node0->active.election (open_big.qualified_root ());
		ASSERT_NE (nullptr, election);
		futurehead::lock_guard<std::mutex> guard (node0->active.mutex);
		election->confirm_once ();
	}

	system.wallet (0)->insert_adhoc (rep_big.prv);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	ASSERT_EQ (system.wallet (0)->wallets.reps ().voting, 2);
	auto block0 (std::make_shared<futurehead::send_block> (node0->latest (futurehead::test_genesis_key.pub), rep_big.pub, futurehead::uint128_t ("0x60000000000000000000000000000000"), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node0->work_generate_blocking (*block0);
	ASSERT_EQ (futurehead::process_result::progress, node0->process (*block0).code);
	auto & active (node0->active);
	auto election1 = active.insert (block0);
	system.deadline_set (1s);
	// Wait until representatives are activated & make vote
	while (election1.election->last_votes_size () != 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	futurehead::unique_lock<std::mutex> lock (active.mutex);
	auto & rep_votes (election1.election->last_votes);
	ASSERT_NE (rep_votes.end (), rep_votes.find (futurehead::test_genesis_key.pub));
	ASSERT_NE (rep_votes.end (), rep_votes.find (rep_big.pub));
}

// Bootstrapping shouldn't republish the blocks to the network.
TEST (node, DISABLED_bootstrap_no_publish)
{
	futurehead::system system0 (1);
	futurehead::system system1 (1);
	auto node0 (system0.nodes[0]);
	auto node1 (system1.nodes[0]);
	futurehead::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	futurehead::send_block send0 (node0->latest (futurehead::test_genesis_key.pub), key0.pub, 500, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0);
	{
		auto transaction (node0->store.tx_begin_write ());
		ASSERT_EQ (futurehead::process_result::progress, node0->ledger.process (transaction, send0).code);
	}
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint ());
	ASSERT_TRUE (node1->active.empty ());
	system1.deadline_set (10s);
	while (node1->block (send0.hash ()) == nullptr)
	{
		// Poll until the TCP connection is torn down and in_progress goes false
		system0.poll ();
		auto ec = system1.poll ();
		// There should never be an active transaction because the only activity is bootstrapping 1 block which shouldn't be publishing.
		ASSERT_TRUE (node1->active.empty ());
		ASSERT_NO_ERROR (ec);
	}
}

// Check that an outgoing bootstrap request can push blocks
TEST (node, bootstrap_bulk_push)
{
	futurehead::system system0;
	futurehead::system system1;
	futurehead::node_config config0 (futurehead::get_available_port (), system0.logging);
	config0.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto node0 (system0.add_node (config0));
	futurehead::node_config config1 (futurehead::get_available_port (), system1.logging);
	config1.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto node1 (system1.add_node (config1));
	futurehead::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	futurehead::send_block send0 (node0->latest (futurehead::test_genesis_key.pub), key0.pub, 500, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0);
	node0->work_generate_blocking (send0);
	{
		auto transaction (node0->store.tx_begin_write ());
		ASSERT_EQ (futurehead::process_result::progress, node0->ledger.process (transaction, send0).code);
	}
	ASSERT_FALSE (node0->bootstrap_initiator.in_progress ());
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	ASSERT_TRUE (node1->active.empty ());
	node0->bootstrap_initiator.bootstrap (node1->network.endpoint (), false);
	system1.deadline_set (10s);
	while (node1->block (send0.hash ()) == nullptr)
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
	// since this uses bulk_push, the new block should be republished
	ASSERT_FALSE (node1->active.empty ());
}

// Bootstrapping a forked open block should succeed.
TEST (node, bootstrap_fork_open)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto node0 = system.add_node (node_config);
	node_config.peering_port = futurehead::get_available_port ();
	auto node1 = system.add_node (node_config);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key0;
	futurehead::send_block send0 (node0->latest (futurehead::test_genesis_key.pub), key0.pub, futurehead::genesis_amount - 500, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0);
	futurehead::open_block open0 (send0.hash (), 1, key0.pub, key0.prv, key0.pub, 0);
	futurehead::open_block open1 (send0.hash (), 2, key0.pub, key0.prv, key0.pub, 0);
	node0->work_generate_blocking (send0);
	node0->work_generate_blocking (open0);
	node0->work_generate_blocking (open1);
	// Both know about send0
	ASSERT_EQ (futurehead::process_result::progress, node0->process (send0).code);
	ASSERT_EQ (futurehead::process_result::progress, node1->process (send0).code);
	// Confirm send0 to allow starting and voting on the following blocks
	for (auto node : system.nodes)
	{
		node->block_confirm (node->block (node->latest (futurehead::test_genesis_key.pub)));
		{
			auto election = node->active.election (send0.qualified_root ());
			ASSERT_NE (nullptr, election);
			futurehead::lock_guard<std::mutex> guard (node->active.mutex);
			election->confirm_once ();
		}
		ASSERT_TIMELY (2s, node->active.empty ());
	}
	ASSERT_TIMELY (3s, node0->block_confirmed (send0.hash ()));
	// They disagree about open0/open1
	ASSERT_EQ (futurehead::process_result::progress, node0->process (open0).code);
	ASSERT_EQ (futurehead::process_result::progress, node1->process (open1).code);
	ASSERT_FALSE (node1->ledger.block_exists (open0.hash ()));
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint ());
	ASSERT_TRUE (node1->active.empty ());
	ASSERT_TIMELY (10s, !node1->ledger.block_exists (open1.hash ()) && node1->ledger.block_exists (open0.hash ()));
}

// Unconfirmed blocks from bootstrap should be confirmed
TEST (node, bootstrap_confirm_frontiers)
{
	futurehead::system system0 (1);
	futurehead::system system1 (1);
	auto node0 (system0.nodes[0]);
	auto node1 (system0.nodes[0]);
	system0.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	futurehead::send_block send0 (node0->latest (futurehead::test_genesis_key.pub), key0.pub, futurehead::genesis_amount - 500, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0);
	node0->work_generate_blocking (send0);
	{
		auto transaction (node0->store.tx_begin_write ());
		ASSERT_EQ (futurehead::process_result::progress, node0->ledger.process (transaction, send0).code);
	}
	ASSERT_FALSE (node0->bootstrap_initiator.in_progress ());
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	ASSERT_TRUE (node1->active.empty ());
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint ());
	system1.deadline_set (10s);
	while (node1->block (send0.hash ()) == nullptr)
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
	// Wait for election start
	system1.deadline_set (10s);
	while (node1->active.empty ())
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
	{
		futurehead::lock_guard<std::mutex> guard (node1->active.mutex);
		auto existing1 (node1->active.blocks.find (send0.hash ()));
		ASSERT_NE (node1->active.blocks.end (), existing1);
	}
	// Wait for confirmation height update
	system1.deadline_set (10s);
	bool done (false);
	while (!done)
	{
		{
			auto transaction (node1->store.tx_begin_read ());
			done = node1->ledger.block_confirmed (transaction, send0.hash ());
		}
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
}

// Test that if we create a block that isn't confirmed, we sync.
TEST (node, DISABLED_unconfirmed_send)
{
	futurehead::system system (2);
	auto & node0 (*system.nodes[0]);
	auto & node1 (*system.nodes[1]);
	auto wallet0 (system.wallet (0));
	auto wallet1 (system.wallet (1));
	futurehead::keypair key0;
	wallet1->insert_adhoc (key0.prv);
	wallet0->insert_adhoc (futurehead::test_genesis_key.prv);
	auto send1 (wallet0->send_action (futurehead::genesis_account, key0.pub, 2 * futurehead::Mxrb_ratio));
	system.deadline_set (10s);
	while (node1.balance (key0.pub) != 2 * futurehead::Mxrb_ratio || node1.bootstrap_initiator.in_progress ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto latest (node1.latest (key0.pub));
	futurehead::state_block send2 (key0.pub, latest, futurehead::genesis_account, futurehead::Mxrb_ratio, futurehead::genesis_account, key0.prv, key0.pub, *node0.work_generate_blocking (latest));
	{
		auto transaction (node1.store.tx_begin_write ());
		ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, send2).code);
	}
	auto send3 (wallet1->send_action (key0.pub, futurehead::genesis_account, futurehead::Mxrb_ratio));
	system.deadline_set (10s);
	while (node0.balance (futurehead::genesis_account) != futurehead::genesis_amount)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Test that nodes can track nodes that have rep weight for priority broadcasting
TEST (node, rep_list)
{
	futurehead::system system (2);
	auto & node1 (*system.nodes[1]);
	auto wallet0 (system.wallet (0));
	auto wallet1 (system.wallet (1));
	// Node0 has a rep
	wallet0->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key1;
	// Broadcast a confirm so others should know this is a rep node
	wallet0->send_action (futurehead::test_genesis_key.pub, key1.pub, futurehead::Mxrb_ratio);
	ASSERT_EQ (0, node1.rep_crawler.representatives (1).size ());
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		auto reps (node1.rep_crawler.representatives (1));
		if (!reps.empty ())
		{
			if (!reps[0].weight.is_zero ())
			{
				done = true;
			}
		}
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, rep_weight)
{
	futurehead::system system (1);
	auto & node (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::keypair keypair1;
	futurehead::keypair keypair2;
	futurehead::block_builder builder;
	auto amount_pr (node.minimum_principal_weight () + 100);
	auto amount_not_pr (node.minimum_principal_weight () - 100);
	std::shared_ptr<futurehead::block> block1 = builder
	                                      .state ()
	                                      .account (futurehead::test_genesis_key.pub)
	                                      .previous (genesis.hash ())
	                                      .representative (futurehead::test_genesis_key.pub)
	                                      .balance (futurehead::genesis_amount - amount_not_pr)
	                                      .link (keypair1.pub)
	                                      .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                      .work (*system.work.generate (genesis.hash ()))
	                                      .build ();
	std::shared_ptr<futurehead::block> block2 = builder
	                                      .state ()
	                                      .account (keypair1.pub)
	                                      .previous (0)
	                                      .representative (keypair1.pub)
	                                      .balance (amount_not_pr)
	                                      .link (block1->hash ())
	                                      .sign (keypair1.prv, keypair1.pub)
	                                      .work (*system.work.generate (keypair1.pub))
	                                      .build ();
	std::shared_ptr<futurehead::block> block3 = builder
	                                      .state ()
	                                      .account (futurehead::test_genesis_key.pub)
	                                      .previous (block1->hash ())
	                                      .representative (futurehead::test_genesis_key.pub)
	                                      .balance (futurehead::genesis_amount - amount_not_pr - amount_pr)
	                                      .link (keypair2.pub)
	                                      .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                      .work (*system.work.generate (block1->hash ()))
	                                      .build ();
	std::shared_ptr<futurehead::block> block4 = builder
	                                      .state ()
	                                      .account (keypair2.pub)
	                                      .previous (0)
	                                      .representative (keypair2.pub)
	                                      .balance (amount_pr)
	                                      .link (block3->hash ())
	                                      .sign (keypair2.prv, keypair2.pub)
	                                      .work (*system.work.generate (keypair2.pub))
	                                      .build ();
	{
		auto transaction = node.store.tx_begin_write ();
		ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (transaction, *block1).code);
		ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (transaction, *block2).code);
		ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (transaction, *block3).code);
		ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (transaction, *block4).code);
	}
	node.network.udp_channels.insert (futurehead::endpoint (boost::asio::ip::address_v6::loopback (), futurehead::get_available_port ()), 0);
	ASSERT_TRUE (node.rep_crawler.representatives (1).empty ());
	futurehead::endpoint endpoint0 (boost::asio::ip::address_v6::loopback (), futurehead::get_available_port ());
	futurehead::endpoint endpoint1 (boost::asio::ip::address_v6::loopback (), futurehead::get_available_port ());
	futurehead::endpoint endpoint2 (boost::asio::ip::address_v6::loopback (), futurehead::get_available_port ());
	std::shared_ptr<futurehead::transport::channel> channel0 (std::make_shared<futurehead::transport::channel_udp> (node.network.udp_channels, endpoint0, node.network_params.protocol.protocol_version));
	std::shared_ptr<futurehead::transport::channel> channel1 (std::make_shared<futurehead::transport::channel_udp> (node.network.udp_channels, endpoint1, node.network_params.protocol.protocol_version));
	std::shared_ptr<futurehead::transport::channel> channel2 (std::make_shared<futurehead::transport::channel_udp> (node.network.udp_channels, endpoint2, node.network_params.protocol.protocol_version));
	node.network.udp_channels.insert (endpoint0, node.network_params.protocol.protocol_version);
	node.network.udp_channels.insert (endpoint1, node.network_params.protocol.protocol_version);
	node.network.udp_channels.insert (endpoint2, node.network_params.protocol.protocol_version);
	auto vote0 = std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 0, genesis.open);
	auto vote1 = std::make_shared<futurehead::vote> (keypair1.pub, keypair1.prv, 0, genesis.open);
	auto vote2 = std::make_shared<futurehead::vote> (keypair2.pub, keypair2.prv, 0, genesis.open);
	node.rep_crawler.response (channel0, vote0);
	node.rep_crawler.response (channel1, vote1);
	node.rep_crawler.response (channel2, vote2);
	system.deadline_set (5s);
	while (node.rep_crawler.representative_count () != 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Make sure we get the rep with the most weight first
	auto reps (node.rep_crawler.representatives (1));
	ASSERT_EQ (1, reps.size ());
	ASSERT_EQ (node.balance (futurehead::test_genesis_key.pub), reps[0].weight.number ());
	ASSERT_EQ (futurehead::test_genesis_key.pub, reps[0].account);
	ASSERT_EQ (*channel0, reps[0].channel_ref ());
	ASSERT_TRUE (node.rep_crawler.is_pr (*channel0));
	ASSERT_FALSE (node.rep_crawler.is_pr (*channel1));
	ASSERT_TRUE (node.rep_crawler.is_pr (*channel2));
}

TEST (node, rep_remove)
{
	futurehead::system system;
	futurehead::node_flags node_flags;
	node_flags.disable_udp = false;
	auto & node = *system.add_node (node_flags);
	futurehead::genesis genesis;
	futurehead::keypair keypair1;
	futurehead::keypair keypair2;
	futurehead::block_builder builder;
	std::shared_ptr<futurehead::block> block1 = builder
	                                      .state ()
	                                      .account (futurehead::test_genesis_key.pub)
	                                      .previous (genesis.hash ())
	                                      .representative (futurehead::test_genesis_key.pub)
	                                      .balance (futurehead::genesis_amount - node.minimum_principal_weight () * 2)
	                                      .link (keypair1.pub)
	                                      .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                      .work (*system.work.generate (genesis.hash ()))
	                                      .build ();
	std::shared_ptr<futurehead::block> block2 = builder
	                                      .state ()
	                                      .account (keypair1.pub)
	                                      .previous (0)
	                                      .representative (keypair1.pub)
	                                      .balance (node.minimum_principal_weight () * 2)
	                                      .link (block1->hash ())
	                                      .sign (keypair1.prv, keypair1.pub)
	                                      .work (*system.work.generate (keypair1.pub))
	                                      .build ();
	std::shared_ptr<futurehead::block> block3 = builder
	                                      .state ()
	                                      .account (futurehead::test_genesis_key.pub)
	                                      .previous (block1->hash ())
	                                      .representative (futurehead::test_genesis_key.pub)
	                                      .balance (futurehead::genesis_amount - node.minimum_principal_weight () * 4)
	                                      .link (keypair2.pub)
	                                      .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                      .work (*system.work.generate (block1->hash ()))
	                                      .build ();
	std::shared_ptr<futurehead::block> block4 = builder
	                                      .state ()
	                                      .account (keypair2.pub)
	                                      .previous (0)
	                                      .representative (keypair2.pub)
	                                      .balance (node.minimum_principal_weight () * 2)
	                                      .link (block3->hash ())
	                                      .sign (keypair2.prv, keypair2.pub)
	                                      .work (*system.work.generate (keypair2.pub))
	                                      .build ();
	{
		auto transaction = node.store.tx_begin_write ();
		ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (transaction, *block1).code);
		ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (transaction, *block2).code);
		ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (transaction, *block3).code);
		ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (transaction, *block4).code);
	}
	// Add inactive UDP representative channel
	futurehead::endpoint endpoint0 (boost::asio::ip::address_v6::loopback (), futurehead::get_available_port ());
	std::shared_ptr<futurehead::transport::channel> channel0 (std::make_shared<futurehead::transport::channel_udp> (node.network.udp_channels, endpoint0, node.network_params.protocol.protocol_version));
	futurehead::amount amount100 (100);
	node.network.udp_channels.insert (endpoint0, node.network_params.protocol.protocol_version);
	auto vote1 = std::make_shared<futurehead::vote> (keypair1.pub, keypair1.prv, 0, genesis.open);
	node.rep_crawler.response (channel0, vote1);
	system.deadline_set (5s);
	while (node.rep_crawler.representative_count () != 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto reps (node.rep_crawler.representatives (1));
	ASSERT_EQ (1, reps.size ());
	ASSERT_EQ (node.minimum_principal_weight () * 2, reps[0].weight.number ());
	ASSERT_EQ (keypair1.pub, reps[0].account);
	ASSERT_EQ (*channel0, reps[0].channel_ref ());
	// This UDP channel is not reachable and should timeout
	while (node.rep_crawler.representative_count () != 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Add working representative
	auto node1 = system.add_node (futurehead::node_config (futurehead::get_available_port (), system.logging));
	system.wallet (1)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto channel1 (node.network.find_channel (node1->network.endpoint ()));
	ASSERT_NE (nullptr, channel1);
	auto vote2 = std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 0, genesis.open);
	node.rep_crawler.response (channel1, vote2);
	while (node.rep_crawler.representative_count () != 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Add inactive TCP representative channel
	auto node2 (std::make_shared<futurehead::node> (system.io_ctx, futurehead::unique_path (), system.alarm, futurehead::node_config (futurehead::get_available_port (), system.logging), system.work));
	std::atomic<bool> done{ false };
	std::weak_ptr<futurehead::node> node_w (node.shared ());
	auto vote3 = std::make_shared<futurehead::vote> (keypair2.pub, keypair2.prv, 0, genesis.open);
	node.network.tcp_channels.start_tcp (node2->network.endpoint (), [node_w, &done, &vote3, &system](std::shared_ptr<futurehead::transport::channel> channel2) {
		if (auto node_l = node_w.lock ())
		{
			node_l->rep_crawler.response (channel2, vote3);
			while (node_l->rep_crawler.representative_count () != 2)
			{
				ASSERT_NO_ERROR (system.poll ());
			}
			done = true;
		}
	});
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node2->stop ();
	// Remove inactive representatives
	system.deadline_set (10s);
	while (node.rep_crawler.representative_count () != 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	reps = node.rep_crawler.representatives (1);
	ASSERT_EQ (futurehead::test_genesis_key.pub, reps[0].account);
	ASSERT_EQ (1, node.network.size ());
	auto list (node.network.list (1));
	ASSERT_EQ (node1->network.endpoint (), list[0]->get_endpoint ());
}

TEST (node, rep_connection_close)
{
	futurehead::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	// Add working representative (node 2)
	system.wallet (1)->insert_adhoc (futurehead::test_genesis_key.prv);
	system.deadline_set (10s);
	while (node1.rep_crawler.representative_count () != 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node2.stop ();
	// Remove representative with closed channel
	system.deadline_set (10s);
	while (node1.rep_crawler.representative_count () != 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Test that nodes can disable representative voting
TEST (node, no_voting)
{
	futurehead::system system (1);
	auto & node0 (*system.nodes[0]);
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.enable_voting = false;
	system.add_node (node_config);

	auto wallet0 (system.wallet (0));
	auto wallet1 (system.wallet (1));
	// Node1 has a rep
	wallet1->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key1;
	wallet1->insert_adhoc (key1.prv);
	// Broadcast a confirm so others should know this is a rep node
	wallet1->send_action (futurehead::test_genesis_key.pub, key1.pub, futurehead::Mxrb_ratio);
	system.deadline_set (10s);
	while (!node0.active.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (0, node0.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::in));
}

TEST (node, send_callback)
{
	futurehead::system system (1);
	auto & node0 (*system.nodes[0]);
	futurehead::keypair key2;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	node0.config.callback_address = "localhost";
	node0.config.callback_port = 8010;
	node0.config.callback_target = "/";
	ASSERT_NE (nullptr, system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key2.pub, node0.config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (node0.balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (std::numeric_limits<futurehead::uint128_t>::max () - node0.config.receive_minimum.number (), node0.balance (futurehead::test_genesis_key.pub));
}

// Check that votes get replayed back to nodes if they sent an old sequence number.
// This helps representatives continue from their last sequence number if their node is reinitialized and the old sequence number is lost
TEST (node, vote_replay)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::keypair key;
	futurehead::genesis genesis;
	for (auto i (0); i < 11000; ++i)
	{
		auto transaction (node1.store.tx_begin_read ());
		auto vote (node1.store.vote_generate (transaction, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, genesis.open));
	}
	auto node2 = system.add_node ();
	{
		auto transaction (node2->store.tx_begin_read ());
		futurehead::lock_guard<std::mutex> lock (node2->store.get_cache_mutex ());
		auto vote (node2->store.vote_current (transaction, futurehead::test_genesis_key.pub));
		ASSERT_EQ (nullptr, vote);
	}
	system.wallet (1)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto done (false);
	system.deadline_set (20s);
	while (!done)
	{
		auto ec = system.poll ();
		auto transaction (node2->store.tx_begin_read ());
		futurehead::lock_guard<std::mutex> lock (node2->store.get_cache_mutex ());
		auto vote (node2->store.vote_current (transaction, futurehead::test_genesis_key.pub));
		done = vote && (vote->sequence >= 10000);
		ASSERT_NO_ERROR (ec);
	}
}

TEST (node, balance_observer)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	std::atomic<int> balances (0);
	futurehead::keypair key;
	node1.observers.account_balance.add ([&key, &balances](futurehead::account const & account_a, bool is_pending) {
		if (key.pub == account_a && is_pending)
		{
			balances++;
		}
		else if (futurehead::test_genesis_key.pub == account_a && !is_pending)
		{
			balances++;
		}
	});
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	system.wallet (0)->send_action (futurehead::test_genesis_key.pub, key.pub, 1);
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		auto ec = system.poll ();
		done = balances.load () == 2;
		ASSERT_NO_ERROR (ec);
	}
}

TEST (node, bootstrap_connection_scaling)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	ASSERT_EQ (34, node1.bootstrap_initiator.connections->target_connections (5000, 1));
	ASSERT_EQ (4, node1.bootstrap_initiator.connections->target_connections (0, 1));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 1));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (10000000000, 1));
	ASSERT_EQ (32, node1.bootstrap_initiator.connections->target_connections (5000, 0));
	ASSERT_EQ (1, node1.bootstrap_initiator.connections->target_connections (0, 0));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 0));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (10000000000, 0));
	ASSERT_EQ (36, node1.bootstrap_initiator.connections->target_connections (5000, 2));
	ASSERT_EQ (8, node1.bootstrap_initiator.connections->target_connections (0, 2));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 2));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (10000000000, 2));
	node1.config.bootstrap_connections = 128;
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (0, 1));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 1));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (0, 2));
	ASSERT_EQ (64, node1.bootstrap_initiator.connections->target_connections (50000, 2));
	node1.config.bootstrap_connections_max = 256;
	ASSERT_EQ (128, node1.bootstrap_initiator.connections->target_connections (0, 1));
	ASSERT_EQ (256, node1.bootstrap_initiator.connections->target_connections (50000, 1));
	ASSERT_EQ (256, node1.bootstrap_initiator.connections->target_connections (0, 2));
	ASSERT_EQ (256, node1.bootstrap_initiator.connections->target_connections (50000, 2));
	node1.config.bootstrap_connections_max = 0;
	ASSERT_EQ (1, node1.bootstrap_initiator.connections->target_connections (0, 1));
	ASSERT_EQ (1, node1.bootstrap_initiator.connections->target_connections (50000, 1));
}

// Test stat counting at both type and detail levels
TEST (node, stat_counting)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	node1.stats.add (futurehead::stat::type::ledger, futurehead::stat::dir::in, 1);
	node1.stats.add (futurehead::stat::type::ledger, futurehead::stat::dir::in, 5);
	node1.stats.inc (futurehead::stat::type::ledger, futurehead::stat::dir::in);
	node1.stats.inc (futurehead::stat::type::ledger, futurehead::stat::detail::send, futurehead::stat::dir::in);
	node1.stats.inc (futurehead::stat::type::ledger, futurehead::stat::detail::send, futurehead::stat::dir::in);
	node1.stats.inc (futurehead::stat::type::ledger, futurehead::stat::detail::receive, futurehead::stat::dir::in);
	ASSERT_EQ (10, node1.stats.count (futurehead::stat::type::ledger, futurehead::stat::dir::in));
	ASSERT_EQ (2, node1.stats.count (futurehead::stat::type::ledger, futurehead::stat::detail::send, futurehead::stat::dir::in));
	ASSERT_EQ (1, node1.stats.count (futurehead::stat::type::ledger, futurehead::stat::detail::receive, futurehead::stat::dir::in));
}

TEST (node, online_reps)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	// 1 sample of minimum weight
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.online_stake ());
	auto vote (std::make_shared<futurehead::vote> ());
	node1.online_reps.observe (futurehead::test_genesis_key.pub);
	// 1 minimum, 1 maximum
	node1.online_reps.sample ();
	ASSERT_EQ (futurehead::genesis_amount, node1.online_reps.online_stake ());
	// 2 minimum, 1 maximum
	node1.online_reps.sample ();
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.online_stake ());
}

TEST (node, block_confirm)
{
	std::vector<futurehead::transport::transport_type> types{ futurehead::transport::transport_type::tcp, futurehead::transport::transport_type::udp };
	for (auto & type : types)
	{
		futurehead::node_flags node_flags;
		if (type == futurehead::transport::transport_type::udp)
		{
			node_flags.disable_tcp_realtime = true;
			node_flags.disable_bootstrap_listener = true;
			node_flags.disable_udp = false;
		}
		futurehead::system system (2, type, node_flags);
		auto & node1 (*system.nodes[0]);
		auto & node2 (*system.nodes[1]);
		futurehead::genesis genesis;
		futurehead::keypair key;
		system.wallet (1)->insert_adhoc (futurehead::test_genesis_key.prv);
		auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node1.work_generate_blocking (genesis.hash ())));
		// A copy is necessary to avoid data races during ledger processing, which sets the sideband
		auto send1_copy (std::make_shared<futurehead::state_block> (*send1));
		node1.block_processor.add (send1, futurehead::seconds_since_epoch ());
		node2.block_processor.add (send1_copy, futurehead::seconds_since_epoch ());
		system.deadline_set (std::chrono::seconds (5));
		while (!node1.ledger.block_exists (send1->hash ()) || !node2.ledger.block_exists (send1_copy->hash ()))
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		ASSERT_TRUE (node1.ledger.block_exists (send1->hash ()));
		ASSERT_TRUE (node2.ledger.block_exists (send1_copy->hash ()));
		auto send2 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send1->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio * 2, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node1.work_generate_blocking (send1->hash ())));
		{
			auto transaction (node1.store.tx_begin_write ());
			ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, *send2).code);
		}
		{
			auto transaction (node2.store.tx_begin_write ());
			ASSERT_EQ (futurehead::process_result::progress, node2.ledger.process (transaction, *send2).code);
		}
		node1.block_confirm (send2);
		ASSERT_TRUE (node1.active.list_recently_cemented ().empty ());
		system.deadline_set (10s);
		while (node1.active.list_recently_cemented ().empty ())
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}
}

TEST (node, block_arrival)
{
	futurehead::system system (1);
	auto & node (*system.nodes[0]);
	ASSERT_EQ (0, node.block_arrival.arrival.size ());
	futurehead::block_hash hash1 (1);
	node.block_arrival.add (hash1);
	ASSERT_EQ (1, node.block_arrival.arrival.size ());
	node.block_arrival.add (hash1);
	ASSERT_EQ (1, node.block_arrival.arrival.size ());
	futurehead::block_hash hash2 (2);
	node.block_arrival.add (hash2);
	ASSERT_EQ (2, node.block_arrival.arrival.size ());
}

TEST (node, block_arrival_size)
{
	futurehead::system system (1);
	auto & node (*system.nodes[0]);
	auto time (std::chrono::steady_clock::now () - futurehead::block_arrival::arrival_time_min - std::chrono::seconds (5));
	futurehead::block_hash hash (0);
	for (auto i (0); i < futurehead::block_arrival::arrival_size_min * 2; ++i)
	{
		node.block_arrival.arrival.push_back (futurehead::block_arrival_info{ time, hash });
		++hash.qwords[0];
	}
	ASSERT_EQ (futurehead::block_arrival::arrival_size_min * 2, node.block_arrival.arrival.size ());
	node.block_arrival.recent (0);
	ASSERT_EQ (futurehead::block_arrival::arrival_size_min, node.block_arrival.arrival.size ());
}

TEST (node, block_arrival_time)
{
	futurehead::system system (1);
	auto & node (*system.nodes[0]);
	auto time (std::chrono::steady_clock::now ());
	futurehead::block_hash hash (0);
	for (auto i (0); i < futurehead::block_arrival::arrival_size_min * 2; ++i)
	{
		node.block_arrival.arrival.push_back (futurehead::block_arrival_info{ time, hash });
		++hash.qwords[0];
	}
	ASSERT_EQ (futurehead::block_arrival::arrival_size_min * 2, node.block_arrival.arrival.size ());
	node.block_arrival.recent (0);
	ASSERT_EQ (futurehead::block_arrival::arrival_size_min * 2, node.block_arrival.arrival.size ());
}

TEST (node, confirm_quorum)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	// Put greater than online_weight_minimum in pending so quorum can't be reached
	futurehead::amount new_balance (node1.config.online_weight_minimum.number () - futurehead::Gxrb_ratio);
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, new_balance, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node1.work_generate_blocking (genesis.hash ())));
	{
		auto transaction (node1.store.tx_begin_write ());
		ASSERT_EQ (futurehead::process_result::progress, node1.ledger.process (transaction, *send1).code);
	}
	system.wallet (0)->send_action (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.pub, new_balance.number ());
	system.deadline_set (10s);
	while (node1.active.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	futurehead::lock_guard<std::mutex> guard (node1.active.mutex);
	auto info (node1.active.roots.find (futurehead::qualified_root (send1->hash (), send1->hash ())));
	ASSERT_NE (node1.active.roots.end (), info);
	ASSERT_FALSE (info->election->confirmed ());
	ASSERT_EQ (1, info->election->last_votes.size ());
	ASSERT_EQ (0, node1.balance (futurehead::test_genesis_key.pub));
}

TEST (node, local_votes_cache)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	node_config.receive_minimum = futurehead::genesis_amount;
	auto & node (*system.add_node (node_config));
	futurehead::genesis genesis;
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	auto send2 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send1->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 2 * futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node.work_generate_blocking (send1->hash ())));
	auto send3 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send2->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 3 * futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node.work_generate_blocking (send2->hash ())));
	{
		auto transaction (node.store.tx_begin_write ());
		ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (transaction, *send1).code);
		ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (transaction, *send2).code);
	}
	// Confirm blocks to allow voting
	node.block_confirm (send2);
	{
		auto election = node.active.election (send2->qualified_root ());
		ASSERT_NE (nullptr, election);
		futurehead::lock_guard<std::mutex> guard (node.active.mutex);
		election->confirm_once ();
	}
	ASSERT_TIMELY (3s, node.ledger.cache.cemented_count == 3);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::confirm_req message1 (send1);
	futurehead::confirm_req message2 (send2);
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.network.process_message (message1, channel);
	auto wait_vote_sequence = [&node, &system](unsigned sequence) {
		std::shared_ptr<futurehead::vote> current_vote;
		system.deadline_set (5s);
		while (current_vote == nullptr || current_vote->sequence < sequence)
		{
			{
				futurehead::lock_guard<std::mutex> lock (node.store.get_cache_mutex ());
				auto transaction (node.store.tx_begin_read ());
				current_vote = node.store.vote_current (transaction, futurehead::test_genesis_key.pub);
			}
			ASSERT_NO_ERROR (system.poll ());
		}
		ASSERT_EQ (sequence, current_vote->sequence);
	};
	wait_vote_sequence (1);
	node.network.process_message (message2, channel);
	wait_vote_sequence (2);
	for (auto i (0); i < 100; ++i)
	{
		node.network.process_message (message1, channel);
		node.network.process_message (message2, channel);
	}
	for (int i = 0; i < 4; ++i)
	{
		system.poll (node.aggregator.max_delay);
	}
	// Make sure a new vote was not generated
	{
		futurehead::lock_guard<std::mutex> lock (node.store.get_cache_mutex ());
		ASSERT_EQ (2, node.store.vote_current (node.store.tx_begin_read (), futurehead::test_genesis_key.pub)->sequence);
	}
	// Max cache
	{
		auto transaction (node.store.tx_begin_write ());
		ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (transaction, *send3).code);
	}
	futurehead::confirm_req message3 (send3);
	for (auto i (0); i < 100; ++i)
	{
		node.network.process_message (message3, channel);
	}
	for (int i = 0; i < 4; ++i)
	{
		system.poll (node.aggregator.max_delay);
	}
	wait_vote_sequence (3);
	ASSERT_TIMELY (3s, node.votes_cache.find (send1->hash ()).empty ());
	ASSERT_FALSE (node.votes_cache.find (send2->hash ()).empty ());
	ASSERT_FALSE (node.votes_cache.find (send3->hash ()).empty ());
}

TEST (node, local_votes_cache_batch)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	ASSERT_GE (node.network_params.voting.max_cache, 2);
	futurehead::genesis genesis;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	std::vector<std::shared_ptr<futurehead::block>> blocks{ genesis.open, send1 };
	std::vector<std::pair<futurehead::block_hash, futurehead::root>> batch{ { genesis.open->hash (), genesis.open->root () }, { send1->hash (), send1->root () } };
	{
		auto transaction (node.store.tx_begin_write ());
		ASSERT_EQ (futurehead::process_result::progress, node.ledger.process (transaction, *send1).code);
	}
	futurehead::confirm_req message (batch);
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	// Generates and sends one vote for both hashes which is then cached
	node.network.process_message (message, channel);
	system.deadline_set (3s);
	while (node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out) < 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (1, node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out));
	ASSERT_FALSE (node.votes_cache.find (genesis.open->hash ()).empty ());
	ASSERT_FALSE (node.votes_cache.find (send1->hash ()).empty ());
	// Only one confirm_ack should be sent if all hashes are part of the same vote
	node.network.process_message (message, channel);
	system.deadline_set (3s);
	while (node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out) < 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (2, node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out));
	// Test when votes are different
	node.votes_cache.remove (genesis.open->hash ());
	node.votes_cache.remove (send1->hash ());
	node.network.process_message (futurehead::confirm_req (genesis.open->hash (), genesis.open->root ()), channel);
	system.deadline_set (3s);
	while (node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out) < 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (3, node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out));
	node.network.process_message (futurehead::confirm_req (send1->hash (), send1->root ()), channel);
	system.deadline_set (3s);
	while (node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out) < 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (4, node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out));
	// There are two different votes, so both should be sent in response
	node.network.process_message (message, channel);
	system.deadline_set (3s);
	while (node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out) < 6)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (6, node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out));
}

TEST (node, local_votes_cache_generate_new_vote)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	futurehead::genesis genesis;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	// Repsond with cached vote
	futurehead::confirm_req message1 (genesis.open);
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.network.process_message (message1, channel);
	system.deadline_set (3s);
	while (node.votes_cache.find (genesis.open->hash ()).empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto votes1 (node.votes_cache.find (genesis.open->hash ()));
	ASSERT_EQ (1, votes1.size ());
	ASSERT_EQ (1, votes1[0]->blocks.size ());
	ASSERT_EQ (genesis.open->hash (), boost::get<futurehead::block_hash> (votes1[0]->blocks[0]));
	{
		futurehead::lock_guard<std::mutex> lock (node.store.get_cache_mutex ());
		auto transaction (node.store.tx_begin_read ());
		auto current_vote (node.store.vote_current (transaction, futurehead::test_genesis_key.pub));
		ASSERT_EQ (current_vote->sequence, 1);
		ASSERT_EQ (current_vote, votes1[0]);
	}
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	ASSERT_EQ (futurehead::process_result::progress, node.process (*send1).code);
	// One of the hashes is cached
	std::vector<std::pair<futurehead::block_hash, futurehead::root>> roots_hashes{ std::make_pair (genesis.open->hash (), genesis.open->root ()), std::make_pair (send1->hash (), send1->root ()) };
	futurehead::confirm_req message2 (roots_hashes);
	node.network.process_message (message2, channel);
	system.deadline_set (3s);
	while (node.votes_cache.find (send1->hash ()).empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto votes2 (node.votes_cache.find (send1->hash ()));
	ASSERT_EQ (1, votes2.size ());
	ASSERT_EQ (1, votes2[0]->blocks.size ());
	{
		futurehead::lock_guard<std::mutex> lock (node.store.get_cache_mutex ());
		auto transaction (node.store.tx_begin_read ());
		auto current_vote (node.store.vote_current (transaction, futurehead::test_genesis_key.pub));
		ASSERT_EQ (current_vote->sequence, 2);
		ASSERT_EQ (current_vote, votes2[0]);
	}
	ASSERT_FALSE (node.votes_cache.find (genesis.open->hash ()).empty ());
	ASSERT_FALSE (node.votes_cache.find (send1->hash ()).empty ());
	// First generated + again cached + new generated
	ASSERT_EQ (3, node.stats.count (futurehead::stat::type::message, futurehead::stat::detail::confirm_ack, futurehead::stat::dir::out));
}

// Tests that the max cache size is inversely proportional to the number of voting accounts
TEST (node, local_votes_cache_size)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	node_config.vote_minimum = 0; // wallet will pick up the second account as voting even if unopened
	auto & node (*system.add_node (node_config));
	ASSERT_EQ (node.network_params.voting.max_cache, 2); // effective cache size is 1 with 2 voting accounts
	futurehead::keypair key;
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (futurehead::test_genesis_key.prv);
	wallet.insert_adhoc (futurehead::keypair ().prv);
	ASSERT_EQ (2, node.wallets.reps ().voting);
	auto transaction (node.store.tx_begin_read ());
	auto vote1 (node.store.vote_generate (transaction, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, { futurehead::genesis_hash }));
	futurehead::block_hash hash (1);
	auto vote2 (node.store.vote_generate (transaction, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, { hash }));
	node.votes_cache.add (vote1);
	node.votes_cache.add (vote2);
	auto existing2 (node.votes_cache.find (hash));
	ASSERT_EQ (1, existing2.size ());
	ASSERT_EQ (vote2, existing2.front ());
	ASSERT_EQ (0, node.votes_cache.find (futurehead::genesis_hash).size ());
}

TEST (node, vote_republish)
{
	futurehead::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	futurehead::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	futurehead::genesis genesis;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, std::numeric_limits<futurehead::uint128_t>::max () - node1.config.receive_minimum.number (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, std::numeric_limits<futurehead::uint128_t>::max () - node1.config.receive_minimum.number () * 2, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	node1.process_active (send1);
	system.deadline_set (5s);
	while (!node2.block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1.active.publish (send2);
	auto vote (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 0, send2));
	ASSERT_TRUE (node1.active.active (*send1));
	ASSERT_TRUE (node2.active.active (*send1));
	node1.vote_processor.vote (vote, std::make_shared<futurehead::transport::channel_udp> (node1.network.udp_channels, node1.network.endpoint (), node1.network_params.protocol.protocol_version));
	while (!node1.block (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!node2.block (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_FALSE (node1.block (send1->hash ()));
	ASSERT_FALSE (node2.block (send1->hash ()));
	system.deadline_set (5s);
	while (node2.balance (key2.pub) != node1.config.receive_minimum.number () * 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (node1.balance (key2.pub) != node1.config.receive_minimum.number () * 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

namespace futurehead
{
TEST (node, vote_by_hash_bundle)
{
	// Keep max_hashes above system to ensure it is kept in scope as votes can be added during system destruction
	std::atomic<size_t> max_hashes{ 0 };
	futurehead::system system (1);
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key1;
	system.wallet (0)->insert_adhoc (key1.prv);

	system.nodes[0]->observers.vote.add ([&max_hashes](std::shared_ptr<futurehead::vote> vote_a, std::shared_ptr<futurehead::transport::channel>, futurehead::vote_code) {
		if (vote_a->blocks.size () > max_hashes)
		{
			max_hashes = vote_a->blocks.size ();
		}
	});

	for (int i = 1; i <= 200; i++)
	{
		system.nodes[0]->active.generator.add (futurehead::genesis_hash);
	}

	// Verify that bundling occurs. While reaching 12 should be common on most hardware in release mode,
	// we set this low enough to allow the test to pass on CI/with santitizers.
	system.deadline_set (20s);
	while (max_hashes.load () < 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}
}

TEST (node, vote_by_hash_republish)
{
	std::vector<futurehead::transport::transport_type> types{ futurehead::transport::transport_type::tcp, futurehead::transport::transport_type::udp };
	for (auto & type : types)
	{
		futurehead::node_flags node_flags;
		if (type == futurehead::transport::transport_type::udp)
		{
			node_flags.disable_tcp_realtime = true;
			node_flags.disable_bootstrap_listener = true;
			node_flags.disable_udp = false;
		}
		futurehead::system system (2, type, node_flags);
		auto & node1 (*system.nodes[0]);
		auto & node2 (*system.nodes[1]);
		futurehead::keypair key2;
		system.wallet (1)->insert_adhoc (key2.prv);
		futurehead::genesis genesis;
		auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, std::numeric_limits<futurehead::uint128_t>::max () - node1.config.receive_minimum.number (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
		auto send2 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, std::numeric_limits<futurehead::uint128_t>::max () - node1.config.receive_minimum.number () * 2, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
		node1.process_active (send1);
		system.deadline_set (5s);
		while (!node2.block (send1->hash ()))
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		node1.active.publish (send2);
		std::vector<futurehead::block_hash> vote_blocks;
		vote_blocks.push_back (send2->hash ());
		auto vote (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 0, vote_blocks));
		ASSERT_TRUE (node1.active.active (*send1));
		ASSERT_TRUE (node2.active.active (*send1));
		node1.vote_processor.vote (vote, std::make_shared<futurehead::transport::channel_udp> (node1.network.udp_channels, node1.network.endpoint (), node1.network_params.protocol.protocol_version));
		while (!node1.block (send2->hash ()))
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		while (!node2.block (send2->hash ()))
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		ASSERT_FALSE (node1.block (send1->hash ()));
		ASSERT_FALSE (node2.block (send1->hash ()));
		system.deadline_set (5s);
		while (node2.balance (key2.pub) != node1.config.receive_minimum.number () * 2)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		while (node1.balance (key2.pub) != node1.config.receive_minimum.number () * 2)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}
}

TEST (node, vote_by_hash_epoch_block_republish)
{
	futurehead::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	futurehead::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	futurehead::genesis genesis;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, std::numeric_limits<futurehead::uint128_t>::max () - node1.config.receive_minimum.number (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	auto epoch1 (std::make_shared<futurehead::state_block> (futurehead::genesis_account, genesis.hash (), futurehead::genesis_account, futurehead::genesis_amount, node1.ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	node1.process_active (send1);
	system.deadline_set (5s);
	while (!node2.block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1.active.publish (epoch1);
	std::vector<futurehead::block_hash> vote_blocks;
	vote_blocks.push_back (epoch1->hash ());
	auto vote (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 0, vote_blocks));
	ASSERT_TRUE (node1.active.active (*send1));
	ASSERT_TRUE (node2.active.active (*send1));
	node1.vote_processor.vote (vote, std::make_shared<futurehead::transport::channel_udp> (node1.network.udp_channels, node1.network.endpoint (), node1.network_params.protocol.protocol_version));
	while (!node1.block (epoch1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!node2.block (epoch1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_FALSE (node1.block (send1->hash ()));
	ASSERT_FALSE (node2.block (send1->hash ()));
}

TEST (node, epoch_conflict_confirm)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto node0 = system.add_node (node_config);
	node_config.peering_port = futurehead::get_available_port ();
	auto node1 = system.add_node (node_config);
	futurehead::keypair key;
	futurehead::genesis genesis;
	futurehead::keypair epoch_signer (futurehead::test_genesis_key);
	auto send (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 1, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	auto open (std::make_shared<futurehead::state_block> (key.pub, 0, key.pub, 1, send->hash (), key.prv, key.pub, *system.work.generate (key.pub)));
	auto change (std::make_shared<futurehead::state_block> (key.pub, open->hash (), key.pub, 1, 0, key.prv, key.pub, *system.work.generate (open->hash ())));
	auto send2 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 2, open->hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (send->hash ())));
	auto epoch_open (std::make_shared<futurehead::state_block> (change->root (), 0, 0, 0, node0->ledger.epoch_link (futurehead::epoch::epoch_1), epoch_signer.prv, epoch_signer.pub, *system.work.generate (open->hash ())));
	ASSERT_EQ (futurehead::process_result::progress, node1->process (*send).code);
	ASSERT_EQ (futurehead::process_result::progress, node1->process (*send2).code);
	ASSERT_EQ (futurehead::process_result::progress, node1->process (*open).code);
	// Confirm block in node1 to allow generating votes
	node1->block_confirm (open);
	{
		auto election (node1->active.election (open->qualified_root ()));
		ASSERT_NE (nullptr, election);
		futurehead::lock_guard<std::mutex> guard (node1->active.mutex);
		election->confirm_once ();
	}
	ASSERT_TIMELY (3s, node1->block_confirmed (open->hash ()));
	{
		futurehead::block_post_events events;
		auto transaction (node0->store.tx_begin_write ());
		ASSERT_EQ (futurehead::process_result::progress, node0->block_processor.process_one (transaction, events, send).code);
		ASSERT_EQ (futurehead::process_result::progress, node0->block_processor.process_one (transaction, events, send2).code);
		ASSERT_EQ (futurehead::process_result::progress, node0->block_processor.process_one (transaction, events, open).code);
	}
	node0->process_active (change);
	node0->process_active (epoch_open);
	system.deadline_set (5s);
	while (!node0->block (change->hash ()) || !node0->block (epoch_open->hash ()) || !node1->block (change->hash ()) || !node1->block (epoch_open->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	futurehead::blocks_confirm (*node0, { change, epoch_open });
	ASSERT_EQ (2, node0->active.size ());
	{
		futurehead::lock_guard<std::mutex> lock (node0->active.mutex);
		ASSERT_TRUE (node0->active.blocks.find (change->hash ()) != node0->active.blocks.end ());
		ASSERT_TRUE (node0->active.blocks.find (epoch_open->hash ()) != node0->active.blocks.end ());
	}
	system.wallet (1)->insert_adhoc (futurehead::test_genesis_key.prv);
	system.deadline_set (5s);
	while (!node0->active.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	{
		auto transaction (node0->store.tx_begin_read ());
		ASSERT_TRUE (node0->ledger.store.block_exists (transaction, change->hash ()));
		ASSERT_TRUE (node0->ledger.store.block_exists (transaction, epoch_open->hash ()));
	}
}

TEST (node, fork_invalid_block_signature)
{
	futurehead::system system;
	futurehead::node_flags node_flags;
	// Disabling republishing + waiting for a rollback before sending the correct vote below fixes an intermittent failure in this test
	// If these are taken out, one of two things may cause the test two fail often:
	// - Block *send2* might get processed before the rollback happens, simply due to timings, with code "fork", and not be processed again. Waiting for the rollback fixes this issue.
	// - Block *send1* might get processed again after the rollback happens, which causes *send2* to be processed with code "fork". Disabling block republishing ensures "send1" is not processed again.
	// An alternative would be to repeatedly flood the correct vote
	node_flags.disable_block_processor_republishing = true;
	auto & node1 (*system.add_node (node_flags));
	auto & node2 (*system.add_node (node_flags));
	futurehead::keypair key2;
	futurehead::genesis genesis;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, std::numeric_limits<futurehead::uint128_t>::max () - node1.config.receive_minimum.number (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, std::numeric_limits<futurehead::uint128_t>::max () - node1.config.receive_minimum.number () * 2, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	auto send2_corrupt (std::make_shared<futurehead::send_block> (*send2));
	send2_corrupt->signature = futurehead::signature (123);
	auto vote (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 0, send2));
	auto vote_corrupt (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 0, send2_corrupt));

	node1.process_active (send1);
	system.deadline_set (5s);
	while (!node1.block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Send the vote with the corrupt block signature
	node2.network.flood_vote (vote_corrupt, 1.0f);
	// Wait for the rollback
	ASSERT_TIMELY (5s, node1.stats.count (futurehead::stat::type::rollback, futurehead::stat::detail::all));
	// Send the vote with the correct block
	node2.network.flood_vote (vote, 1.0f);
	while (node1.block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!node1.block (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (node1.block (send2->hash ())->block_signature (), send2->block_signature ());
}

TEST (node, fork_election_invalid_block_signature)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::block_builder builder;
	std::shared_ptr<futurehead::block> send1 = builder.state ()
	                                     .account (futurehead::test_genesis_key.pub)
	                                     .previous (genesis.hash ())
	                                     .representative (futurehead::test_genesis_key.pub)
	                                     .balance (futurehead::genesis_amount - futurehead::Gxrb_ratio)
	                                     .link (futurehead::test_genesis_key.pub)
	                                     .work (*system.work.generate (genesis.hash ()))
	                                     .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                     .build ();
	std::shared_ptr<futurehead::block> send2 = builder.state ()
	                                     .account (futurehead::test_genesis_key.pub)
	                                     .previous (genesis.hash ())
	                                     .representative (futurehead::test_genesis_key.pub)
	                                     .balance (futurehead::genesis_amount - 2 * futurehead::Gxrb_ratio)
	                                     .link (futurehead::test_genesis_key.pub)
	                                     .work (*system.work.generate (genesis.hash ()))
	                                     .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                     .build ();
	std::shared_ptr<futurehead::block> send3 = builder.state ()
	                                     .account (futurehead::test_genesis_key.pub)
	                                     .previous (genesis.hash ())
	                                     .representative (futurehead::test_genesis_key.pub)
	                                     .balance (futurehead::genesis_amount - 2 * futurehead::Gxrb_ratio)
	                                     .link (futurehead::test_genesis_key.pub)
	                                     .work (*system.work.generate (genesis.hash ()))
	                                     .sign (futurehead::test_genesis_key.prv, 0) // Invalid signature
	                                     .build ();
	auto channel1 (node1.network.udp_channels.create (node1.network.endpoint ()));
	node1.network.process_message (futurehead::publish (send1), channel1);
	system.deadline_set (5s);
	std::shared_ptr<futurehead::election> election;
	while (election == nullptr)
	{
		ASSERT_NO_ERROR (system.poll ());
		futurehead::lock_guard<std::mutex> lock (node1.active.mutex);
		auto existing = node1.active.blocks.find (send1->hash ());
		if (existing != node1.active.blocks.end ())
		{
			election = existing->second;
		}
	}
	futurehead::unique_lock<std::mutex> lock (node1.active.mutex);
	ASSERT_EQ (1, election->blocks.size ());
	lock.unlock ();
	node1.network.process_message (futurehead::publish (send3), channel1);
	node1.network.process_message (futurehead::publish (send2), channel1);
	lock.lock ();
	while (election->blocks.size () == 1)
	{
		lock.unlock ();
		ASSERT_NO_ERROR (system.poll ());
		lock.lock ();
	}
	ASSERT_EQ (election->blocks[send2->hash ()]->block_signature (), send2->block_signature ());
}

TEST (node, block_processor_signatures)
{
	futurehead::system system0 (1);
	auto & node1 (*system0.nodes[0]);
	system0.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::block_hash latest (system0.nodes[0]->latest (futurehead::test_genesis_key.pub));
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, latest, futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, key1.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	futurehead::keypair key2;
	auto send2 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send1->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 2 * futurehead::Gxrb_ratio, key2.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	futurehead::keypair key3;
	auto send3 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send2->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 3 * futurehead::Gxrb_ratio, key3.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send3);
	// Invalid signature bit
	auto send4 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send3->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 4 * futurehead::Gxrb_ratio, key3.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send4);
	send4->signature.bytes[32] ^= 0x1;
	// Invalid signature bit (force)
	auto send5 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send3->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 5 * futurehead::Gxrb_ratio, key3.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send5);
	send5->signature.bytes[31] ^= 0x1;
	// Invalid signature to unchecked
	{
		auto transaction (node1.store.tx_begin_write ());
		node1.store.unchecked_put (transaction, send5->previous (), send5);
		++node1.ledger.cache.unchecked_count;
	}
	auto receive1 (std::make_shared<futurehead::state_block> (key1.pub, 0, futurehead::test_genesis_key.pub, futurehead::Gxrb_ratio, send1->hash (), key1.prv, key1.pub, 0));
	node1.work_generate_blocking (*receive1);
	auto receive2 (std::make_shared<futurehead::state_block> (key2.pub, 0, futurehead::test_genesis_key.pub, futurehead::Gxrb_ratio, send2->hash (), key2.prv, key2.pub, 0));
	node1.work_generate_blocking (*receive2);
	// Invalid private key
	auto receive3 (std::make_shared<futurehead::state_block> (key3.pub, 0, futurehead::test_genesis_key.pub, futurehead::Gxrb_ratio, send3->hash (), key2.prv, key3.pub, 0));
	node1.work_generate_blocking (*receive3);
	node1.process_active (send1);
	node1.process_active (send2);
	node1.process_active (send3);
	node1.process_active (send4);
	node1.process_active (receive1);
	node1.process_active (receive2);
	node1.process_active (receive3);
	node1.block_processor.flush ();
	node1.block_processor.force (send5);
	node1.block_processor.flush ();
	auto transaction (node1.store.tx_begin_read ());
	ASSERT_TRUE (node1.store.block_exists (transaction, send1->hash ()));
	ASSERT_TRUE (node1.store.block_exists (transaction, send2->hash ()));
	ASSERT_TRUE (node1.store.block_exists (transaction, send3->hash ()));
	ASSERT_FALSE (node1.store.block_exists (transaction, send4->hash ()));
	ASSERT_FALSE (node1.store.block_exists (transaction, send5->hash ()));
	ASSERT_TRUE (node1.store.block_exists (transaction, receive1->hash ()));
	ASSERT_TRUE (node1.store.block_exists (transaction, receive2->hash ()));
	ASSERT_FALSE (node1.store.block_exists (transaction, receive3->hash ()));
}

/*
 *  State blocks go through a different signature path, ensure invalidly signed state blocks are rejected
 *  This test can freeze if the wake conditions in block_processor::flush are off, for that reason this is done async here
 */
TEST (node, block_processor_reject_state)
{
	futurehead::system system (1);
	auto & node (*system.nodes[0]);
	futurehead::genesis genesis;
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node.work_generate_blocking (*send1);
	send1->signature.bytes[0] ^= 1;
	ASSERT_FALSE (node.ledger.block_exists (send1->hash ()));
	node.process_active (send1);
	auto flushed = std::async (std::launch::async, [&node] { node.block_processor.flush (); });
	ASSERT_NE (std::future_status::timeout, flushed.wait_for (5s));
	ASSERT_FALSE (node.ledger.block_exists (send1->hash ()));
	auto send2 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 2 * futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node.work_generate_blocking (*send2);
	node.process_active (send2);
	auto flushed2 = std::async (std::launch::async, [&node] { node.block_processor.flush (); });
	ASSERT_NE (std::future_status::timeout, flushed2.wait_for (5s));
	ASSERT_TRUE (node.ledger.block_exists (send2->hash ()));
}

TEST (node, block_processor_full)
{
	futurehead::system system;
	futurehead::node_flags node_flags;
	node_flags.block_processor_full_size = 3;
	auto & node = *system.add_node (futurehead::node_config (futurehead::get_available_port (), system.logging), node_flags);
	futurehead::genesis genesis;
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node.work_generate_blocking (*send1);
	auto send2 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send1->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 2 * futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node.work_generate_blocking (*send2);
	auto send3 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send2->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 3 * futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node.work_generate_blocking (*send3);
	// The write guard prevents block processor doing any writes
	auto write_guard = node.write_database_queue.wait (futurehead::writer::testing);
	node.block_processor.add (send1);
	ASSERT_FALSE (node.block_processor.full ());
	node.block_processor.add (send2);
	ASSERT_FALSE (node.block_processor.full ());
	node.block_processor.add (send3);
	// Block processor may be not full during state blocks signatures verification
	system.deadline_set (2s);
	while (!node.block_processor.full ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, block_processor_half_full)
{
	futurehead::system system;
	futurehead::node_flags node_flags;
	node_flags.block_processor_full_size = 6;
	auto & node = *system.add_node (futurehead::node_config (futurehead::get_available_port (), system.logging), node_flags);
	futurehead::genesis genesis;
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node.work_generate_blocking (*send1);
	auto send2 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send1->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 2 * futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node.work_generate_blocking (*send2);
	auto send3 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send2->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 3 * futurehead::Gxrb_ratio, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node.work_generate_blocking (*send3);
	// The write guard prevents block processor doing any writes
	auto write_guard = node.write_database_queue.wait (futurehead::writer::testing);
	node.block_processor.add (send1);
	ASSERT_FALSE (node.block_processor.half_full ());
	node.block_processor.add (send2);
	ASSERT_FALSE (node.block_processor.half_full ());
	node.block_processor.add (send3);
	// Block processor may be not half_full during state blocks signatures verification
	system.deadline_set (2s);
	while (!node.block_processor.half_full ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_FALSE (node.block_processor.full ());
}

TEST (node, confirm_back)
{
	futurehead::system system (1);
	futurehead::keypair key;
	auto & node (*system.nodes[0]);
	futurehead::genesis genesis;
	auto genesis_start_balance (node.balance (futurehead::test_genesis_key.pub));
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key.pub, genesis_start_balance - 1, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	auto open (std::make_shared<futurehead::state_block> (key.pub, 0, key.pub, 1, send1->hash (), key.prv, key.pub, *system.work.generate (key.pub)));
	auto send2 (std::make_shared<futurehead::state_block> (key.pub, open->hash (), key.pub, 0, futurehead::test_genesis_key.pub, key.prv, key.pub, *system.work.generate (open->hash ())));
	node.process_active (send1);
	node.process_active (open);
	node.process_active (send2);
	futurehead::blocks_confirm (node, { send1, open, send2 });
	ASSERT_EQ (3, node.active.size ());
	std::vector<futurehead::block_hash> vote_blocks;
	vote_blocks.push_back (send2->hash ());
	auto vote (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 0, vote_blocks));
	node.vote_processor.vote_blocking (vote, std::make_shared<futurehead::transport::channel_udp> (node.network.udp_channels, node.network.endpoint (), node.network_params.protocol.protocol_version));
	system.deadline_set (10s);
	while (!node.active.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, peers)
{
	futurehead::system system (1);
	auto node1 (system.nodes[0]);
	ASSERT_TRUE (node1->network.empty ());

	auto node2 (std::make_shared<futurehead::node> (system.io_ctx, futurehead::get_available_port (), futurehead::unique_path (), system.alarm, system.logging, system.work));
	system.nodes.push_back (node2);

	auto endpoint = node1->network.endpoint ();
	futurehead::endpoint_key endpoint_key{ endpoint.address ().to_v6 ().to_bytes (), endpoint.port () };
	auto & store = node2->store;
	{
		// Add a peer to the database
		auto transaction (store.tx_begin_write ());
		store.peer_put (transaction, endpoint_key);

		// Add a peer which is not contactable
		store.peer_put (transaction, futurehead::endpoint_key{ boost::asio::ip::address_v6::any ().to_bytes (), 55555 });
	}

	node2->start ();
	system.deadline_set (10s);
	while (node2->network.empty () || node1->network.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Wait to finish TCP node ID handshakes
	system.deadline_set (10s);
	while (node1->bootstrap.realtime_count == 0 || node2->bootstrap.realtime_count == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Confirm that the peers match with the endpoints we are expecting
	ASSERT_EQ (1, node1->network.size ());
	auto list1 (node1->network.list (2));
	ASSERT_EQ (node2->network.endpoint (), list1[0]->get_endpoint ());
	ASSERT_EQ (futurehead::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_EQ (1, node2->network.size ());
	auto list2 (node2->network.list (2));
	ASSERT_EQ (node1->network.endpoint (), list2[0]->get_endpoint ());
	ASSERT_EQ (futurehead::transport::transport_type::tcp, list2[0]->get_type ());
	// Stop the peer node and check that it is removed from the store
	node1->stop ();

	system.deadline_set (10s);
	while (node2->network.size () == 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	ASSERT_TRUE (node2->network.empty ());

	// Uncontactable peer should not be stored
	auto transaction (store.tx_begin_read ());
	ASSERT_EQ (store.peer_count (transaction), 1);
	ASSERT_TRUE (store.peer_exists (transaction, endpoint_key));

	node2->stop ();
}

TEST (node, peer_cache_restart)
{
	futurehead::system system (1);
	auto node1 (system.nodes[0]);
	ASSERT_TRUE (node1->network.empty ());
	auto endpoint = node1->network.endpoint ();
	futurehead::endpoint_key endpoint_key{ endpoint.address ().to_v6 ().to_bytes (), endpoint.port () };
	auto path (futurehead::unique_path ());
	{
		auto node2 (std::make_shared<futurehead::node> (system.io_ctx, futurehead::get_available_port (), path, system.alarm, system.logging, system.work));
		system.nodes.push_back (node2);
		auto & store = node2->store;
		{
			// Add a peer to the database
			auto transaction (store.tx_begin_write ());
			store.peer_put (transaction, endpoint_key);
		}
		node2->start ();
		system.deadline_set (10s);
		while (node2->network.empty ())
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		// Confirm that the peers match with the endpoints we are expecting
		auto list (node2->network.list (2));
		ASSERT_EQ (node1->network.endpoint (), list[0]->get_endpoint ());
		ASSERT_EQ (1, node2->network.size ());
		node2->stop ();
	}
	// Restart node
	{
		futurehead::node_flags node_flags;
		node_flags.read_only = true;
		auto node3 (std::make_shared<futurehead::node> (system.io_ctx, futurehead::get_available_port (), path, system.alarm, system.logging, system.work, node_flags));
		system.nodes.push_back (node3);
		// Check cached peers after restart
		node3->network.start ();
		node3->add_initial_peers ();

		auto & store = node3->store;
		{
			auto transaction (store.tx_begin_read ());
			ASSERT_EQ (store.peer_count (transaction), 1);
			ASSERT_TRUE (store.peer_exists (transaction, endpoint_key));
		}
		system.deadline_set (10s);
		while (node3->network.empty ())
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		// Confirm that the peers match with the endpoints we are expecting
		auto list (node3->network.list (2));
		ASSERT_EQ (node1->network.endpoint (), list[0]->get_endpoint ());
		ASSERT_EQ (1, node3->network.size ());
		node3->stop ();
	}
}

TEST (node, unchecked_cleanup)
{
	futurehead::system system (1);
	futurehead::keypair key;
	auto & node (*system.nodes[0]);
	auto open (std::make_shared<futurehead::state_block> (key.pub, 0, key.pub, 1, key.pub, key.prv, key.pub, *system.work.generate (key.pub)));
	std::vector<uint8_t> bytes;
	{
		futurehead::vectorstream stream (bytes);
		open->serialize (stream);
	}
	// Add to the blocks filter
	// Should be cleared after unchecked cleanup
	ASSERT_FALSE (node.network.publish_filter.apply (bytes.data (), bytes.size ()));
	node.process_active (open);
	node.block_processor.flush ();
	node.config.unchecked_cutoff_time = std::chrono::seconds (2);
	{
		auto transaction (node.store.tx_begin_read ());
		auto unchecked_count (node.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		ASSERT_EQ (unchecked_count, node.ledger.cache.unchecked_count);
	}
	std::this_thread::sleep_for (std::chrono::seconds (1));
	node.unchecked_cleanup ();
	ASSERT_TRUE (node.network.publish_filter.apply (bytes.data (), bytes.size ()));
	{
		auto transaction (node.store.tx_begin_read ());
		auto unchecked_count (node.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		ASSERT_EQ (unchecked_count, node.ledger.cache.unchecked_count);
	}
	std::this_thread::sleep_for (std::chrono::seconds (2));
	node.unchecked_cleanup ();
	ASSERT_FALSE (node.network.publish_filter.apply (bytes.data (), bytes.size ()));
	{
		auto transaction (node.store.tx_begin_read ());
		auto unchecked_count (node.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
		ASSERT_EQ (unchecked_count, node.ledger.cache.unchecked_count);
	}
}

/** This checks that a node can be opened (without being blocked) when a write lock is held elsewhere */
TEST (node, dont_write_lock_node)
{
	auto path = futurehead::unique_path ();

	std::promise<void> write_lock_held_promise;
	std::promise<void> finished_promise;
	std::thread ([&path, &write_lock_held_promise, &finished_promise]() {
		futurehead::logger_mt logger;
		auto store = futurehead::make_store (logger, path, false, true);
		{
			futurehead::genesis genesis;
			futurehead::ledger_cache ledger_cache;
			auto transaction (store->tx_begin_write ());
			store->initialize (transaction, genesis, ledger_cache);
		}

		// Hold write lock open until main thread is done needing it
		auto transaction (store->tx_begin_write ());
		write_lock_held_promise.set_value ();
		finished_promise.get_future ().wait ();
	})
	.detach ();

	write_lock_held_promise.get_future ().wait ();

	// Check inactive node can finish executing while a write lock is open
	futurehead::inactive_node node (path);
	finished_promise.set_value ();
}

TEST (node, bidirectional_tcp)
{
	futurehead::system system;
	futurehead::node_flags node_flags;
	// Disable bootstrap to start elections for new blocks
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_wallet_bootstrap = true;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto node1 = system.add_node (node_config, node_flags);
	node_config.peering_port = futurehead::get_available_port ();
	node_config.tcp_incoming_connections_max = 0; // Disable incoming TCP connections for node 2
	auto node2 = system.add_node (node_config, node_flags);
	// Check network connections
	ASSERT_EQ (1, node1->network.size ());
	ASSERT_EQ (1, node2->network.size ());
	auto list1 (node1->network.list (1));
	ASSERT_EQ (futurehead::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_NE (node2->network.endpoint (), list1[0]->get_endpoint ()); // Ephemeral port
	ASSERT_EQ (node2->node_id.pub, list1[0]->get_node_id ());
	auto list2 (node2->network.list (1));
	ASSERT_EQ (futurehead::transport::transport_type::tcp, list2[0]->get_type ());
	ASSERT_EQ (node1->network.endpoint (), list2[0]->get_endpoint ());
	ASSERT_EQ (node1->node_id.pub, list2[0]->get_node_id ());
	// Test block propagation from node 1
	futurehead::genesis genesis;
	futurehead::keypair key;
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node1->work_generate_blocking (genesis.hash ())));
	node1->process_active (send1);
	node1->block_processor.flush ();
	system.deadline_set (5s);
	while (!node1->ledger.block_exists (send1->hash ()) || !node2->ledger.block_exists (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Test block confirmation from node 1 (add representative to node 1)
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	// Wait to find new reresentative
	system.deadline_set (10s);
	while (node2->rep_crawler.representative_count () == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	/* Wait for confirmation
	To check connection we need only node 2 confirmation status
	Node 1 election can be unconfirmed because representative private key was inserted after election start (and node 2 isn't flooding new votes to principal representatives) */
	bool confirmed (false);
	system.deadline_set (10s);
	while (!confirmed)
	{
		auto transaction2 (node2->store.tx_begin_read ());
		confirmed = node2->ledger.block_confirmed (transaction2, send1->hash ());
		ASSERT_NO_ERROR (system.poll ());
	}
	// Test block propagation & confirmation from node 2 (remove representative from node 1)
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_write ());
		system.wallet (0)->store.erase (transaction, futurehead::test_genesis_key.pub);
	}
	/* Test block propagation from node 2
	Node 2 has only ephemeral TCP port open. Node 1 cannot establish connection to node 2 listening port */
	auto send2 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, send1->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 2 * futurehead::Gxrb_ratio, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *node1->work_generate_blocking (send1->hash ())));
	node2->process_active (send2);
	node2->block_processor.flush ();
	system.deadline_set (5s);
	while (!node1->ledger.block_exists (send2->hash ()) || !node2->ledger.block_exists (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Test block confirmation from node 2 (add representative to node 2)
	system.wallet (1)->insert_adhoc (futurehead::test_genesis_key.prv);
	// Wait to find changed reresentative
	system.deadline_set (10s);
	while (node1->rep_crawler.representative_count () == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	/* Wait for confirmation
	To check connection we need only node 1 confirmation status
	Node 2 election can be unconfirmed because representative private key was inserted after election start (and node 1 isn't flooding new votes to principal representatives) */
	confirmed = false;
	system.deadline_set (20s);
	while (!confirmed)
	{
		auto transaction1 (node1->store.tx_begin_read ());
		confirmed = node1->ledger.block_confirmed (transaction1, send2->hash ());
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Tests that local blocks are flooded to all principal representatives
// Sanitizers or running within valgrind use different timings and number of nodes
TEST (node, aggressive_flooding)
{
	futurehead::system system;
	futurehead::node_flags node_flags;
	node_flags.disable_request_loop = true;
	node_flags.disable_block_processor_republishing = true;
	node_flags.disable_bootstrap_bulk_push_client = true;
	node_flags.disable_bootstrap_bulk_pull_server = true;
	node_flags.disable_bootstrap_listener = true;
	node_flags.disable_lazy_bootstrap = true;
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_wallet_bootstrap = true;
	auto & node1 (*system.add_node (node_flags));
	auto & wallet1 (*system.wallet (0));
	wallet1.insert_adhoc (futurehead::test_genesis_key.prv);
	std::vector<std::pair<std::shared_ptr<futurehead::node>, std::shared_ptr<futurehead::wallet>>> nodes_wallets;
	bool const sanitizer_or_valgrind (is_sanitizer_build || futurehead::running_within_valgrind ());
	nodes_wallets.resize (!sanitizer_or_valgrind ? 5 : 3);

	std::generate (nodes_wallets.begin (), nodes_wallets.end (), [&system, node_flags]() {
		futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
		auto node (system.add_node (node_config, node_flags));
		return std::make_pair (node, system.wallet (system.nodes.size () - 1));
	});

	// This test is only valid if a non-aggressive flood would not reach every peer
	ASSERT_TIMELY (5s, node1.network.size () == nodes_wallets.size ());
	ASSERT_LT (node1.network.fanout (), nodes_wallets.size ());

	// Send a large amount to create a principal representative in each node
	auto large_amount = (futurehead::genesis_amount / 2) / nodes_wallets.size ();
	std::vector<std::shared_ptr<futurehead::block>> genesis_blocks;
	for (auto & node_wallet : nodes_wallets)
	{
		futurehead::keypair keypair;
		node_wallet.second->store.representative_set (node_wallet.first->wallets.tx_begin_write (), keypair.pub);
		node_wallet.second->insert_adhoc (keypair.prv);
		auto block (wallet1.send_action (futurehead::test_genesis_key.pub, keypair.pub, large_amount));
		genesis_blocks.push_back (block);
	}

	// Ensure all nodes have the full genesis chain
	for (auto & node_wallet : nodes_wallets)
	{
		for (auto const & block : genesis_blocks)
		{
			node_wallet.first->process (*block);
		}
		ASSERT_EQ (node1.latest (futurehead::test_genesis_key.pub), node_wallet.first->latest (futurehead::test_genesis_key.pub));
	}

	// Wait until the main node sees all representatives
	ASSERT_TIMELY (!sanitizer_or_valgrind ? 10s : 40s, node1.rep_crawler.principal_representatives ().size () == nodes_wallets.size ());

	// Generate blocks and ensure they are sent to all representatives
	futurehead::block_builder builder;
	std::shared_ptr<futurehead::state_block> block{};
	{
		auto transaction (node1.store.tx_begin_read ());
		block = builder.state ()
		        .account (futurehead::test_genesis_key.pub)
		        .representative (futurehead::test_genesis_key.pub)
		        .previous (node1.ledger.latest (transaction, futurehead::test_genesis_key.pub))
		        .balance (node1.ledger.account_balance (transaction, futurehead::test_genesis_key.pub) - 1)
		        .link (futurehead::test_genesis_key.pub)
		        .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
		        .work (*node1.work_generate_blocking (node1.ledger.latest (transaction, futurehead::test_genesis_key.pub)))
		        .build ();
	}
	// Processing locally goes through the aggressive block flooding path
	node1.process_local (block, false);

	auto all_have_block = [&nodes_wallets](futurehead::block_hash const & hash_a) {
		return std::all_of (nodes_wallets.begin (), nodes_wallets.end (), [hash = hash_a](auto const & node_wallet) {
			return node_wallet.first->block (hash) != nullptr;
		});
	};

	ASSERT_TIMELY (!sanitizer_or_valgrind ? 5s : 25s, all_have_block (block->hash ()));

	// Do the same for a wallet block
	auto wallet_block = wallet1.send_sync (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.pub, 10);
	ASSERT_TIMELY (!sanitizer_or_valgrind ? 5s : 25s, all_have_block (wallet_block));

	// All blocks: genesis + (send+open) for each representative + 2 local blocks
	// The main node only sees all blocks if other nodes are flooding their PR's open block to all other PRs
	ASSERT_EQ (1 + 2 * nodes_wallets.size () + 2, node1.ledger.cache.block_count);
}

// Tests that upon changing the default difficulty, max generation difficulty changes proportionally
TEST (node, max_work_generate_difficulty)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.max_work_generate_multiplier = 2.0;
	auto & node = *system.add_node (node_config);
	auto initial_difficulty = node.default_difficulty (futurehead::work_version::work_1);
	ASSERT_EQ (node.max_work_generate_difficulty (futurehead::work_version::work_1), futurehead::difficulty::from_multiplier (node.config.max_work_generate_multiplier, initial_difficulty));
	ASSERT_NE (nullptr, system.upgrade_genesis_epoch (node, futurehead::epoch::epoch_1));
	ASSERT_NE (nullptr, system.upgrade_genesis_epoch (node, futurehead::epoch::epoch_2));
	auto final_difficulty = node.default_difficulty (futurehead::work_version::work_1);
	ASSERT_NE (final_difficulty, initial_difficulty);
	ASSERT_EQ (node.max_work_generate_difficulty (futurehead::work_version::work_1), futurehead::difficulty::from_multiplier (node.config.max_work_generate_multiplier, final_difficulty));
}

TEST (active_difficulty, recalculate_work)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.enable_voting = false;
	auto & node1 = *system.add_node (node_config);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	ASSERT_EQ (node1.network_params.network.publish_thresholds.epoch_1, node1.active.active_difficulty ());
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto multiplier1 = futurehead::difficulty::to_multiplier (send1->difficulty (), node1.network_params.network.publish_thresholds.epoch_1);
	// Process as local block
	node1.process_active (send1);
	system.deadline_set (2s);
	while (node1.active.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto sum (std::accumulate (node1.active.multipliers_cb.begin (), node1.active.multipliers_cb.end (), double(0)));
	ASSERT_EQ (node1.active.active_difficulty (), futurehead::difficulty::from_multiplier (sum / node1.active.multipliers_cb.size (), node1.network_params.network.publish_thresholds.epoch_1));
	futurehead::unique_lock<std::mutex> lock (node1.active.mutex);
	// Fake history records to force work recalculation
	for (auto i (0); i < node1.active.multipliers_cb.size (); i++)
	{
		node1.active.multipliers_cb.push_back (multiplier1 * (1 + i / 100.));
	}
	node1.work_generate_blocking (*send1);
	node1.process_active (send1);
	node1.active.update_active_multiplier (lock);
	sum = std::accumulate (node1.active.multipliers_cb.begin (), node1.active.multipliers_cb.end (), double(0));
	ASSERT_EQ (node1.active.trended_active_multiplier, sum / node1.active.multipliers_cb.size ());
	lock.unlock ();
}

TEST (node, node_sequence)
{
	futurehead::system system (3);
	ASSERT_EQ (0, system.nodes[0]->node_seq);
	ASSERT_EQ (0, system.nodes[0]->node_seq);
	ASSERT_EQ (1, system.nodes[1]->node_seq);
	ASSERT_EQ (2, system.nodes[2]->node_seq);
}

TEST (node, rollback_vote_self)
{
	futurehead::system system;
	futurehead::node_flags flags;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);
	futurehead::state_block_builder builder;
	futurehead::keypair key;
	auto weight = node.config.online_weight_minimum.number ();
	std::shared_ptr<futurehead::state_block> send1 = builder.make_block ()
	                                           .account (futurehead::test_genesis_key.pub)
	                                           .previous (futurehead::genesis_hash)
	                                           .representative (futurehead::test_genesis_key.pub)
	                                           .link (key.pub)
	                                           .balance (futurehead::genesis_amount - weight)
	                                           .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                           .work (*system.work.generate (futurehead::genesis_hash))
	                                           .build ();
	std::shared_ptr<futurehead::state_block> open = builder.make_block ()
	                                          .account (key.pub)
	                                          .previous (0)
	                                          .representative (key.pub)
	                                          .link (send1->hash ())
	                                          .balance (weight)
	                                          .sign (key.prv, key.pub)
	                                          .work (*system.work.generate (key.pub))
	                                          .build ();
	std::shared_ptr<futurehead::state_block> send2 = builder.make_block ()
	                                           .from (*send1)
	                                           .previous (send1->hash ())
	                                           .balance (send1->balance ().number () - 1)
	                                           .link (futurehead::test_genesis_key.pub)
	                                           .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                           .work (*system.work.generate (send1->hash ()))
	                                           .build ();
	std::shared_ptr<futurehead::state_block> fork = builder.make_block ()
	                                          .from (*send2)
	                                          .balance (send2->balance ().number () - 2)
	                                          .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                          .build ();
	ASSERT_EQ (futurehead::process_result::progress, node.process (*send1).code);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*open).code);
	// Confirm blocks to allow voting
	node.block_confirm (open);
	{
		auto election = node.active.election (open->qualified_root ());
		ASSERT_NE (nullptr, election);
		futurehead::lock_guard<std::mutex> guard (node.active.mutex);
		election->confirm_once ();
	}
	ASSERT_TIMELY (5s, node.ledger.cache.cemented_count == 3);
	ASSERT_EQ (weight, node.weight (key.pub));
	node.process_active (send2);
	node.process_active (fork);
	node.block_processor.flush ();
	auto election = node.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (2, election->blocks.size ());
	// Insert genesis key in the wallet
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	{
		// The write guard prevents the block processor from performing the rollback
		auto write_guard = node.write_database_queue.wait (futurehead::writer::testing);
		{
			futurehead::lock_guard<std::mutex> guard (node.active.mutex);
			ASSERT_EQ (1, election->last_votes.size ());
			// Vote with key to switch the winner
			election->vote (key.pub, 0, fork->hash ());
			ASSERT_EQ (2, election->last_votes.size ());
			// The winner changed
			ASSERT_EQ (election->status.winner, fork);
		}
		// Even without the rollback being finished, the aggregator must reply with a vote for the new winner, not the old one
		ASSERT_TRUE (node.votes_cache.find (send2->hash ()).empty ());
		ASSERT_TRUE (node.votes_cache.find (fork->hash ()).empty ());
		auto & node2 = *system.add_node ();
		auto channel (node.network.udp_channels.create (node2.network.endpoint ()));
		node.aggregator.add (channel, { { send2->hash (), send2->root () } });
		ASSERT_TIMELY (5s, !node.votes_cache.find (fork->hash ()).empty ());
		ASSERT_TRUE (node.votes_cache.find (send2->hash ()).empty ());

		// Going out of the scope allows the rollback to complete
	}
	// A vote is eventually generated from the local representative
	ASSERT_TIMELY (5s, 3 == election->last_votes_size ());
	auto vote (election->last_votes.find (futurehead::test_genesis_key.pub));
	ASSERT_NE (election->last_votes.end (), vote);
	ASSERT_EQ (fork->hash (), vote->second.hash);
}

// Confirm a complex dependency graph starting from the first block
TEST (node, dependency_graph)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (config);

	futurehead::state_block_builder builder;
	futurehead::keypair key1, key2, key3;

	// Send to key1
	std::shared_ptr<futurehead::state_block> gen_send1 = builder.make_block ()
	                                               .account (futurehead::test_genesis_key.pub)
	                                               .previous (futurehead::genesis_hash)
	                                               .representative (futurehead::test_genesis_key.pub)
	                                               .link (key1.pub)
	                                               .balance (futurehead::genesis_amount - 1)
	                                               .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                               .work (*system.work.generate (futurehead::genesis_hash))
	                                               .build ();
	// Receive from genesis
	auto key1_open = builder.make_block ()
	                 .account (key1.pub)
	                 .previous (0)
	                 .representative (key1.pub)
	                 .link (gen_send1->hash ())
	                 .balance (1)
	                 .sign (key1.prv, key1.pub)
	                 .work (*system.work.generate (key1.pub))
	                 .build ();
	// Send to genesis
	auto key1_send1 = builder.make_block ()
	                  .account (key1.pub)
	                  .previous (key1_open->hash ())
	                  .representative (key1.pub)
	                  .link (futurehead::test_genesis_key.pub)
	                  .balance (0)
	                  .sign (key1.prv, key1.pub)
	                  .work (*system.work.generate (key1_open->hash ()))
	                  .build ();
	// Receive from key1
	auto gen_receive = builder.make_block ()
	                   .from (*gen_send1)
	                   .previous (gen_send1->hash ())
	                   .link (key1_send1->hash ())
	                   .balance (futurehead::genesis_amount)
	                   .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                   .work (*system.work.generate (gen_send1->hash ()))
	                   .build ();
	// Send to key2
	auto gen_send2 = builder.make_block ()
	                 .from (*gen_receive)
	                 .previous (gen_receive->hash ())
	                 .link (key2.pub)
	                 .balance (gen_receive->balance ().number () - 2)
	                 .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                 .work (*system.work.generate (gen_receive->hash ()))
	                 .build ();
	// Receive from genesis
	auto key2_open = builder.make_block ()
	                 .account (key2.pub)
	                 .previous (0)
	                 .representative (key2.pub)
	                 .link (gen_send2->hash ())
	                 .balance (2)
	                 .sign (key2.prv, key2.pub)
	                 .work (*system.work.generate (key2.pub))
	                 .build ();
	// Send to key3
	auto key2_send1 = builder.make_block ()
	                  .account (key2.pub)
	                  .previous (key2_open->hash ())
	                  .representative (key2.pub)
	                  .link (key3.pub)
	                  .balance (1)
	                  .sign (key2.prv, key2.pub)
	                  .work (*system.work.generate (key2_open->hash ()))
	                  .build ();
	// Receive from key2
	auto key3_open = builder.make_block ()
	                 .account (key3.pub)
	                 .previous (0)
	                 .representative (key3.pub)
	                 .link (key2_send1->hash ())
	                 .balance (1)
	                 .sign (key3.prv, key3.pub)
	                 .work (*system.work.generate (key3.pub))
	                 .build ();
	// Send to key1
	auto key2_send2 = builder.make_block ()
	                  .from (*key2_send1)
	                  .previous (key2_send1->hash ())
	                  .link (key1.pub)
	                  .balance (key2_send1->balance ().number () - 1)
	                  .sign (key2.prv, key2.pub)
	                  .work (*system.work.generate (key2_send1->hash ()))
	                  .build ();
	// Receive from key2
	auto key1_receive = builder.make_block ()
	                    .from (*key1_send1)
	                    .previous (key1_send1->hash ())
	                    .link (key2_send2->hash ())
	                    .balance (key1_send1->balance ().number () + 1)
	                    .sign (key1.prv, key1.pub)
	                    .work (*system.work.generate (key1_send1->hash ()))
	                    .build ();
	// Send to key3
	auto key1_send2 = builder.make_block ()
	                  .from (*key1_receive)
	                  .previous (key1_receive->hash ())
	                  .link (key3.pub)
	                  .balance (key1_receive->balance ().number () - 1)
	                  .sign (key1.prv, key1.pub)
	                  .work (*system.work.generate (key1_receive->hash ()))
	                  .build ();
	// Receive from key1
	auto key3_receive = builder.make_block ()
	                    .from (*key3_open)
	                    .previous (key3_open->hash ())
	                    .link (key1_send2->hash ())
	                    .balance (key3_open->balance ().number () + 1)
	                    .sign (key3.prv, key3.pub)
	                    .work (*system.work.generate (key3_open->hash ()))
	                    .build ();
	// Upgrade key3
	auto key3_epoch = builder.make_block ()
	                  .from (*key3_receive)
	                  .previous (key3_receive->hash ())
	                  .link (node.ledger.epoch_link (futurehead::epoch::epoch_1))
	                  .balance (key3_receive->balance ())
	                  .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                  .work (*system.work.generate (key3_receive->hash ()))
	                  .build ();

	ASSERT_EQ (futurehead::process_result::progress, node.process (*gen_send1).code);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*key1_open).code);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*key1_send1).code);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*gen_receive).code);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*gen_send2).code);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*key2_open).code);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*key2_send1).code);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*key3_open).code);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*key2_send2).code);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*key1_receive).code);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*key1_send2).code);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*key3_receive).code);
	ASSERT_EQ (futurehead::process_result::progress, node.process (*key3_epoch).code);
	ASSERT_TRUE (node.active.empty ());

	// Hash -> Ancestors
	std::unordered_map<futurehead::block_hash, std::vector<futurehead::block_hash>> dependency_graph{
		{ key1_open->hash (), { gen_send1->hash () } },
		{ key1_send1->hash (), { key1_open->hash () } },
		{ gen_receive->hash (), { gen_send1->hash (), key1_open->hash () } },
		{ gen_send2->hash (), { gen_receive->hash () } },
		{ key2_open->hash (), { gen_send2->hash () } },
		{ key2_send1->hash (), { key2_open->hash () } },
		{ key3_open->hash (), { key2_send1->hash () } },
		{ key2_send2->hash (), { key2_send1->hash () } },
		{ key1_receive->hash (), { key1_send1->hash (), key2_send2->hash () } },
		{ key1_send2->hash (), { key1_send1->hash () } },
		{ key3_receive->hash (), { key3_open->hash (), key1_send2->hash () } },
		{ key3_epoch->hash (), { key3_receive->hash () } },
	};
	ASSERT_EQ (node.ledger.cache.block_count - 2, dependency_graph.size ());

	// Start an election for the first block of the dependency graph, and ensure all blocks are eventually confirmed
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	node.block_confirm (gen_send1);

	ASSERT_NO_ERROR (system.poll_until_true (15s, [&] {
		// Not many blocks should be active simultaneously
		EXPECT_LT (node.active.size (), 6);
		futurehead::lock_guard<std::mutex> guard (node.active.mutex);

		// Ensure that active blocks have their ancestors confirmed
		auto error = std::any_of (dependency_graph.cbegin (), dependency_graph.cend (), [&](auto entry) {
			if (node.active.blocks.count (entry.first))
			{
				for (auto ancestor : entry.second)
				{
					if (!node.block_confirmed (ancestor))
					{
						return true;
					}
				}
			}
			return false;
		});

		EXPECT_FALSE (error);
		return error || node.ledger.cache.cemented_count == node.ledger.cache.block_count;
	}));
	ASSERT_EQ (node.ledger.cache.cemented_count, node.ledger.cache.block_count);
	ASSERT_TIMELY (5s, node.active.empty ());
}

// Confirm a complex dependency graph starting from a frontier
TEST (node, dependency_graph_frontier)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (config);
	config.peering_port = futurehead::get_available_port ();
	auto & node2 = *system.add_node (config);

	futurehead::state_block_builder builder;
	futurehead::keypair key1, key2, key3;

	// Send to key1
	std::shared_ptr<futurehead::state_block> gen_send1 = builder.make_block ()
	                                               .account (futurehead::test_genesis_key.pub)
	                                               .previous (futurehead::genesis_hash)
	                                               .representative (futurehead::test_genesis_key.pub)
	                                               .link (key1.pub)
	                                               .balance (futurehead::genesis_amount - 1)
	                                               .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                               .work (*system.work.generate (futurehead::genesis_hash))
	                                               .build ();
	// Receive from genesis
	auto key1_open = builder.make_block ()
	                 .account (key1.pub)
	                 .previous (0)
	                 .representative (key1.pub)
	                 .link (gen_send1->hash ())
	                 .balance (1)
	                 .sign (key1.prv, key1.pub)
	                 .work (*system.work.generate (key1.pub))
	                 .build ();
	// Send to genesis
	auto key1_send1 = builder.make_block ()
	                  .account (key1.pub)
	                  .previous (key1_open->hash ())
	                  .representative (key1.pub)
	                  .link (futurehead::test_genesis_key.pub)
	                  .balance (0)
	                  .sign (key1.prv, key1.pub)
	                  .work (*system.work.generate (key1_open->hash ()))
	                  .build ();
	// Receive from key1
	auto gen_receive = builder.make_block ()
	                   .from (*gen_send1)
	                   .previous (gen_send1->hash ())
	                   .link (key1_send1->hash ())
	                   .balance (futurehead::genesis_amount)
	                   .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                   .work (*system.work.generate (gen_send1->hash ()))
	                   .build ();
	// Send to key2
	auto gen_send2 = builder.make_block ()
	                 .from (*gen_receive)
	                 .previous (gen_receive->hash ())
	                 .link (key2.pub)
	                 .balance (gen_receive->balance ().number () - 2)
	                 .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                 .work (*system.work.generate (gen_receive->hash ()))
	                 .build ();
	// Receive from genesis
	auto key2_open = builder.make_block ()
	                 .account (key2.pub)
	                 .previous (0)
	                 .representative (key2.pub)
	                 .link (gen_send2->hash ())
	                 .balance (2)
	                 .sign (key2.prv, key2.pub)
	                 .work (*system.work.generate (key2.pub))
	                 .build ();
	// Send to key3
	auto key2_send1 = builder.make_block ()
	                  .account (key2.pub)
	                  .previous (key2_open->hash ())
	                  .representative (key2.pub)
	                  .link (key3.pub)
	                  .balance (1)
	                  .sign (key2.prv, key2.pub)
	                  .work (*system.work.generate (key2_open->hash ()))
	                  .build ();
	// Receive from key2
	auto key3_open = builder.make_block ()
	                 .account (key3.pub)
	                 .previous (0)
	                 .representative (key3.pub)
	                 .link (key2_send1->hash ())
	                 .balance (1)
	                 .sign (key3.prv, key3.pub)
	                 .work (*system.work.generate (key3.pub))
	                 .build ();
	// Send to key1
	auto key2_send2 = builder.make_block ()
	                  .from (*key2_send1)
	                  .previous (key2_send1->hash ())
	                  .link (key1.pub)
	                  .balance (key2_send1->balance ().number () - 1)
	                  .sign (key2.prv, key2.pub)
	                  .work (*system.work.generate (key2_send1->hash ()))
	                  .build ();
	// Receive from key2
	auto key1_receive = builder.make_block ()
	                    .from (*key1_send1)
	                    .previous (key1_send1->hash ())
	                    .link (key2_send2->hash ())
	                    .balance (key1_send1->balance ().number () + 1)
	                    .sign (key1.prv, key1.pub)
	                    .work (*system.work.generate (key1_send1->hash ()))
	                    .build ();
	// Send to key3
	auto key1_send2 = builder.make_block ()
	                  .from (*key1_receive)
	                  .previous (key1_receive->hash ())
	                  .link (key3.pub)
	                  .balance (key1_receive->balance ().number () - 1)
	                  .sign (key1.prv, key1.pub)
	                  .work (*system.work.generate (key1_receive->hash ()))
	                  .build ();
	// Receive from key1
	auto key3_receive = builder.make_block ()
	                    .from (*key3_open)
	                    .previous (key3_open->hash ())
	                    .link (key1_send2->hash ())
	                    .balance (key3_open->balance ().number () + 1)
	                    .sign (key3.prv, key3.pub)
	                    .work (*system.work.generate (key3_open->hash ()))
	                    .build ();
	// Upgrade key3
	auto key3_epoch = builder.make_block ()
	                  .from (*key3_receive)
	                  .previous (key3_receive->hash ())
	                  .link (node1.ledger.epoch_link (futurehead::epoch::epoch_1))
	                  .balance (key3_receive->balance ())
	                  .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                  .work (*system.work.generate (key3_receive->hash ()))
	                  .build ();

	for (auto const & node : system.nodes)
	{
		auto transaction (node->store.tx_begin_write ());
		ASSERT_EQ (futurehead::process_result::progress, node->ledger.process (transaction, *gen_send1).code);
		ASSERT_EQ (futurehead::process_result::progress, node->ledger.process (transaction, *key1_open).code);
		ASSERT_EQ (futurehead::process_result::progress, node->ledger.process (transaction, *key1_send1).code);
		ASSERT_EQ (futurehead::process_result::progress, node->ledger.process (transaction, *gen_receive).code);
		ASSERT_EQ (futurehead::process_result::progress, node->ledger.process (transaction, *gen_send2).code);
		ASSERT_EQ (futurehead::process_result::progress, node->ledger.process (transaction, *key2_open).code);
		ASSERT_EQ (futurehead::process_result::progress, node->ledger.process (transaction, *key2_send1).code);
		ASSERT_EQ (futurehead::process_result::progress, node->ledger.process (transaction, *key3_open).code);
		ASSERT_EQ (futurehead::process_result::progress, node->ledger.process (transaction, *key2_send2).code);
		ASSERT_EQ (futurehead::process_result::progress, node->ledger.process (transaction, *key1_receive).code);
		ASSERT_EQ (futurehead::process_result::progress, node->ledger.process (transaction, *key1_send2).code);
		ASSERT_EQ (futurehead::process_result::progress, node->ledger.process (transaction, *key3_receive).code);
		ASSERT_EQ (futurehead::process_result::progress, node->ledger.process (transaction, *key3_epoch).code);
	}

	ASSERT_TRUE (node1.active.empty () && node2.active.empty ());

	// node1 can vote, but only on the first block
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);

	// activate the graph frontier
	// node2 activates dependencies in sequence until it reaches the first block
	node2.block_confirm (node2.block (key3_epoch->hash ()));

	// Eventually the first block in the graph gets activated and confirmed via node1
	ASSERT_TIMELY (15s, node2.block_confirmed (gen_send1->hash ()));

	// Activate the first block in node1, allowing it to confirm all blocks for both nodes
	node1.block_confirm (gen_send1);
	ASSERT_TIMELY (15s, node1.ledger.cache.cemented_count == node1.ledger.cache.block_count);
	ASSERT_TIMELY (5s, node2.ledger.cache.cemented_count == node2.ledger.cache.block_count);
	ASSERT_TIMELY (5s, node1.active.empty () && node2.active.empty ());
}

namespace futurehead
{
TEST (node, deferred_dependent_elections)
{
	futurehead::system system;
	futurehead::node_flags flags;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);
	auto & node2 = *system.add_node (flags); // node2 will be used to ensure all blocks are being propagated

	futurehead::state_block_builder builder;
	futurehead::keypair key;
	std::shared_ptr<futurehead::state_block> send1 = builder.make_block ()
	                                           .account (futurehead::test_genesis_key.pub)
	                                           .previous (futurehead::genesis_hash)
	                                           .representative (futurehead::test_genesis_key.pub)
	                                           .link (key.pub)
	                                           .balance (futurehead::genesis_amount - 1)
	                                           .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                           .work (*system.work.generate (futurehead::genesis_hash))
	                                           .build ();
	std::shared_ptr<futurehead::state_block> open = builder.make_block ()
	                                          .account (key.pub)
	                                          .previous (0)
	                                          .representative (key.pub)
	                                          .link (send1->hash ())
	                                          .balance (1)
	                                          .sign (key.prv, key.pub)
	                                          .work (*system.work.generate (key.pub))
	                                          .build ();
	std::shared_ptr<futurehead::state_block> send2 = builder.make_block ()
	                                           .from (*send1)
	                                           .previous (send1->hash ())
	                                           .balance (send1->balance ().number () - 1)
	                                           .link (key.pub)
	                                           .sign (futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub)
	                                           .work (*system.work.generate (send1->hash ()))
	                                           .build ();
	std::shared_ptr<futurehead::state_block> receive = builder.make_block ()
	                                             .from (*open)
	                                             .previous (open->hash ())
	                                             .link (send2->hash ())
	                                             .balance (2)
	                                             .sign (key.prv, key.pub)
	                                             .work (*system.work.generate (open->hash ()))
	                                             .build ();
	std::shared_ptr<futurehead::state_block> fork = builder.make_block ()
	                                          .from (*receive)
	                                          .representative (futurehead::test_genesis_key.pub)
	                                          .sign (key.prv, key.pub)
	                                          .build ();
	node.process_active (send1);
	node.block_processor.flush ();
	auto election_send1 = node.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election_send1);

	// Should process and republish but not start an election for any dependent blocks
	node.process_active (open);
	node.process_active (send2);
	node.block_processor.flush ();
	ASSERT_TRUE (node.block (open->hash ()));
	ASSERT_TRUE (node.block (send2->hash ()));
	ASSERT_FALSE (node.active.active (open->qualified_root ()));
	ASSERT_FALSE (node.active.active (send2->qualified_root ()));
	ASSERT_TIMELY (2s, node2.block (open->hash ()));
	ASSERT_TIMELY (2s, node2.block (send2->hash ()));

	// Re-processing older blocks with updated work also does not start an election
	node.work_generate_blocking (*open, open->difficulty ());
	node.process_active (open);
	node.block_processor.flush ();
	ASSERT_FALSE (node.active.active (open->qualified_root ()));

	// It is however possible to manually start an election from elsewhere
	node.block_confirm (open);
	ASSERT_TRUE (node.active.active (open->qualified_root ()));

	// Dropping an election allows restarting it [with higher work]
	node.active.erase (*open);
	ASSERT_FALSE (node.active.active (open->qualified_root ()));
	ASSERT_NE (std::chrono::steady_clock::time_point{}, node.active.recently_dropped.find (open->qualified_root ()));
	node.process_active (open);
	node.block_processor.flush ();
	ASSERT_TRUE (node.active.active (open->qualified_root ()));

	// Frontier confirmation also starts elections
	ASSERT_NO_ERROR (system.poll_until_true (5s, [&node, &send2] {
		futurehead::unique_lock<std::mutex> lock (node.active.mutex);
		node.active.frontiers_confirmation (lock);
		lock.unlock ();
		return node.active.election (send2->qualified_root ()) != nullptr;
	}));

	// Drop both elections
	node.active.erase (*open);
	ASSERT_FALSE (node.active.active (open->qualified_root ()));
	node.active.erase (*send2);
	ASSERT_FALSE (node.active.active (send2->qualified_root ()));

	// Confirming send1 will automatically start elections for the dependents
	{
		futurehead::lock_guard<std::mutex> guard (node.active.mutex);
		election_send1->confirm_once ();
	}
	ASSERT_TIMELY (2s, node.block_confirmed (send1->hash ()));
	ASSERT_TIMELY (2s, node.active.active (open->qualified_root ()) && node.active.active (send2->qualified_root ()));
	auto election_open = node.active.election (open->qualified_root ());
	ASSERT_NE (nullptr, election_open);
	auto election_send2 = node.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election_open);

	// Confirm one of the dependents of the receive but not the other, to ensure both have to be confirmed to start an election on processing
	ASSERT_EQ (futurehead::process_result::progress, node.process (*receive).code);
	ASSERT_FALSE (node.active.active (receive->qualified_root ()));
	{
		futurehead::lock_guard<std::mutex> guard (node.active.mutex);
		election_open->confirm_once ();
	}
	ASSERT_TIMELY (2s, node.block_confirmed (open->hash ()));
	ASSERT_FALSE (node.ledger.can_vote (node.store.tx_begin_read (), *receive));
	std::this_thread::sleep_for (500ms);
	ASSERT_FALSE (node.active.active (receive->qualified_root ()));
	ASSERT_FALSE (node.ledger.rollback (node.store.tx_begin_write (), receive->hash ()));
	ASSERT_FALSE (node.block (receive->hash ()));
	node.process_active (receive);
	node.block_processor.flush ();
	ASSERT_TRUE (node.block (receive->hash ()));
	ASSERT_FALSE (node.active.active (receive->qualified_root ()));

	// Processing a fork will also not start an election
	ASSERT_EQ (futurehead::process_result::fork, node.process (*fork).code);
	node.process_active (fork);
	node.block_processor.flush ();
	ASSERT_FALSE (node.active.active (receive->qualified_root ()));

	// Confirming the other dependency allows starting an election from a fork
	{
		futurehead::lock_guard<std::mutex> guard (node.active.mutex);
		election_send2->confirm_once ();
	}
	ASSERT_TIMELY (2s, node.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (2s, node.active.active (receive->qualified_root ()));
	node.active.erase (*receive);
	ASSERT_FALSE (node.active.active (receive->qualified_root ()));
	node.process_active (fork);
	node.block_processor.flush ();
	ASSERT_TRUE (node.active.active (receive->qualified_root ()));
}
}

namespace
{
void add_required_children_node_config_tree (futurehead::jsonconfig & tree)
{
	futurehead::logging logging1;
	futurehead::jsonconfig logging_l;
	logging1.serialize_json (logging_l);
	tree.put_child ("logging", logging_l);
	futurehead::jsonconfig preconfigured_peers_l;
	tree.put_child ("preconfigured_peers", preconfigured_peers_l);
	futurehead::jsonconfig preconfigured_representatives_l;
	tree.put_child ("preconfigured_representatives", preconfigured_representatives_l);
	futurehead::jsonconfig work_peers_l;
	tree.put_child ("work_peers", work_peers_l);
	tree.put ("version", std::to_string (futurehead::node_config::json_version ()));
}
}
