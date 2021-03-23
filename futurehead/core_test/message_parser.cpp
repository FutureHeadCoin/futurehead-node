#include <futurehead/node/testing.hpp>

#include <gtest/gtest.h>

namespace
{
class test_visitor : public futurehead::message_visitor
{
public:
	void keepalive (futurehead::keepalive const &) override
	{
		++keepalive_count;
	}
	void publish (futurehead::publish const &) override
	{
		++publish_count;
	}
	void confirm_req (futurehead::confirm_req const &) override
	{
		++confirm_req_count;
	}
	void confirm_ack (futurehead::confirm_ack const &) override
	{
		++confirm_ack_count;
	}
	void bulk_pull (futurehead::bulk_pull const &) override
	{
		ASSERT_FALSE (true);
	}
	void bulk_pull_account (futurehead::bulk_pull_account const &) override
	{
		ASSERT_FALSE (true);
	}
	void bulk_push (futurehead::bulk_push const &) override
	{
		ASSERT_FALSE (true);
	}
	void frontier_req (futurehead::frontier_req const &) override
	{
		ASSERT_FALSE (true);
	}
	void node_id_handshake (futurehead::node_id_handshake const &) override
	{
		ASSERT_FALSE (true);
	}
	void telemetry_req (futurehead::telemetry_req const &) override
	{
		ASSERT_FALSE (true);
	}
	void telemetry_ack (futurehead::telemetry_ack const &) override
	{
		ASSERT_FALSE (true);
	}

	uint64_t keepalive_count{ 0 };
	uint64_t publish_count{ 0 };
	uint64_t confirm_req_count{ 0 };
	uint64_t confirm_ack_count{ 0 };
};
}

TEST (message_parser, exact_confirm_ack_size)
{
	futurehead::system system (1);
	test_visitor visitor;
	futurehead::network_filter filter (1);
	futurehead::block_uniquer block_uniquer;
	futurehead::vote_uniquer vote_uniquer (block_uniquer);
	futurehead::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, false);
	auto block (std::make_shared<futurehead::send_block> (1, 1, 2, futurehead::keypair ().prv, 4, *system.work.generate (futurehead::root (1))));
	auto vote (std::make_shared<futurehead::vote> (0, futurehead::keypair ().prv, 0, std::move (block)));
	futurehead::confirm_ack message (vote);
	std::vector<uint8_t> bytes;
	{
		futurehead::vectorstream stream (bytes);
		message.serialize (stream, true);
	}
	ASSERT_EQ (0, visitor.confirm_ack_count);
	ASSERT_EQ (parser.status, futurehead::message_parser::parse_status::success);
	auto error (false);
	futurehead::bufferstream stream1 (bytes.data (), bytes.size ());
	futurehead::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_ack (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_ack_count);
	ASSERT_EQ (parser.status, futurehead::message_parser::parse_status::success);
	bytes.push_back (0);
	futurehead::bufferstream stream2 (bytes.data (), bytes.size ());
	futurehead::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_ack (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_ack_count);
	ASSERT_NE (parser.status, futurehead::message_parser::parse_status::success);
}

TEST (message_parser, exact_confirm_req_size)
{
	futurehead::system system (1);
	test_visitor visitor;
	futurehead::network_filter filter (1);
	futurehead::block_uniquer block_uniquer;
	futurehead::vote_uniquer vote_uniquer (block_uniquer);
	futurehead::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, false);
	auto block (std::make_shared<futurehead::send_block> (1, 1, 2, futurehead::keypair ().prv, 4, *system.work.generate (futurehead::root (1))));
	futurehead::confirm_req message (std::move (block));
	std::vector<uint8_t> bytes;
	{
		futurehead::vectorstream stream (bytes);
		message.serialize (stream, false);
	}
	ASSERT_EQ (0, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, futurehead::message_parser::parse_status::success);
	auto error (false);
	futurehead::bufferstream stream1 (bytes.data (), bytes.size ());
	futurehead::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, futurehead::message_parser::parse_status::success);
	bytes.push_back (0);
	futurehead::bufferstream stream2 (bytes.data (), bytes.size ());
	futurehead::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_NE (parser.status, futurehead::message_parser::parse_status::success);
}

TEST (message_parser, exact_confirm_req_hash_size)
{
	futurehead::system system (1);
	test_visitor visitor;
	futurehead::network_filter filter (1);
	futurehead::block_uniquer block_uniquer;
	futurehead::vote_uniquer vote_uniquer (block_uniquer);
	futurehead::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, true);
	futurehead::send_block block (1, 1, 2, futurehead::keypair ().prv, 4, *system.work.generate (futurehead::root (1)));
	futurehead::confirm_req message (block.hash (), block.root ());
	std::vector<uint8_t> bytes;
	{
		futurehead::vectorstream stream (bytes);
		message.serialize (stream, false);
	}
	ASSERT_EQ (0, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, futurehead::message_parser::parse_status::success);
	auto error (false);
	futurehead::bufferstream stream1 (bytes.data (), bytes.size ());
	futurehead::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, futurehead::message_parser::parse_status::success);
	bytes.push_back (0);
	futurehead::bufferstream stream2 (bytes.data (), bytes.size ());
	futurehead::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_NE (parser.status, futurehead::message_parser::parse_status::success);
}

TEST (message_parser, exact_publish_size)
{
	futurehead::system system (1);
	test_visitor visitor;
	futurehead::network_filter filter (1);
	futurehead::block_uniquer block_uniquer;
	futurehead::vote_uniquer vote_uniquer (block_uniquer);
	futurehead::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, true);
	auto block (std::make_shared<futurehead::send_block> (1, 1, 2, futurehead::keypair ().prv, 4, *system.work.generate (futurehead::root (1))));
	futurehead::publish message (std::move (block));
	std::vector<uint8_t> bytes;
	{
		futurehead::vectorstream stream (bytes);
		message.serialize (stream, false);
	}
	ASSERT_EQ (0, visitor.publish_count);
	ASSERT_EQ (parser.status, futurehead::message_parser::parse_status::success);
	auto error (false);
	futurehead::bufferstream stream1 (bytes.data (), bytes.size ());
	futurehead::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_publish (stream1, header1);
	ASSERT_EQ (1, visitor.publish_count);
	ASSERT_EQ (parser.status, futurehead::message_parser::parse_status::success);
	bytes.push_back (0);
	futurehead::bufferstream stream2 (bytes.data (), bytes.size ());
	futurehead::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_publish (stream2, header2);
	ASSERT_EQ (1, visitor.publish_count);
	ASSERT_NE (parser.status, futurehead::message_parser::parse_status::success);
}

TEST (message_parser, exact_keepalive_size)
{
	futurehead::system system (1);
	test_visitor visitor;
	futurehead::network_filter filter (1);
	futurehead::block_uniquer block_uniquer;
	futurehead::vote_uniquer vote_uniquer (block_uniquer);
	futurehead::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, true);
	futurehead::keepalive message;
	std::vector<uint8_t> bytes;
	{
		futurehead::vectorstream stream (bytes);
		message.serialize (stream, true);
	}
	ASSERT_EQ (0, visitor.keepalive_count);
	ASSERT_EQ (parser.status, futurehead::message_parser::parse_status::success);
	auto error (false);
	futurehead::bufferstream stream1 (bytes.data (), bytes.size ());
	futurehead::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_keepalive (stream1, header1);
	ASSERT_EQ (1, visitor.keepalive_count);
	ASSERT_EQ (parser.status, futurehead::message_parser::parse_status::success);
	bytes.push_back (0);
	futurehead::bufferstream stream2 (bytes.data (), bytes.size ());
	futurehead::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_keepalive (stream2, header2);
	ASSERT_EQ (1, visitor.keepalive_count);
	ASSERT_NE (parser.status, futurehead::message_parser::parse_status::success);
}
