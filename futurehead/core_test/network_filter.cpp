#include <futurehead/core_test/testutil.hpp>
#include <futurehead/node/common.hpp>
#include <futurehead/secure/buffer.hpp>
#include <futurehead/secure/common.hpp>
#include <futurehead/secure/network_filter.hpp>

#include <gtest/gtest.h>

TEST (network_filter, unit)
{
	futurehead::genesis genesis;
	futurehead::network_filter filter (1);
	auto one_block = [&filter](std::shared_ptr<futurehead::block> const & block_a, bool expect_duplicate_a) {
		futurehead::publish message (block_a);
		auto bytes (message.to_bytes (false));
		futurehead::bufferstream stream (bytes->data (), bytes->size ());

		// First read the header
		bool error{ false };
		futurehead::message_header header (error, stream);
		ASSERT_FALSE (error);

		// This validates futurehead::message_header::size
		ASSERT_EQ (bytes->size (), block_a->size (block_a->type ()) + header.size);

		// Now filter the rest of the stream
		bool duplicate (filter.apply (bytes->data (), bytes->size () - header.size));
		ASSERT_EQ (expect_duplicate_a, duplicate);

		// Make sure the stream was rewinded correctly
		auto block (futurehead::deserialize_block (stream, header.block_type ()));
		ASSERT_NE (nullptr, block);
		ASSERT_EQ (*block, *block_a);
	};
	one_block (genesis.open, false);
	for (int i = 0; i < 10; ++i)
	{
		one_block (genesis.open, true);
	}
	auto new_block (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.open->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - 10 * futurehead::xrb_ratio, futurehead::public_key (), futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));
	one_block (new_block, false);
	for (int i = 0; i < 10; ++i)
	{
		one_block (new_block, true);
	}
	for (int i = 0; i < 100; ++i)
	{
		one_block (genesis.open, false);
		one_block (new_block, false);
	}
}

TEST (network_filter, many)
{
	futurehead::genesis genesis;
	futurehead::network_filter filter (4);
	futurehead::keypair key1;
	for (int i = 0; i < 100; ++i)
	{
		auto block (std::make_shared<futurehead::state_block> (futurehead::test_genesis_key.pub, genesis.open->hash (), futurehead::test_genesis_key.pub, futurehead::genesis_amount - i * 10 * futurehead::xrb_ratio, key1.pub, futurehead::test_genesis_key.prv, futurehead::test_genesis_key.pub, 0));

		futurehead::publish message (block);
		auto bytes (message.to_bytes (false));
		futurehead::bufferstream stream (bytes->data (), bytes->size ());

		// First read the header
		bool error{ false };
		futurehead::message_header header (error, stream);
		ASSERT_FALSE (error);

		// This validates futurehead::message_header::size
		ASSERT_EQ (bytes->size (), block->size + header.size);

		// Now filter the rest of the stream
		// All blocks should pass through
		ASSERT_FALSE (filter.apply (bytes->data (), block->size));
		ASSERT_FALSE (error);

		// Make sure the stream was rewinded correctly
		auto deserialized_block (futurehead::deserialize_block (stream, header.block_type ()));
		ASSERT_NE (nullptr, deserialized_block);
		ASSERT_EQ (*block, *deserialized_block);
	}
}

TEST (network_filter, clear)
{
	futurehead::network_filter filter (1);
	std::vector<uint8_t> bytes1{ 1, 2, 3 };
	std::vector<uint8_t> bytes2{ 1 };
	ASSERT_FALSE (filter.apply (bytes1.data (), bytes1.size ()));
	ASSERT_TRUE (filter.apply (bytes1.data (), bytes1.size ()));
	filter.clear (bytes1.data (), bytes1.size ());
	ASSERT_FALSE (filter.apply (bytes1.data (), bytes1.size ()));
	ASSERT_TRUE (filter.apply (bytes1.data (), bytes1.size ()));
	filter.clear (bytes2.data (), bytes2.size ());
	ASSERT_TRUE (filter.apply (bytes1.data (), bytes1.size ()));
	ASSERT_FALSE (filter.apply (bytes2.data (), bytes2.size ()));
}

TEST (network_filter, optional_digest)
{
	futurehead::network_filter filter (1);
	std::vector<uint8_t> bytes1{ 1, 2, 3 };
	futurehead::uint128_t digest{ 0 };
	ASSERT_FALSE (filter.apply (bytes1.data (), bytes1.size (), &digest));
	ASSERT_NE (0, digest);
	ASSERT_TRUE (filter.apply (bytes1.data (), bytes1.size ()));
	filter.clear (digest);
	ASSERT_FALSE (filter.apply (bytes1.data (), bytes1.size ()));
}
