#include <futurehead/lib/epoch.hpp>
#include <futurehead/secure/common.hpp>

#include <gtest/gtest.h>

TEST (epochs, is_epoch_link)
{
	futurehead::epochs epochs;
	// Test epoch 1
	futurehead::keypair key1;
	auto link1 = 42;
	auto link2 = 43;
	ASSERT_FALSE (epochs.is_epoch_link (link1));
	ASSERT_FALSE (epochs.is_epoch_link (link2));
	epochs.add (futurehead::epoch::epoch_1, key1.pub, link1);
	ASSERT_TRUE (epochs.is_epoch_link (link1));
	ASSERT_FALSE (epochs.is_epoch_link (link2));
	ASSERT_EQ (key1.pub, epochs.signer (futurehead::epoch::epoch_1));
	ASSERT_EQ (epochs.epoch (link1), futurehead::epoch::epoch_1);

	// Test epoch 2
	futurehead::keypair key2;
	epochs.add (futurehead::epoch::epoch_2, key2.pub, link2);
	ASSERT_TRUE (epochs.is_epoch_link (link2));
	ASSERT_EQ (key2.pub, epochs.signer (futurehead::epoch::epoch_2));
	ASSERT_EQ (futurehead::uint256_union (link1), epochs.link (futurehead::epoch::epoch_1));
	ASSERT_EQ (futurehead::uint256_union (link2), epochs.link (futurehead::epoch::epoch_2));
	ASSERT_EQ (epochs.epoch (link2), futurehead::epoch::epoch_2);
}

TEST (epochs, is_sequential)
{
	ASSERT_TRUE (futurehead::epochs::is_sequential (futurehead::epoch::epoch_0, futurehead::epoch::epoch_1));
	ASSERT_TRUE (futurehead::epochs::is_sequential (futurehead::epoch::epoch_1, futurehead::epoch::epoch_2));

	ASSERT_FALSE (futurehead::epochs::is_sequential (futurehead::epoch::epoch_0, futurehead::epoch::epoch_2));
	ASSERT_FALSE (futurehead::epochs::is_sequential (futurehead::epoch::epoch_0, futurehead::epoch::invalid));
	ASSERT_FALSE (futurehead::epochs::is_sequential (futurehead::epoch::unspecified, futurehead::epoch::epoch_1));
	ASSERT_FALSE (futurehead::epochs::is_sequential (futurehead::epoch::epoch_1, futurehead::epoch::epoch_0));
	ASSERT_FALSE (futurehead::epochs::is_sequential (futurehead::epoch::epoch_2, futurehead::epoch::epoch_0));
	ASSERT_FALSE (futurehead::epochs::is_sequential (futurehead::epoch::epoch_2, futurehead::epoch::epoch_2));
}
