#include <futurehead/node/lmdb/wallet_value.hpp>

futurehead::wallet_value::wallet_value (futurehead::db_val<MDB_val> const & val_a)
{
	debug_assert (val_a.size () == sizeof (*this));
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), key.chars.begin ());
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key) + sizeof (work), reinterpret_cast<char *> (&work));
}

futurehead::wallet_value::wallet_value (futurehead::uint256_union const & key_a, uint64_t work_a) :
key (key_a),
work (work_a)
{
}

futurehead::db_val<MDB_val> futurehead::wallet_value::val () const
{
	static_assert (sizeof (*this) == sizeof (key) + sizeof (work), "Class not packed");
	return futurehead::db_val<MDB_val> (sizeof (*this), const_cast<futurehead::wallet_value *> (this));
}
