#pragma once

#include <futurehead/lib/numbers.hpp>
#include <futurehead/secure/blockstore.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace futurehead
{
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (futurehead::db_val<MDB_val> const &);
	wallet_value (futurehead::uint256_union const &, uint64_t);
	futurehead::db_val<MDB_val> val () const;
	futurehead::uint256_union key;
	uint64_t work;
};
}
