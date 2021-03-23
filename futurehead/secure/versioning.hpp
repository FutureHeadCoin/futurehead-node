#pragma once

#include <futurehead/lib/blocks.hpp>
#include <futurehead/secure/common.hpp>

struct MDB_val;

namespace futurehead
{
class account_info_v1 final
{
public:
	account_info_v1 () = default;
	explicit account_info_v1 (MDB_val const &);
	account_info_v1 (futurehead::block_hash const &, futurehead::block_hash const &, futurehead::amount const &, uint64_t);
	futurehead::block_hash head{ 0 };
	futurehead::block_hash rep_block{ 0 };
	futurehead::amount balance{ 0 };
	uint64_t modified{ 0 };
};
class pending_info_v3 final
{
public:
	pending_info_v3 () = default;
	explicit pending_info_v3 (MDB_val const &);
	pending_info_v3 (futurehead::account const &, futurehead::amount const &, futurehead::account const &);
	futurehead::account source{ 0 };
	futurehead::amount amount{ 0 };
	futurehead::account destination{ 0 };
};
class pending_info_v14 final
{
public:
	pending_info_v14 () = default;
	pending_info_v14 (futurehead::account const &, futurehead::amount const &, futurehead::epoch);
	size_t db_size () const;
	bool deserialize (futurehead::stream &);
	bool operator== (futurehead::pending_info_v14 const &) const;
	futurehead::account source{ 0 };
	futurehead::amount amount{ 0 };
	futurehead::epoch epoch{ futurehead::epoch::epoch_0 };
};
class account_info_v5 final
{
public:
	account_info_v5 () = default;
	explicit account_info_v5 (MDB_val const &);
	account_info_v5 (futurehead::block_hash const &, futurehead::block_hash const &, futurehead::block_hash const &, futurehead::amount const &, uint64_t);
	futurehead::block_hash head{ 0 };
	futurehead::block_hash rep_block{ 0 };
	futurehead::block_hash open_block{ 0 };
	futurehead::amount balance{ 0 };
	uint64_t modified{ 0 };
};
class account_info_v13 final
{
public:
	account_info_v13 () = default;
	account_info_v13 (futurehead::block_hash const &, futurehead::block_hash const &, futurehead::block_hash const &, futurehead::amount const &, uint64_t, uint64_t, futurehead::epoch);
	size_t db_size () const;
	futurehead::block_hash head{ 0 };
	futurehead::block_hash rep_block{ 0 };
	futurehead::block_hash open_block{ 0 };
	futurehead::amount balance{ 0 };
	uint64_t modified{ 0 };
	uint64_t block_count{ 0 };
	futurehead::epoch epoch{ futurehead::epoch::epoch_0 };
};
class account_info_v14 final
{
public:
	account_info_v14 () = default;
	account_info_v14 (futurehead::block_hash const &, futurehead::block_hash const &, futurehead::block_hash const &, futurehead::amount const &, uint64_t, uint64_t, uint64_t, futurehead::epoch);
	size_t db_size () const;
	futurehead::block_hash head{ 0 };
	futurehead::block_hash rep_block{ 0 };
	futurehead::block_hash open_block{ 0 };
	futurehead::amount balance{ 0 };
	uint64_t modified{ 0 };
	uint64_t block_count{ 0 };
	uint64_t confirmation_height{ 0 };
	futurehead::epoch epoch{ futurehead::epoch::epoch_0 };
};
class block_sideband_v14 final
{
public:
	block_sideband_v14 () = default;
	block_sideband_v14 (futurehead::block_type, futurehead::account const &, futurehead::block_hash const &, futurehead::amount const &, uint64_t, uint64_t);
	void serialize (futurehead::stream &) const;
	bool deserialize (futurehead::stream &);
	static size_t size (futurehead::block_type);
	futurehead::block_type type{ futurehead::block_type::invalid };
	futurehead::block_hash successor{ 0 };
	futurehead::account account{ 0 };
	futurehead::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
};
class state_block_w_sideband_v14
{
public:
	std::shared_ptr<futurehead::state_block> state_block;
	futurehead::block_sideband_v14 sideband;
};
}
