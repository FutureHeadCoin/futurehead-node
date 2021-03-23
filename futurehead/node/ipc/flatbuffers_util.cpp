#include <futurehead/lib/blocks.hpp>
#include <futurehead/lib/numbers.hpp>
#include <futurehead/node/ipc/flatbuffers_util.hpp>
#include <futurehead/secure/common.hpp>

std::unique_ptr<futureheadapi::BlockStateT> futurehead::ipc::flatbuffers_builder::from (futurehead::state_block const & block_a, futurehead::amount const & amount_a, bool is_state_send_a)
{
	static futurehead::network_params params;
	auto block (std::make_unique<futureheadapi::BlockStateT> ());
	block->account = block_a.account ().to_account ();
	block->hash = block_a.hash ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block->representative = block_a.representative ().to_account ();
	block->balance = block_a.balance ().to_string_dec ();
	block->link = block_a.link ().to_string ();
	block->link_as_account = block_a.link ().to_account ();
	block_a.signature.encode_hex (block->signature);
	block->work = futurehead::to_string_hex (block_a.work);

	if (is_state_send_a)
	{
		block->subtype = futureheadapi::BlockSubType::BlockSubType_send;
	}
	else if (block_a.link ().is_zero ())
	{
		block->subtype = futureheadapi::BlockSubType::BlockSubType_change;
	}
	else if (amount_a == 0 && params.ledger.epochs.is_epoch_link (block_a.link ()))
	{
		block->subtype = futureheadapi::BlockSubType::BlockSubType_epoch;
	}
	else
	{
		block->subtype = futureheadapi::BlockSubType::BlockSubType_receive;
	}
	return block;
}

std::unique_ptr<futureheadapi::BlockSendT> futurehead::ipc::flatbuffers_builder::from (futurehead::send_block const & block_a)
{
	auto block (std::make_unique<futureheadapi::BlockSendT> ());
	block->hash = block_a.hash ().to_string ();
	block->balance = block_a.balance ().to_string_dec ();
	block->destination = block_a.hashables.destination.to_account ();
	block->previous = block_a.previous ().to_string ();
	block_a.signature.encode_hex (block->signature);
	block->work = futurehead::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<futureheadapi::BlockReceiveT> futurehead::ipc::flatbuffers_builder::from (futurehead::receive_block const & block_a)
{
	auto block (std::make_unique<futureheadapi::BlockReceiveT> ());
	block->hash = block_a.hash ().to_string ();
	block->source = block_a.source ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block_a.signature.encode_hex (block->signature);
	block->work = futurehead::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<futureheadapi::BlockOpenT> futurehead::ipc::flatbuffers_builder::from (futurehead::open_block const & block_a)
{
	auto block (std::make_unique<futureheadapi::BlockOpenT> ());
	block->hash = block_a.hash ().to_string ();
	block->source = block_a.source ().to_string ();
	block->account = block_a.account ().to_account ();
	block->representative = block_a.representative ().to_account ();
	block_a.signature.encode_hex (block->signature);
	block->work = futurehead::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<futureheadapi::BlockChangeT> futurehead::ipc::flatbuffers_builder::from (futurehead::change_block const & block_a)
{
	auto block (std::make_unique<futureheadapi::BlockChangeT> ());
	block->hash = block_a.hash ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block->representative = block_a.representative ().to_account ();
	block_a.signature.encode_hex (block->signature);
	block->work = futurehead::to_string_hex (block_a.work);
	return block;
}

futureheadapi::BlockUnion futurehead::ipc::flatbuffers_builder::block_to_union (futurehead::block const & block_a, futurehead::amount const & amount_a, bool is_state_send_a)
{
	futureheadapi::BlockUnion u;
	switch (block_a.type ())
	{
		case futurehead::block_type::state:
		{
			u.Set (*from (dynamic_cast<futurehead::state_block const &> (block_a), amount_a, is_state_send_a));
			break;
		}
		case futurehead::block_type::send:
		{
			u.Set (*from (dynamic_cast<futurehead::send_block const &> (block_a)));
			break;
		}
		case futurehead::block_type::receive:
		{
			u.Set (*from (dynamic_cast<futurehead::receive_block const &> (block_a)));
			break;
		}
		case futurehead::block_type::open:
		{
			u.Set (*from (dynamic_cast<futurehead::open_block const &> (block_a)));
			break;
		}
		case futurehead::block_type::change:
		{
			u.Set (*from (dynamic_cast<futurehead::change_block const &> (block_a)));
			break;
		}

		default:
			debug_assert (false);
	}
	return u;
}
