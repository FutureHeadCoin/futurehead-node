#include <futurehead/lib/blockbuilders.hpp>

#include <crypto/cryptopp/osrng.h>

#include <unordered_map>

namespace
{
template <typename BLOCKTYPE>
void previous_hex_impl (std::string const & previous_hex, std::error_code & ec, BLOCKTYPE & block)
{
	futurehead::block_hash previous;
	if (!previous.decode_hex (previous_hex))
	{
		block->hashables.previous = previous;
	}
	else
	{
		ec = futurehead::error_common::bad_previous;
	}
}

template <typename BLOCKTYPE>
void account_hex_impl (std::string const & account_hex, std::error_code & ec, BLOCKTYPE & block)
{
	futurehead::account account;
	if (!account.decode_hex (account_hex))
	{
		block->hashables.account = account;
	}
	else
	{
		ec = futurehead::error_common::bad_account_number;
	}
}

template <typename BLOCKTYPE>
void account_address_impl (std::string const & address, std::error_code & ec, BLOCKTYPE & block)
{
	futurehead::account account;
	if (!account.decode_account (address))
	{
		block->hashables.account = account;
	}
	else
	{
		ec = futurehead::error_common::bad_account_number;
	}
}

template <typename BLOCKTYPE>
void representative_hex_impl (std::string const & account_hex, std::error_code & ec, BLOCKTYPE & block)
{
	futurehead::account account;
	if (!account.decode_hex (account_hex))
	{
		block->hashables.representative = account;
	}
	else
	{
		ec = futurehead::error_common::bad_representative_number;
	}
}

template <typename BLOCKTYPE>
void representative_address_impl (std::string const & address, std::error_code & ec, BLOCKTYPE & block)
{
	futurehead::account account;
	if (!account.decode_account (address))
	{
		block->hashables.representative = account;
	}
	else
	{
		ec = futurehead::error_common::bad_representative_number;
	}
}

template <typename BLOCKTYPE>
void destination_hex_impl (std::string const & account_hex, std::error_code & ec, BLOCKTYPE & block)
{
	futurehead::account account;
	if (!account.decode_hex (account_hex))
	{
		block->hashables.destination = account;
	}
	else
	{
		ec = futurehead::error_common::bad_account_number;
	}
}

template <typename BLOCKTYPE>
void destination_address_impl (std::string const & address, std::error_code & ec, BLOCKTYPE & block)
{
	futurehead::account account;
	if (!account.decode_account (address))
	{
		block->hashables.destination = account;
	}
	else
	{
		ec = futurehead::error_common::bad_account_number;
	}
}

template <typename BLOCKTYPE>
void source_hex_impl (std::string const & source_hex, std::error_code & ec, BLOCKTYPE & block)
{
	futurehead::block_hash source;
	if (!source.decode_hex (source_hex))
	{
		block->hashables.source = source;
	}
	else
	{
		ec = futurehead::error_common::bad_source;
	}
}

template <typename BLOCKTYPE>
void balance_dec_impl (std::string const & balance_decimal, std::error_code & ec, BLOCKTYPE & block)
{
	futurehead::amount balance;
	if (!balance.decode_dec (balance_decimal))
	{
		block->hashables.balance = balance;
	}
	else
	{
		ec = futurehead::error_common::bad_balance;
	}
}

template <typename BLOCKTYPE>
void balance_hex_impl (std::string const & balance_hex, std::error_code & ec, BLOCKTYPE & block)
{
	futurehead::amount balance;
	if (!balance.decode_hex (balance_hex))
	{
		block->hashables.balance = balance;
	}
	else
	{
		ec = futurehead::error_common::bad_balance;
	}
}

/* The cost of looking up the error_code map is only taken if field-presence checks fail */
std::unordered_map<uint8_t, std::error_code> ec_map = {
	{ static_cast<uint8_t> (futurehead::build_flags::account_present), futurehead::error_common::missing_account },
	{ static_cast<uint8_t> (futurehead::build_flags::balance_present), futurehead::error_common::missing_balance },
	{ static_cast<uint8_t> (futurehead::build_flags::link_present), futurehead::error_common::missing_link },
	{ static_cast<uint8_t> (futurehead::build_flags::previous_present), futurehead::error_common::missing_previous },
	{ static_cast<uint8_t> (futurehead::build_flags::representative_present), futurehead::error_common::missing_representative },
	{ static_cast<uint8_t> (futurehead::build_flags::signature_present), futurehead::error_common::missing_signature },
	{ static_cast<uint8_t> (futurehead::build_flags::work_present), futurehead::error_common::missing_work }
};

/** Find first set bit as a mask, e.g. 10101000 => 0x08. Returns -1 if no bit is set. */
inline signed ffs_mask (uint8_t num)
{
	for (signed i = 0; i < 8; i++)
	{
		if ((num >> i) & 1)
		{
			return 1 << i;
		}
	}
	return -1;
}

/**
 * Check if \p build_state contains all the flags in \p block_all_flags.
 * If not, return the corresponding futurehead::error_common::missing_<...> value.
 */
std::error_code check_fields_set (uint8_t block_all_flags, uint8_t build_state)
{
	std::error_code ec;

	// Figure out which fields are not set. Note that static typing ensures we cannot set values
	// not applicable to a given block type, we can only forget to set fields. This will be zero
	// if all fields are set and thus succeed.
	uint8_t res = block_all_flags ^ build_state;
	if (res)
	{
		// Convert the first bit set to a field mask and look up the error code.
		auto build_flags_mask = static_cast<uint8_t> (ffs_mask (res));
		debug_assert (ec_map.find (build_flags_mask) != ec_map.end ());
		ec = ec_map[build_flags_mask];
	}
	return ec;
}
} // anonymous namespace

futurehead::state_block_builder::state_block_builder ()
{
	make_block ();
}

futurehead::state_block_builder & futurehead::state_block_builder::make_block ()
{
	construct_block ();
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::from (futurehead::state_block const & other_block)
{
	block->work = other_block.work;
	build_state |= build_flags::work_present;
	block->signature = other_block.signature;
	build_state |= build_flags::signature_present;
	block->hashables.account = other_block.hashables.account;
	build_state |= build_flags::account_present;
	block->hashables.balance = other_block.hashables.balance;
	build_state |= build_flags::balance_present;
	block->hashables.link = other_block.hashables.link;
	build_state |= build_flags::link_present;
	block->hashables.previous = other_block.hashables.previous;
	build_state |= build_flags::previous_present;
	block->hashables.representative = other_block.hashables.representative;
	build_state |= build_flags::representative_present;
	return *this;
}

void futurehead::state_block_builder::validate ()
{
	if (!ec)
	{
		ec = check_fields_set (required_fields, build_state);
	}
}

futurehead::state_block_builder & futurehead::state_block_builder::zero ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.account.clear ();
	block->hashables.balance.clear ();
	block->hashables.link.clear ();
	block->hashables.previous.clear ();
	block->hashables.representative.clear ();
	build_state = required_fields;
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::account (futurehead::account const & account)
{
	block->hashables.account = account;
	build_state |= build_flags::account_present;
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::account_hex (std::string const & account_hex)
{
	account_hex_impl (account_hex, ec, block);
	build_state |= build_flags::account_present;
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::account_address (std::string const & address)
{
	account_address_impl (address, ec, block);
	build_state |= build_flags::account_present;
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::representative (futurehead::account const & account)
{
	block->hashables.representative = account;
	build_state |= build_flags::representative_present;
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::representative_hex (std::string const & account_hex)
{
	representative_hex_impl (account_hex, ec, block);
	build_state |= build_flags::representative_present;
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::representative_address (std::string const & address)
{
	representative_address_impl (address, ec, block);
	build_state |= build_flags::representative_present;
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::previous (futurehead::block_hash const & previous)
{
	block->hashables.previous = previous;
	build_state |= build_flags::previous_present;
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::previous_hex (std::string const & previous_hex)
{
	previous_hex_impl (previous_hex, ec, block);
	build_state |= build_flags::previous_present;
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::balance (futurehead::amount const & balance)
{
	block->hashables.balance = balance;
	build_state |= build_flags::balance_present;
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::balance_dec (std::string const & balance_decimal)
{
	balance_dec_impl (balance_decimal, ec, block);
	build_state |= build_flags::balance_present;
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::balance_hex (std::string const & balance_hex)
{
	balance_hex_impl (balance_hex, ec, block);
	build_state |= build_flags::balance_present;
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::link (futurehead::link const & link)
{
	block->hashables.link = link;
	build_state |= build_flags::link_present;
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::link_hex (std::string const & link_hex)
{
	futurehead::link link;
	if (!link.decode_hex (link_hex))
	{
		block->hashables.link = link;
		build_state |= build_flags::link_present;
	}
	else
	{
		ec = futurehead::error_common::bad_link;
	}
	return *this;
}

futurehead::state_block_builder & futurehead::state_block_builder::link_address (std::string const & link_address)
{
	futurehead::link link;
	if (!link.decode_account (link_address))
	{
		block->hashables.link = link;
		build_state |= build_flags::link_present;
	}
	else
	{
		ec = futurehead::error_common::bad_link;
	}
	return *this;
}

futurehead::open_block_builder::open_block_builder ()
{
	make_block ();
}

futurehead::open_block_builder & futurehead::open_block_builder::make_block ()
{
	construct_block ();
	return *this;
}

void futurehead::open_block_builder::validate ()
{
	if (!ec)
	{
		ec = check_fields_set (required_fields, build_state);
	}
}

futurehead::open_block_builder & futurehead::open_block_builder::zero ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.account.clear ();
	block->hashables.representative.clear ();
	block->hashables.source.clear ();
	build_state = required_fields;
	return *this;
}

futurehead::open_block_builder & futurehead::open_block_builder::account (futurehead::account account)
{
	block->hashables.account = account;
	build_state |= build_flags::account_present;
	return *this;
}

futurehead::open_block_builder & futurehead::open_block_builder::account_hex (std::string account_hex)
{
	account_hex_impl (account_hex, ec, block);
	build_state |= build_flags::account_present;
	return *this;
}

futurehead::open_block_builder & futurehead::open_block_builder::account_address (std::string address)
{
	account_address_impl (address, ec, block);
	build_state |= build_flags::account_present;
	return *this;
}

futurehead::open_block_builder & futurehead::open_block_builder::representative (futurehead::account account)
{
	block->hashables.representative = account;
	build_state |= build_flags::representative_present;
	return *this;
}

futurehead::open_block_builder & futurehead::open_block_builder::representative_hex (std::string account_hex)
{
	representative_hex_impl (account_hex, ec, block);
	build_state |= build_flags::representative_present;
	return *this;
}

futurehead::open_block_builder & futurehead::open_block_builder::representative_address (std::string address)
{
	representative_address_impl (address, ec, block);
	build_state |= build_flags::representative_present;
	return *this;
}

futurehead::open_block_builder & futurehead::open_block_builder::source (futurehead::block_hash source)
{
	block->hashables.source = source;
	build_state |= build_flags::link_present;
	return *this;
}

futurehead::open_block_builder & futurehead::open_block_builder::source_hex (std::string source_hex)
{
	source_hex_impl (source_hex, ec, block);
	build_state |= build_flags::link_present;
	return *this;
}

futurehead::change_block_builder::change_block_builder ()
{
	make_block ();
}

futurehead::change_block_builder & futurehead::change_block_builder::make_block ()
{
	construct_block ();
	return *this;
}

void futurehead::change_block_builder::validate ()
{
	if (!ec)
	{
		ec = check_fields_set (required_fields, build_state);
	}
}

futurehead::change_block_builder & futurehead::change_block_builder::zero ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.previous.clear ();
	block->hashables.representative.clear ();
	build_state = required_fields;
	return *this;
}

futurehead::change_block_builder & futurehead::change_block_builder::representative (futurehead::account account)
{
	block->hashables.representative = account;
	build_state |= build_flags::representative_present;
	return *this;
}

futurehead::change_block_builder & futurehead::change_block_builder::representative_hex (std::string account_hex)
{
	representative_hex_impl (account_hex, ec, block);
	build_state |= build_flags::representative_present;
	return *this;
}

futurehead::change_block_builder & futurehead::change_block_builder::representative_address (std::string address)
{
	representative_address_impl (address, ec, block);
	build_state |= build_flags::representative_present;
	return *this;
}

futurehead::change_block_builder & futurehead::change_block_builder::previous (futurehead::block_hash previous)
{
	block->hashables.previous = previous;
	build_state |= build_flags::previous_present;
	return *this;
}

futurehead::change_block_builder & futurehead::change_block_builder::previous_hex (std::string previous_hex)
{
	previous_hex_impl (previous_hex, ec, block);
	build_state |= build_flags::previous_present;
	return *this;
}

futurehead::send_block_builder::send_block_builder ()
{
	make_block ();
}

futurehead::send_block_builder & futurehead::send_block_builder::make_block ()
{
	construct_block ();
	return *this;
}

void futurehead::send_block_builder::validate ()
{
	if (!ec)
	{
		ec = check_fields_set (required_fields, build_state);
	}
}

futurehead::send_block_builder & futurehead::send_block_builder::zero ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.previous.clear ();
	block->hashables.destination.clear ();
	block->hashables.balance.clear ();
	build_state = required_fields;
	return *this;
}

futurehead::send_block_builder & futurehead::send_block_builder::destination (futurehead::account account)
{
	block->hashables.destination = account;
	build_state |= build_flags::link_present;
	return *this;
}

futurehead::send_block_builder & futurehead::send_block_builder::destination_hex (std::string account_hex)
{
	destination_hex_impl (account_hex, ec, block);
	build_state |= build_flags::link_present;
	return *this;
}

futurehead::send_block_builder & futurehead::send_block_builder::destination_address (std::string address)
{
	destination_address_impl (address, ec, block);
	build_state |= build_flags::link_present;
	return *this;
}

futurehead::send_block_builder & futurehead::send_block_builder::previous (futurehead::block_hash previous)
{
	block->hashables.previous = previous;
	build_state |= build_flags::previous_present;
	return *this;
}

futurehead::send_block_builder & futurehead::send_block_builder::previous_hex (std::string previous_hex)
{
	previous_hex_impl (previous_hex, ec, block);
	build_state |= build_flags::previous_present;
	return *this;
}

futurehead::send_block_builder & futurehead::send_block_builder::balance (futurehead::amount balance)
{
	block->hashables.balance = balance;
	build_state |= build_flags::balance_present;
	return *this;
}

futurehead::send_block_builder & futurehead::send_block_builder::balance_dec (std::string balance_decimal)
{
	balance_dec_impl (balance_decimal, ec, block);
	build_state |= build_flags::balance_present;
	return *this;
}

futurehead::send_block_builder & futurehead::send_block_builder::balance_hex (std::string balance_hex)
{
	balance_hex_impl (balance_hex, ec, block);
	build_state |= build_flags::balance_present;
	return *this;
}

futurehead::receive_block_builder::receive_block_builder ()
{
	make_block ();
}

futurehead::receive_block_builder & futurehead::receive_block_builder::make_block ()
{
	construct_block ();
	return *this;
}

void futurehead::receive_block_builder::validate ()
{
	if (!ec)
	{
		ec = check_fields_set (required_fields, build_state);
	}
}

futurehead::receive_block_builder & futurehead::receive_block_builder::zero ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.previous.clear ();
	block->hashables.source.clear ();
	build_state = required_fields;
	return *this;
}

futurehead::receive_block_builder & futurehead::receive_block_builder::previous (futurehead::block_hash previous)
{
	block->hashables.previous = previous;
	build_state |= build_flags::previous_present;
	return *this;
}

futurehead::receive_block_builder & futurehead::receive_block_builder::previous_hex (std::string previous_hex)
{
	previous_hex_impl (previous_hex, ec, block);
	build_state |= build_flags::previous_present;
	return *this;
}

futurehead::receive_block_builder & futurehead::receive_block_builder::source (futurehead::block_hash source)
{
	block->hashables.source = source;
	build_state |= build_flags::link_present;
	return *this;
}

futurehead::receive_block_builder & futurehead::receive_block_builder::source_hex (std::string source_hex)
{
	source_hex_impl (source_hex, ec, block);
	build_state |= build_flags::link_present;
	return *this;
}

template <typename BLOCKTYPE, typename BUILDER>
std::unique_ptr<BLOCKTYPE> futurehead::abstract_builder<BLOCKTYPE, BUILDER>::build ()
{
	if (!ec)
	{
		static_cast<BUILDER *> (this)->validate ();
	}
	debug_assert (!ec);
	return std::move (block);
}

template <typename BLOCKTYPE, typename BUILDER>
std::unique_ptr<BLOCKTYPE> futurehead::abstract_builder<BLOCKTYPE, BUILDER>::build (std::error_code & ec)
{
	if (!this->ec)
	{
		static_cast<BUILDER *> (this)->validate ();
	}
	ec = this->ec;
	return std::move (block);
}

template <typename BLOCKTYPE, typename BUILDER>
futurehead::abstract_builder<BLOCKTYPE, BUILDER> & futurehead::abstract_builder<BLOCKTYPE, BUILDER>::work (uint64_t work)
{
	block->work = work;
	build_state |= build_flags::work_present;
	return *this;
}

template <typename BLOCKTYPE, typename BUILDER>
futurehead::abstract_builder<BLOCKTYPE, BUILDER> & futurehead::abstract_builder<BLOCKTYPE, BUILDER>::sign (futurehead::raw_key const & private_key, futurehead::public_key const & public_key)
{
	block->signature = futurehead::sign_message (private_key, public_key, block->hash ());
	build_state |= build_flags::signature_present;
	return *this;
}

template <typename BLOCKTYPE, typename BUILDER>
futurehead::abstract_builder<BLOCKTYPE, BUILDER> & futurehead::abstract_builder<BLOCKTYPE, BUILDER>::sign_zero ()
{
	block->signature.clear ();
	build_state |= build_flags::signature_present;
	return *this;
}

template <typename BLOCKTYPE, typename BUILDER>
void futurehead::abstract_builder<BLOCKTYPE, BUILDER>::construct_block ()
{
	block = std::make_unique<BLOCKTYPE> ();
	ec.clear ();
	build_state = 0;
}

// Explicit instantiations
template class futurehead::abstract_builder<futurehead::open_block, futurehead::open_block_builder>;
template class futurehead::abstract_builder<futurehead::send_block, futurehead::send_block_builder>;
template class futurehead::abstract_builder<futurehead::receive_block, futurehead::receive_block_builder>;
template class futurehead::abstract_builder<futurehead::change_block, futurehead::change_block_builder>;
template class futurehead::abstract_builder<futurehead::state_block, futurehead::state_block_builder>;
