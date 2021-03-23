#include <futurehead/core_test/testutil.hpp>
#include <futurehead/node/election.hpp>
#include <futurehead/node/testing.hpp>

#include <gtest/gtest.h>

#include <boost/variant/get.hpp>

using namespace std::chrono_literals;

TEST (conflicts, start_stop)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	ASSERT_EQ (futurehead::process_result::progress, node1.process (*send1).code);
	ASSERT_EQ (0, node1.active.size ());
	auto election1 = node1.active.insert (send1);
	ASSERT_EQ (1, node1.active.size ());
	{
		futurehead::lock_guard<std::mutex> guard (node1.active.mutex);
		ASSERT_NE (nullptr, election1.election);
		ASSERT_EQ (1, election1.election->last_votes.size ());
	}
}

TEST (conflicts, add_existing)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	ASSERT_EQ (futurehead::process_result::progress, node1.process (*send1).code);
	node1.active.insert (send1);
	futurehead::keypair key2;
	auto send2 (std::make_shared<futurehead::send_block> (genesis.hash (), key2.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	send2->sideband_set ({});
	auto election1 = node1.active.insert (send2);
	ASSERT_EQ (1, node1.active.size ());
	auto vote1 (std::make_shared<futurehead::vote> (key2.pub, key2.prv, 0, send2));
	node1.active.vote (vote1);
	ASSERT_EQ (1, node1.active.size ());
	{
		futurehead::lock_guard<std::mutex> guard (node1.active.mutex);
		ASSERT_NE (nullptr, election1.election);
		ASSERT_EQ (2, election1.election->last_votes.size ());
		ASSERT_NE (election1.election->last_votes.end (), election1.election->last_votes.find (key2.pub));
	}
}

TEST (conflicts, add_two)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	ASSERT_EQ (futurehead::process_result::progress, node1.process (*send1).code);
	node1.active.insert (send1);
	futurehead::keypair key2;
	auto send2 (std::make_shared<futurehead::send_block> (send1->hash (), key2.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	ASSERT_EQ (futurehead::process_result::progress, node1.process (*send2).code);
	node1.active.insert (send2);
	ASSERT_EQ (2, node1.active.size ());
}

TEST (vote_uniquer, null)
{
	futurehead::block_uniquer block_uniquer;
	futurehead::vote_uniquer uniquer (block_uniquer);
	ASSERT_EQ (nullptr, uniquer.unique (nullptr));
}

// Show that an identical vote can be uniqued
TEST (vote_uniquer, same_vote)
{
	futurehead::block_uniquer block_uniquer;
	futurehead::vote_uniquer uniquer (block_uniquer);
	futurehead::keypair key;
	auto vote1 (std::make_shared<futurehead::vote> (key.pub, key.prv, 0, std::make_shared<futurehead::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0)));
	auto vote2 (std::make_shared<futurehead::vote> (*vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote2));
}

// Show that a different vote for the same block will have the block uniqued
TEST (vote_uniquer, same_block)
{
	futurehead::block_uniquer block_uniquer;
	futurehead::vote_uniquer uniquer (block_uniquer);
	futurehead::keypair key1;
	futurehead::keypair key2;
	auto block1 (std::make_shared<futurehead::state_block> (0, 0, 0, 0, 0, key1.prv, key1.pub, 0));
	auto block2 (std::make_shared<futurehead::state_block> (*block1));
	auto vote1 (std::make_shared<futurehead::vote> (key1.pub, key1.prv, 0, block1));
	auto vote2 (std::make_shared<futurehead::vote> (key1.pub, key1.prv, 0, block2));
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote2, uniquer.unique (vote2));
	ASSERT_NE (vote1, vote2);
	ASSERT_EQ (boost::get<std::shared_ptr<futurehead::block>> (vote1->blocks[0]), boost::get<std::shared_ptr<futurehead::block>> (vote2->blocks[0]));
}

TEST (vote_uniquer, vbh_one)
{
	futurehead::block_uniquer block_uniquer;
	futurehead::vote_uniquer uniquer (block_uniquer);
	futurehead::keypair key;
	auto block (std::make_shared<futurehead::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0));
	std::vector<futurehead::block_hash> hashes;
	hashes.push_back (block->hash ());
	auto vote1 (std::make_shared<futurehead::vote> (key.pub, key.prv, 0, hashes));
	auto vote2 (std::make_shared<futurehead::vote> (*vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote2));
}

TEST (vote_uniquer, vbh_two)
{
	futurehead::block_uniquer block_uniquer;
	futurehead::vote_uniquer uniquer (block_uniquer);
	futurehead::keypair key;
	auto block1 (std::make_shared<futurehead::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0));
	std::vector<futurehead::block_hash> hashes1;
	hashes1.push_back (block1->hash ());
	auto block2 (std::make_shared<futurehead::state_block> (1, 0, 0, 0, 0, key.prv, key.pub, 0));
	std::vector<futurehead::block_hash> hashes2;
	hashes2.push_back (block2->hash ());
	auto vote1 (std::make_shared<futurehead::vote> (key.pub, key.prv, 0, hashes1));
	auto vote2 (std::make_shared<futurehead::vote> (key.pub, key.prv, 0, hashes2));
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote2, uniquer.unique (vote2));
}

TEST (vote_uniquer, cleanup)
{
	futurehead::block_uniquer block_uniquer;
	futurehead::vote_uniquer uniquer (block_uniquer);
	futurehead::keypair key;
	auto vote1 (std::make_shared<futurehead::vote> (key.pub, key.prv, 0, std::make_shared<futurehead::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0)));
	auto vote2 (std::make_shared<futurehead::vote> (key.pub, key.prv, 1, std::make_shared<futurehead::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0)));
	auto vote3 (uniquer.unique (vote1));
	auto vote4 (uniquer.unique (vote2));
	vote2.reset ();
	vote4.reset ();
	ASSERT_EQ (2, uniquer.size ());
	auto iterations (0);
	while (uniquer.size () == 2)
	{
		auto vote5 (uniquer.unique (vote1));
		ASSERT_LT (iterations++, 200);
	}
}

TEST (conflicts, reprioritize)
{
	futurehead::system system (1);
	auto & node1 (*system.nodes[0]);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, 0, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto difficulty1 (send1->difficulty ());
	auto multiplier1 (futurehead::normalized_multiplier (futurehead::difficulty::to_multiplier (difficulty1, futurehead::work_threshold (send1->work_version (), futurehead::block_details (futurehead::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */))), node1.network_params.network.publish_thresholds.epoch_1));
	futurehead::send_block send1_copy (*send1);
	node1.process_active (send1);
	node1.block_processor.flush ();
	{
		futurehead::lock_guard<std::mutex> guard (node1.active.mutex);
		auto existing1 (node1.active.roots.find (send1->qualified_root ()));
		ASSERT_NE (node1.active.roots.end (), existing1);
		ASSERT_EQ (multiplier1, existing1->multiplier);
	}
	node1.work_generate_blocking (send1_copy, difficulty1);
	auto difficulty2 (send1_copy.difficulty ());
	auto multiplier2 (futurehead::normalized_multiplier (futurehead::difficulty::to_multiplier (difficulty2, futurehead::work_threshold (send1_copy.work_version (), futurehead::block_details (futurehead::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */))), node1.network_params.network.publish_thresholds.epoch_1));
	node1.process_active (std::make_shared<futurehead::send_block> (send1_copy));
	node1.block_processor.flush ();
	{
		futurehead::lock_guard<std::mutex> guard (node1.active.mutex);
		auto existing2 (node1.active.roots.find (send1->qualified_root ()));
		ASSERT_NE (node1.active.roots.end (), existing2);
		ASSERT_EQ (multiplier2, existing2->multiplier);
	}
}

TEST (conflicts, dependency)
{
	futurehead::system system;
	futurehead::node_config node_config (futurehead::get_available_port (), system.logging);
	node_config.frontiers_confirmation = futurehead::frontiers_confirmation_mode::disabled;
	auto node1 = system.add_node (node_config);
	futurehead::genesis genesis;
	futurehead::keypair key1;
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, futurehead::genesis_amount - futurehead::xrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	node1->work_generate_blocking (*send1);
	auto state_open1 (std::make_shared<futurehead::state_block> (key1.pub, 0, futurehead::test_genesis_key.pub, futurehead::xrb_ratio, send1->hash (), key1.prv, key1.pub, 0));
	node1->work_generate_blocking (*state_open1);
	ASSERT_EQ (futurehead::process_result::progress, node1->process (*send1).code);
	ASSERT_EQ (futurehead::process_result::progress, node1->process (*state_open1).code);
	ASSERT_EQ (0, node1->active.size ());
	auto election1 = node1->active.insert (send1);
	node1->active.insert (state_open1);
	ASSERT_EQ (2, node1->active.size ());
	// Check dependency for send block
	{
		futurehead::lock_guard<std::mutex> guard (node1->active.mutex);
		ASSERT_NE (nullptr, election1.election);
		ASSERT_EQ (1, election1.election->dependent_blocks.size ());
		ASSERT_NE (election1.election->dependent_blocks.end (), election1.election->dependent_blocks.find (state_open1->hash ()));
	}
}

TEST (conflicts, adjusted_multiplier)
{
	futurehead::system system;
	futurehead::node_flags flags;
	flags.disable_request_loop = true;
	auto & node1 (*system.add_node (flags));
	futurehead::genesis genesis;
	futurehead::keypair key1;
	futurehead::keypair key2;
	futurehead::keypair key3;
	ASSERT_EQ (0, node1.active.size ());
	auto send1 (std::make_shared<futurehead::send_block> (genesis.hash (), key1.pub, futurehead::genesis_amount - 2 * futurehead::xrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (genesis.hash ())));
	node1.process_active (send1);
	auto send2 (std::make_shared<futurehead::send_block> (send1->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 3 * futurehead::xrb_ratio, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (send1->hash ())));
	node1.process_active (send2);
	auto receive1 (std::make_shared<futurehead::receive_block> (send2->hash (), send2->hash (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (send2->hash ())));
	node1.process_active (receive1);
	auto open1 (std::make_shared<futurehead::open_block> (send1->hash (), key1.pub, key1.pub, key1.prv, key1.pub, *system.work.generate (key1.pub)));
	node1.process_active (open1);
	auto send3 (std::make_shared<futurehead::state_block> (key1.pub, open1->hash (), key1.pub, futurehead::xrb_ratio, key2.pub, key1.prv, key1.pub, *system.work.generate (open1->hash ())));
	node1.process_active (send3);
	auto send4 (std::make_shared<futurehead::state_block> (key1.pub, send3->hash (), key1.pub, 0, key3.pub, key1.prv, key1.pub, *system.work.generate (send3->hash ())));
	node1.process_active (send4);
	ASSERT_EQ (node1.ledger.epoch_signer (node1.ledger.epoch_link (futurehead::epoch::epoch_1)), futurehead::test_genesis_key.pub);
	auto open_epoch1 (std::make_shared<futurehead::state_block> (key2.pub, 0, 0, 0, node1.ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (key2.pub)));
	node1.process_active (open_epoch1);
	auto receive2 (std::make_shared<futurehead::state_block> (key2.pub, open_epoch1->hash (), 0, futurehead::xrb_ratio, send3->hash (), key2.prv, key2.pub, *system.work.generate (open_epoch1->hash ())));
	node1.process_active (receive2);
	auto open2 (std::make_shared<futurehead::state_block> (key3.pub, 0, key3.pub, futurehead::xrb_ratio, send4->hash (), key3.prv, key3.pub, *system.work.generate (key3.pub)));
	node1.process_active (open2);
	auto change1 (std::make_shared<futurehead::state_block> (key3.pub, open2->hash (), futurehead::test_genesis_key.pub, futurehead::xrb_ratio, 0, key3.prv, key3.pub, *system.work.generate (open2->hash ())));
	node1.process_active (change1);
	futurehead::keypair key4;
	auto send5 (std::make_shared<futurehead::state_block> (key3.pub, change1->hash (), futurehead::test_genesis_key.pub, 0, key4.pub, key3.prv, key3.pub, *system.work.generate (change1->hash ()))); // Pending for open epoch block
	node1.process_active (send5);
	futurehead::blocks_confirm (node1, { send1, send2, receive1, open1, send3, send4, open_epoch1, receive2, open2, change1, send5 });
	system.deadline_set (3s);
	while (node1.active.size () != 11)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	std::unordered_map<futurehead::block_hash, double> adjusted_multipliers;
	{
		futurehead::lock_guard<std::mutex> guard (node1.active.mutex);
		node1.active.update_adjusted_multiplier ();
		ASSERT_EQ (node1.active.roots.get<1> ().begin ()->election->status.winner->hash (), send1->hash ());
		for (auto i (node1.active.roots.get<1> ().begin ()), n (node1.active.roots.get<1> ().end ()); i != n; ++i)
		{
			adjusted_multipliers.insert (std::make_pair (i->election->status.winner->hash (), i->adjusted_multiplier));
		}
	}
	// genesis
	ASSERT_GT (adjusted_multipliers.find (send1->hash ())->second, adjusted_multipliers.find (send2->hash ())->second);
	ASSERT_GT (adjusted_multipliers.find (send2->hash ())->second, adjusted_multipliers.find (receive1->hash ())->second);
	// key1
	ASSERT_GT (adjusted_multipliers.find (send1->hash ())->second, adjusted_multipliers.find (open1->hash ())->second);
	ASSERT_GT (adjusted_multipliers.find (open1->hash ())->second, adjusted_multipliers.find (send3->hash ())->second);
	ASSERT_GT (adjusted_multipliers.find (send3->hash ())->second, adjusted_multipliers.find (send4->hash ())->second);
	//key2
	ASSERT_GT (adjusted_multipliers.find (send3->hash ())->second, adjusted_multipliers.find (receive2->hash ())->second);
	ASSERT_GT (adjusted_multipliers.find (open_epoch1->hash ())->second, adjusted_multipliers.find (receive2->hash ())->second);
	// key3
	ASSERT_GT (adjusted_multipliers.find (send4->hash ())->second, adjusted_multipliers.find (open2->hash ())->second);
	ASSERT_GT (adjusted_multipliers.find (open2->hash ())->second, adjusted_multipliers.find (change1->hash ())->second);
	ASSERT_GT (adjusted_multipliers.find (change1->hash ())->second, adjusted_multipliers.find (send5->hash ())->second);
	// Independent elections can have higher difficulty than adjusted tree
	auto open_epoch2 (std::make_shared<futurehead::state_block> (key4.pub, 0, 0, 0, node1.ledger.epoch_link (futurehead::epoch::epoch_1), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, *system.work.generate (key4.pub, futurehead::difficulty::from_multiplier ((adjusted_multipliers.find (send1->hash ())->second), node1.network_params.network.publish_thresholds.base))));
	ASSERT_GT (open_epoch2->difficulty (), futurehead::difficulty::from_multiplier ((adjusted_multipliers.find (send1->hash ())->second), node1.network_params.network.publish_thresholds.base));
	node1.process_active (open_epoch2);
	node1.block_processor.flush ();
	node1.block_confirm (open_epoch2);
	system.deadline_set (3s);
	while (node1.active.size () != 12)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	{
		futurehead::lock_guard<std::mutex> guard (node1.active.mutex);
		node1.active.update_adjusted_multiplier ();
		ASSERT_EQ (node1.active.roots.size (), 12);
		ASSERT_EQ (node1.active.roots.get<1> ().begin ()->election->status.winner->hash (), open_epoch2->hash ());
	}
}
