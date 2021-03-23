#include <futurehead/core_test/testutil.hpp>
#include <futurehead/node/common.hpp>
#include <futurehead/node/testing.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace futurehead
{
void force_futurehead_test_network ();
}
namespace
{
std::shared_ptr<futurehead::system> system0;
std::shared_ptr<futurehead::node> node0;

class fuzz_visitor : public futurehead::message_visitor
{
public:
	virtual void keepalive (futurehead::keepalive const &) override
	{
	}
	virtual void publish (futurehead::publish const &) override
	{
	}
	virtual void confirm_req (futurehead::confirm_req const &) override
	{
	}
	virtual void confirm_ack (futurehead::confirm_ack const &) override
	{
	}
	virtual void bulk_pull (futurehead::bulk_pull const &) override
	{
	}
	virtual void bulk_pull_account (futurehead::bulk_pull_account const &) override
	{
	}
	virtual void bulk_push (futurehead::bulk_push const &) override
	{
	}
	virtual void frontier_req (futurehead::frontier_req const &) override
	{
	}
	virtual void node_id_handshake (futurehead::node_id_handshake const &) override
	{
	}
	virtual void telemetry_req (futurehead::telemetry_req const &) override
	{
	}
	virtual void telemetry_ack (futurehead::telemetry_ack const &) override
	{
	}
};
}

/** Fuzz live message parsing. This covers parsing and block/vote uniquing. */
void fuzz_message_parser (const uint8_t * Data, size_t Size)
{
	static bool initialized = false;
	if (!initialized)
	{
		futurehead::force_futurehead_test_network ();
		initialized = true;
		system0 = std::make_shared<futurehead::system> (1);
		node0 = system0->nodes[0];
	}

	fuzz_visitor visitor;
	futurehead::message_parser parser (node0->network.publish_filter, node0->block_uniquer, node0->vote_uniquer, visitor, node0->work);
	parser.deserialize_buffer (Data, Size);
}

/** Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput (const uint8_t * Data, size_t Size)
{
	fuzz_message_parser (Data, Size);
	return 0;
}
