#pragma once

#include <futurehead/ipc_flatbuffers_lib/generated/flatbuffers/futureheadapi_generated.h>

#include <memory>

namespace futurehead
{
class amount;
class block;
class send_block;
class receive_block;
class change_block;
class open_block;
class state_block;
namespace ipc
{
	/**
	 * Utilities to convert between blocks and Flatbuffers equivalents
	 */
	class flatbuffers_builder
	{
	public:
		static futureheadapi::BlockUnion block_to_union (futurehead::block const & block_a, futurehead::amount const & amount_a, bool is_state_send_a = false);
		static std::unique_ptr<futureheadapi::BlockStateT> from (futurehead::state_block const & block_a, futurehead::amount const & amount_a, bool is_state_send_a);
		static std::unique_ptr<futureheadapi::BlockSendT> from (futurehead::send_block const & block_a);
		static std::unique_ptr<futureheadapi::BlockReceiveT> from (futurehead::receive_block const & block_a);
		static std::unique_ptr<futureheadapi::BlockOpenT> from (futurehead::open_block const & block_a);
		static std::unique_ptr<futureheadapi::BlockChangeT> from (futurehead::change_block const & block_a);
	};
}
}
