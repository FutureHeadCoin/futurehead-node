#include <futurehead/core_test/common.hpp>
#include <futurehead/core_test/fakes/websocket_client.hpp>
#include <futurehead/core_test/testutil.hpp>
#include <futurehead/node/testing.hpp>
#include <futurehead/node/websocket.hpp>

#include <gtest/gtest.h>

#include <boost/property_tree/json_parser.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace std::chrono_literals;

// Tests clients subscribing multiple times or unsubscribing without a subscription
TEST (websocket, subscription_edge)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	auto node1 (system.add_node (config));

	ASSERT_EQ (0, node1->websocket_server->subscriber_count (futurehead::websocket::topic::confirmation));

	auto task = ([config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json");
		client.await_ack ();
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::confirmation));
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json");
		client.await_ack ();
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::confirmation));
		client.send_message (R"json({"action": "unsubscribe", "topic": "confirmation", "ack": true})json");
		client.await_ack ();
		EXPECT_EQ (0, node1->websocket_server->subscriber_count (futurehead::websocket::topic::confirmation));
		client.send_message (R"json({"action": "unsubscribe", "topic": "confirmation", "ack": true})json");
		client.await_ack ();
		EXPECT_EQ (0, node1->websocket_server->subscriber_count (futurehead::websocket::topic::confirmation));
	});
	auto future = std::async (std::launch::async, task);

	system.deadline_set (5s);
	while (future.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Test client subscribing to changes in active_multiplier
TEST (websocket, active_difficulty)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	futurehead::node_flags node_flags;
	// Disable auto-updating active difficulty (multiplier) to prevent intermittent failures
	node_flags.disable_request_loop = true;
	auto node1 (system.add_node (config, node_flags));

	// "Start" epoch 2
	node1->ledger.cache.epoch_2_started = true;
	ASSERT_EQ (node1->default_difficulty (futurehead::work_version::work_1), node1->network_params.network.publish_thresholds.epoch_2);

	ASSERT_EQ (0, node1->websocket_server->subscriber_count (futurehead::websocket::topic::active_difficulty));

	std::atomic<bool> ack_ready{ false };
	auto task = ([&ack_ready, config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "active_difficulty", "ack": true})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::active_difficulty));
		return client.get_response ();
	});
	auto future = std::async (std::launch::async, task);

	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Fake history records and force a trended_active_multiplier change
	{
		futurehead::unique_lock<std::mutex> lock (node1->active.mutex);
		node1->active.multipliers_cb.push_front (10.);
		node1->active.update_active_multiplier (lock);
	}

	system.deadline_set (5s);
	while (future.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Check active_difficulty response
	boost::optional<std::string> response = future.get ();
	ASSERT_TRUE (response);
	std::stringstream stream;
	stream << response;
	boost::property_tree::ptree event;
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "active_difficulty");

	auto message_contents = event.get_child ("message");
	uint64_t network_minimum;
	futurehead::from_string_hex (message_contents.get<std::string> ("network_minimum"), network_minimum);
	ASSERT_EQ (network_minimum, node1->default_difficulty (futurehead::work_version::work_1));

	uint64_t network_current;
	futurehead::from_string_hex (message_contents.get<std::string> ("network_current"), network_current);
	ASSERT_EQ (network_current, node1->active.active_difficulty ());

	double multiplier = message_contents.get<double> ("multiplier");
	ASSERT_NEAR (multiplier, futurehead::difficulty::to_multiplier (node1->active.active_difficulty (), node1->default_difficulty (futurehead::work_version::work_1)), 1e-6);
}

// Subscribes to block confirmations, confirms a block and then awaits websocket notification
TEST (websocket, confirmation)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	std::atomic<bool> unsubscribed{ false };
	auto task = ([&ack_ready, &unsubscribed, config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::confirmation));
		auto response = client.get_response ();
		EXPECT_TRUE (response);
		boost::property_tree::ptree event;
		std::stringstream stream;
		stream << response.get ();
		boost::property_tree::read_json (stream, event);
		EXPECT_EQ (event.get<std::string> ("topic"), "confirmation");
		client.send_message (R"json({"action": "unsubscribe", "topic": "confirmation", "ack": true})json");
		client.await_ack ();
		unsubscribed = true;
		EXPECT_FALSE (client.get_response (1s));
	});
	auto future = std::async (std::launch::async, task);

	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	futurehead::keypair key;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto balance = futurehead::genesis_amount;
	auto send_amount = node1->config.online_weight_minimum.number () + 1;
	// Quick-confirm a block, legacy blocks should work without filtering
	{
		futurehead::block_hash previous (node1->latest (futurehead::test_genesis_key.pub));
		balance -= send_amount;
		auto send (std::make_shared<futurehead::send_block> (previous, key.pub, balance, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (previous)));
		node1->process_active (send);
	}

	system.deadline_set (5s);
	while (!unsubscribed)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Quick confirm a state block
	{
		futurehead::block_hash previous (node1->latest (futurehead::test_genesis_key.pub));
		balance -= send_amount;
		auto send (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, previous, futurehead::test_genesis_key.pub, balance, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (previous)));
		node1->process_active (send);
	}

	system.deadline_set (5s);
	while (future.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Tests getting notification of an erased election
TEST (websocket, stopped_election)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task = ([&ack_ready, config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "stopped_election", "ack": "true"})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::stopped_election));
		return client.get_response ();
	});
	auto future = std::async (std::launch::async, task);

	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Create election, then erase it, causing a websocket message to be emitted
	futurehead::keypair key1;
	futurehead::genesis genesis;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	futurehead::publish publish1 (send1);
	auto channel1 (node1->network.udp_channels.create (node1->network.endpoint ()));
	node1->network.process_message (publish1, channel1);
	node1->block_processor.flush ();
	node1->active.erase (*send1);

	system.deadline_set (5s);
	while (future.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	auto response = future.get ();
	ASSERT_TRUE (response);
	boost::property_tree::ptree event;
	std::stringstream stream;
	stream << response.get ();
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "stopped_election");
}

// Tests the filtering options of block confirmations
TEST (websocket, confirmation_options)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task1 = ([&ack_ready, config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"confirmation_type": "active_quorum", "accounts": ["xrb_invalid"]}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::confirmation));
		auto response = client.get_response (1s);
		EXPECT_FALSE (response);
	});
	auto future1 = std::async (std::launch::async, task1);

	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Confirm a state block for an in-wallet account
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::keypair key;
	auto balance = futurehead::genesis_amount;
	auto send_amount = node1->config.online_weight_minimum.number () + 1;
	futurehead::block_hash previous (node1->latest (futurehead::test_genesis_key.pub));
	{
		balance -= send_amount;
		auto send (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, previous, futurehead::test_genesis_key.pub, balance, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (previous)));
		node1->process_active (send);
		previous = send->hash ();
	}

	system.deadline_set (5s);
	while (future1.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	ack_ready = false;
	auto task2 = ([&ack_ready, config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"confirmation_type": "active_quorum", "all_local_accounts": "true", "include_election_info": "true"}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::confirmation));
		return client.get_response ();
	});
	auto future2 = std::async (std::launch::async, task2);

	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Quick-confirm another block
	{
		balance -= send_amount;
		auto send (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, previous, futurehead::test_genesis_key.pub, balance, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (previous)));
		node1->process_active (send);
		previous = send->hash ();
	}

	system.deadline_set (5s);
	while (future2.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	auto response2 = future2.get ();
	ASSERT_TRUE (response2);
	boost::property_tree::ptree event;
	std::stringstream stream;
	stream << response2.get ();
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "confirmation");
	try
	{
		boost::property_tree::ptree election_info = event.get_child ("message.election_info");
		auto tally (election_info.get<std::string> ("tally"));
		auto time (election_info.get<std::string> ("time"));
		// Duration and request count may be zero on testnet, so we only check that they're present
		ASSERT_EQ (1, election_info.count ("duration"));
		ASSERT_EQ (1, election_info.count ("request_count"));
		ASSERT_EQ (1, election_info.count ("voters"));
		ASSERT_GE (1, election_info.get<unsigned> ("blocks"));
		// Make sure tally and time are non-zero.
		ASSERT_NE ("0", tally);
		ASSERT_NE ("0", time);
	}
	catch (std::runtime_error const & ex)
	{
		FAIL () << ex.what ();
	}

	ack_ready = false;
	auto task3 = ([&ack_ready, config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"confirmation_type": "active_quorum", "all_local_accounts": "true"}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::confirmation));
		auto response = client.get_response (1s);
		EXPECT_FALSE (response);
	});
	auto future3 = std::async (std::launch::async, task3);

	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Confirm a legacy block
	// When filtering options are enabled, legacy blocks are always filtered
	{
		balance -= send_amount;
		auto send (std::make_shared<futurehead::send_block> (previous, key.pub, balance, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (previous)));
		node1->process_active (send);
		previous = send->hash ();
	}

	system.deadline_set (5s);
	while (future1.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Tests updating options of block confirmations
TEST (websocket, confirmation_options_update)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> added{ false };
	std::atomic<bool> deleted{ false };
	auto task = ([&added, &deleted, config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		// Subscribe initially with empty options, everything will be filtered
		client.send_message (R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {}})json");
		client.await_ack ();
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::confirmation));
		// Now update filter with an account and wait for a response
		std::string add_message = boost::str (boost::format (R"json({"action": "update", "topic": "confirmation", "ack": "true", "options": {"accounts_add": ["%1%"]}})json") % futurehead::test_genesis_key.pub.to_account ());
		client.send_message (add_message);
		client.await_ack ();
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::confirmation));
		added = true;
		EXPECT_TRUE (client.get_response ());
		// Update the filter again, removing the account
		std::string delete_message = boost::str (boost::format (R"json({"action": "update", "topic": "confirmation", "ack": "true", "options": {"accounts_del": ["%1%"]}})json") % futurehead::test_genesis_key.pub.to_account ());
		client.send_message (delete_message);
		client.await_ack ();
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::confirmation));
		deleted = true;
		EXPECT_FALSE (client.get_response (1s));
	});
	auto future = std::async (std::launch::async, task);

	// Wait for update acknowledgement
	system.deadline_set (5s);
	while (!added)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Confirm a block
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::genesis genesis;
	futurehead::keypair key;
	auto previous (node1->latest (futurehead::test_genesis_key.pub));
	auto send (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, previous, futurehead::test_genesis_key.pub, futurehead::genesis_amount - futurehead::Gxrb_ratio, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (previous)));
	node1->process_active (send);

	// Wait for delete acknowledgement
	system.deadline_set (5s);
	while (!deleted)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Confirm another block
	previous = send->hash ();
	auto send2 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, previous, futurehead::test_genesis_key.pub, futurehead::genesis_amount - 2 * futurehead::Gxrb_ratio, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (previous)));
	node1->process_active (send2);

	system.deadline_set (5s);
	while (future.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Subscribes to votes, sends a block and awaits websocket notification of a vote arrival
TEST (websocket, vote)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task = ([&ack_ready, config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "vote", "ack": true})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::vote));
		return client.get_response ();
	});
	auto future = std::async (std::launch::async, task);

	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Quick-confirm a block
	futurehead::keypair key;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	futurehead::block_hash previous (node1->latest (futurehead::test_genesis_key.pub));
	auto send (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, previous, futurehead::test_genesis_key.pub, futurehead::genesis_amount - (node1->config.online_weight_minimum.number () + 1), key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (previous)));
	node1->process_active (send);

	system.deadline_set (5s);
	while (future.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	auto response = future.get ();
	ASSERT_TRUE (response);
	boost::property_tree::ptree event;
	std::stringstream stream;
	stream << response;
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "vote");
}

// Tests vote subscription options - vote type
TEST (websocket, vote_options_type)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task = ([&ack_ready, config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "vote", "ack": true, "options": {"include_replays": "true", "include_indeterminate": "false"}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::vote));
		return client.get_response ();
	});
	auto future = std::async (std::launch::async, task);

	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Custom made votes for simplicity
	futurehead::genesis genesis;
	auto vote (std::make_shared<futurehead::vote> (futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, 0, genesis.open));
	futurehead::websocket::message_builder builder;
	auto msg (builder.vote_received (vote, futurehead::vote_code::replay));
	node1->websocket_server->broadcast (msg);

	system.deadline_set (5s);
	while (future.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	auto response = future.get ();
	ASSERT_TRUE (response);
	boost::property_tree::ptree event;
	std::stringstream stream;
	stream << response;
	boost::property_tree::read_json (stream, event);
	auto message_contents = event.get_child ("message");
	ASSERT_EQ (1, message_contents.count ("type"));
	ASSERT_EQ ("replay", message_contents.get<std::string> ("type"));
}

// Tests vote subscription options - list of representatives
TEST (websocket, vote_options_representatives)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task1 = ([&ack_ready, config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		std::string message = boost::str (boost::format (R"json({"action": "subscribe", "topic": "vote", "ack": "true", "options": {"representatives": ["%1%"]}})json") % futurehead::test_genesis_key.pub.to_account ());
		client.send_message (message);
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::vote));
		auto response = client.get_response ();
		EXPECT_TRUE (response);
		boost::property_tree::ptree event;
		std::stringstream stream;
		stream << response;
		boost::property_tree::read_json (stream, event);
		EXPECT_EQ (event.get<std::string> ("topic"), "vote");
	});
	auto future1 = std::async (std::launch::async, task1);

	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Quick-confirm a block
	futurehead::keypair key;
	auto balance = futurehead::genesis_amount;
	system.wallet (0)->insert_adhoc (futurehead::test_genesis_key.prv);
	auto send_amount = node1->config.online_weight_minimum.number () + 1;
	auto confirm_block = [&]() {
		futurehead::block_hash previous (node1->latest (futurehead::test_genesis_key.pub));
		balance -= send_amount;
		auto send (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, previous, futurehead::test_genesis_key.pub, balance, key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (previous)));
		node1->process_active (send);
	};
	confirm_block ();

	system.deadline_set (5s);
	while (future1.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	ack_ready = false;
	auto task2 = ([&ack_ready, config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "vote", "ack": "true", "options": {"representatives": ["xrb_invalid"]}})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::vote));
		auto response = client.get_response ();
		// A list of invalid representatives is the same as no filter
		EXPECT_TRUE (response);
	});
	auto future2 = std::async (std::launch::async, task2);

	// Wait for the subscription to be acknowledged
	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Confirm another block
	confirm_block ();

	system.deadline_set (5s);
	while (future2.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Test client subscribing to notifications for work generation
TEST (websocket, work)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	auto node1 (system.add_node (config));

	ASSERT_EQ (0, node1->websocket_server->subscriber_count (futurehead::websocket::topic::work));

	// Subscribe to work and wait for response asynchronously
	std::atomic<bool> ack_ready{ false };
	auto task = ([&ack_ready, config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "work", "ack": true})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::work));
		return client.get_response ();
	});
	auto future = std::async (std::launch::async, task);

	// Wait for acknowledge
	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::work));

	// Generate work
	futurehead::block_hash hash{ 1 };
	auto work (node1->work_generate_blocking (hash));
	ASSERT_TRUE (work.is_initialized ());

	// Wait for the work notification
	system.deadline_set (5s);
	while (future.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Check the work notification message
	auto response = future.get ();
	ASSERT_TRUE (response);
	std::stringstream stream;
	stream << response;
	boost::property_tree::ptree event;
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "work");

	auto & contents = event.get_child ("message");
	ASSERT_EQ (contents.get<std::string> ("success"), "true");
	ASSERT_LT (contents.get<unsigned> ("duration"), 10000);

	ASSERT_EQ (1, contents.count ("request"));
	auto & request = contents.get_child ("request");
	ASSERT_EQ (request.get<std::string> ("version"), futurehead::to_string (futurehead::work_version::work_1));
	ASSERT_EQ (request.get<std::string> ("hash"), hash.to_string ());
	ASSERT_EQ (request.get<std::string> ("difficulty"), futurehead::to_string_hex (node1->default_difficulty (futurehead::work_version::work_1)));
	ASSERT_EQ (request.get<double> ("multiplier"), 1.0);

	ASSERT_EQ (1, contents.count ("result"));
	auto & result = contents.get_child ("result");
	uint64_t result_difficulty;
	futurehead::from_string_hex (result.get<std::string> ("difficulty"), result_difficulty);
	ASSERT_GE (result_difficulty, node1->default_difficulty (futurehead::work_version::work_1));
	ASSERT_NEAR (result.get<double> ("multiplier"), futurehead::difficulty::to_multiplier (result_difficulty, node1->default_difficulty (futurehead::work_version::work_1)), 1e-6);
	ASSERT_EQ (result.get<std::string> ("work"), futurehead::to_string_hex (work.get ()));

	ASSERT_EQ (1, contents.count ("bad_peers"));
	auto & bad_peers = contents.get_child ("bad_peers");
	ASSERT_TRUE (bad_peers.empty ());

	ASSERT_EQ (contents.get<std::string> ("reason"), "");
}

// Test client subscribing to notifications for bootstrap
TEST (websocket, bootstrap)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	auto node1 (system.add_node (config));

	ASSERT_EQ (0, node1->websocket_server->subscriber_count (futurehead::websocket::topic::bootstrap));

	// Subscribe to bootstrap and wait for response asynchronously
	std::atomic<bool> ack_ready{ false };
	auto task = ([&ack_ready, config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "bootstrap", "ack": true})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::bootstrap));
		return client.get_response ();
	});
	auto future = std::async (std::launch::async, task);

	// Wait for acknowledge
	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Start bootstrap attempt
	node1->bootstrap_initiator.bootstrap (true, "123abc");
	ASSERT_NE (nullptr, node1->bootstrap_initiator.current_attempt ());

	// Wait for the bootstrap notification
	system.deadline_set (5s);
	while (future.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Check the bootstrap notification message
	auto response = future.get ();
	ASSERT_TRUE (response);
	std::stringstream stream;
	stream << response;
	boost::property_tree::ptree event;
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "bootstrap");

	auto & contents = event.get_child ("message");
	ASSERT_EQ (contents.get<std::string> ("reason"), "started");
	ASSERT_EQ (contents.get<std::string> ("id"), "123abc");
	ASSERT_EQ (contents.get<std::string> ("mode"), "legacy");

	// Wait for bootstrap finish
	system.deadline_set (5s);
	while (node1->bootstrap_initiator.in_progress ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (websocket, bootstrap_exited)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	auto node1 (system.add_node (config));

	// Start bootstrap, exit after subscription
	std::atomic<bool> bootstrap_started{ false };
	futurehead::util::counted_completion subscribed_completion (1);
	std::thread bootstrap_thread ([node1, &system, &bootstrap_started, &subscribed_completion]() {
		std::shared_ptr<futurehead::bootstrap_attempt> attempt;
		while (attempt == nullptr)
		{
			std::this_thread::sleep_for (50ms);
			node1->bootstrap_initiator.bootstrap (true, "123abc");
			attempt = node1->bootstrap_initiator.current_attempt ();
		}
		ASSERT_NE (nullptr, attempt);
		bootstrap_started = true;
		EXPECT_FALSE (subscribed_completion.await_count_for (5s));
	});

	// Wait for bootstrap start
	system.deadline_set (5s);
	while (!bootstrap_started)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Subscribe to bootstrap and wait for response asynchronously
	std::atomic<bool> ack_ready{ false };
	auto task = ([&ack_ready, config, &node1]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "bootstrap", "ack": true})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::bootstrap));
		return client.get_response ();
	});
	auto future = std::async (std::launch::async, task);

	// Wait for acknowledge
	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Wait for the bootstrap notification
	subscribed_completion.increment ();
	bootstrap_thread.join ();
	system.deadline_set (5s);
	while (future.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Check the bootstrap notification message
	auto response = future.get ();
	ASSERT_TRUE (response);
	std::stringstream stream;
	stream << response;
	boost::property_tree::ptree event;
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "bootstrap");

	auto & contents = event.get_child ("message");
	ASSERT_EQ (contents.get<std::string> ("reason"), "exited");
	ASSERT_EQ (contents.get<std::string> ("id"), "123abc");
	ASSERT_EQ (contents.get<std::string> ("mode"), "legacy");
	ASSERT_EQ (contents.get<unsigned> ("total_blocks"), 0);
	ASSERT_LT (contents.get<unsigned> ("duration"), 15000);
}

// Tests sending keepalive
TEST (websocket, ws_keepalive)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	auto node1 (system.add_node (config));

	auto task = ([config]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "ping"})json");
		client.await_ack ();
	});
	auto future = std::async (std::launch::async, task);

	system.deadline_set (5s);
	while (future.wait_for (0s) != std::future_status::ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Tests sending telemetry
TEST (websocket, telemetry)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	futurehead::node_flags node_flags;
	node_flags.disable_initial_telemetry_requests = true;
	node_flags.disable_ongoing_telemetry_requests = true;
	auto node1 (system.add_node (config, node_flags));
	config.peering_port = futurehead::get_available_port ();
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	auto node2 (system.add_node (config, node_flags));

	wait_peer_connections (system);

	std::atomic<bool> done{ false };
	auto task = ([config = node1->config, &node1, &done]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "telemetry", "ack": true})json");
		client.await_ack ();
		done = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::telemetry));
		return client.get_response ();
	});

	auto future = std::async (std::launch::async, task);

	ASSERT_TIMELY (10s, done);

	node1->telemetry->get_metrics_single_peer_async (node1->network.find_channel (node2->network.endpoint ()), [](auto const & response_a) {
		ASSERT_FALSE (response_a.error);
	});

	ASSERT_TIMELY (10s, future.wait_for (0s) == std::future_status::ready);

	// Check the telemetry notification message
	auto response = future.get ();

	std::stringstream stream;
	stream << response;
	boost::property_tree::ptree event;
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "telemetry");

	auto & contents = event.get_child ("message");
	futurehead::jsonconfig telemetry_contents (contents);
	futurehead::telemetry_data telemetry_data;
	telemetry_data.deserialize_json (telemetry_contents, false);
	compare_default_telemetry_response_data (telemetry_data, node2->network_params, node2->config.bandwidth_limit, node2->active.active_difficulty (), node2->node_id);

	ASSERT_EQ (contents.get<std::string> ("address"), node2->network.endpoint ().address ().to_string ());
	ASSERT_EQ (contents.get<uint16_t> ("port"), node2->network.endpoint ().port ());

	// Other node should have no subscribers
	EXPECT_EQ (0, node2->websocket_server->subscriber_count (futurehead::websocket::topic::telemetry));
}

TEST (websocket, new_unconfirmed_block)
{
	futurehead::system system;
	futurehead::node_config config (futurehead::get_available_port (), system.logging);
	config.websocket_config.enabled = true;
	config.websocket_config.port = futurehead::get_available_port ();
	auto node1 (system.add_node (config));

	std::atomic<bool> ack_ready{ false };
	auto task = ([&ack_ready, config, node1]() {
		fake_websocket_client client (config.websocket_config.port);
		client.send_message (R"json({"action": "subscribe", "topic": "new_unconfirmed_block", "ack": "true"})json");
		client.await_ack ();
		ack_ready = true;
		EXPECT_EQ (1, node1->websocket_server->subscriber_count (futurehead::websocket::topic::new_unconfirmed_block));
		return client.get_response ();
	});
	auto future = std::async (std::launch::async, task);

	ASSERT_TIMELY (5s, ack_ready);

	// Process a new block
	futurehead::genesis genesis;
	auto send1 (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 1, futurehead::test_genesis_key.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	ASSERT_EQ (futurehead::process_result::progress, node1->process_local (send1).code);

	ASSERT_TIMELY (5s, future.wait_for (0s) == std::future_status::ready);

	// Check the response
	boost::optional<std::string> response = future.get ();
	ASSERT_TRUE (response);
	std::stringstream stream;
	stream << response;
	boost::property_tree::ptree event;
	boost::property_tree::read_json (stream, event);
	ASSERT_EQ (event.get<std::string> ("topic"), "new_unconfirmed_block");

	auto message_contents = event.get_child ("message");
	ASSERT_EQ ("state", message_contents.get<std::string> ("type"));
	ASSERT_EQ ("send", message_contents.get<std::string> ("subtype"));
}
