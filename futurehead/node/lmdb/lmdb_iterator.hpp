#pragma once

#include <futurehead/secure/blockstore.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace futurehead
{
template <typename T, typename U>
class mdb_iterator : public store_iterator_impl<T, U>
{
public:
	mdb_iterator (futurehead::transaction const & transaction_a, MDB_dbi db_a)
	{
		auto status (mdb_cursor_open (tx (transaction_a), db_a, &cursor));
		release_assert (status == 0);
		auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_FIRST));
		release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
		if (status2 != MDB_NOTFOUND)
		{
			auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
			release_assert (status3 == 0 || status3 == MDB_NOTFOUND);
			if (current.first.size () != sizeof (T))
			{
				clear ();
			}
		}
		else
		{
			clear ();
		}
	}

	mdb_iterator () = default;

	mdb_iterator (futurehead::transaction const & transaction_a, MDB_dbi db_a, MDB_val const & val_a)
	{
		auto status (mdb_cursor_open (tx (transaction_a), db_a, &cursor));
		release_assert (status == 0);
		current.first = val_a;
		auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_SET_RANGE));
		release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
		if (status2 != MDB_NOTFOUND)
		{
			auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
			release_assert (status3 == 0 || status3 == MDB_NOTFOUND);
			if (current.first.size () != sizeof (T))
			{
				clear ();
			}
		}
		else
		{
			clear ();
		}
	}

	mdb_iterator (futurehead::mdb_iterator<T, U> && other_a)
	{
		cursor = other_a.cursor;
		other_a.cursor = nullptr;
		current = other_a.current;
	}

	mdb_iterator (futurehead::mdb_iterator<T, U> const &) = delete;

	~mdb_iterator ()
	{
		if (cursor != nullptr)
		{
			mdb_cursor_close (cursor);
		}
	}

	futurehead::store_iterator_impl<T, U> & operator++ () override
	{
		debug_assert (cursor != nullptr);
		auto status (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_NEXT));
		release_assert (status == 0 || status == MDB_NOTFOUND);
		if (status == MDB_NOTFOUND)
		{
			clear ();
		}
		if (current.first.size () != sizeof (T))
		{
			clear ();
		}
		return *this;
	}

	std::pair<futurehead::db_val<MDB_val>, futurehead::db_val<MDB_val>> * operator-> ()
	{
		return &current;
	}

	bool operator== (futurehead::store_iterator_impl<T, U> const & base_a) const override
	{
		auto const other_a (boost::polymorphic_downcast<futurehead::mdb_iterator<T, U> const *> (&base_a));
		auto result (current.first.data () == other_a->current.first.data ());
		debug_assert (!result || (current.first.size () == other_a->current.first.size ()));
		debug_assert (!result || (current.second.data () == other_a->current.second.data ()));
		debug_assert (!result || (current.second.size () == other_a->current.second.size ()));
		return result;
	}

	bool is_end_sentinal () const override
	{
		return current.first.size () == 0;
	}
	void fill (std::pair<T, U> & value_a) const override
	{
		if (current.first.size () != 0)
		{
			value_a.first = static_cast<T> (current.first);
		}
		else
		{
			value_a.first = T ();
		}
		if (current.second.size () != 0)
		{
			value_a.second = static_cast<U> (current.second);
		}
		else
		{
			value_a.second = U ();
		}
	}
	void clear ()
	{
		current.first = futurehead::db_val<MDB_val> ();
		current.second = futurehead::db_val<MDB_val> ();
		debug_assert (is_end_sentinal ());
	}

	futurehead::mdb_iterator<T, U> & operator= (futurehead::mdb_iterator<T, U> && other_a)
	{
		if (cursor != nullptr)
		{
			mdb_cursor_close (cursor);
		}
		cursor = other_a.cursor;
		other_a.cursor = nullptr;
		current = other_a.current;
		other_a.clear ();
		return *this;
	}

	futurehead::store_iterator_impl<T, U> & operator= (futurehead::store_iterator_impl<T, U> const &) = delete;
	MDB_cursor * cursor{ nullptr };
	std::pair<futurehead::db_val<MDB_val>, futurehead::db_val<MDB_val>> current;

private:
	MDB_txn * tx (futurehead::transaction const & transaction_a) const
	{
		return static_cast<MDB_txn *> (transaction_a.get_handle ());
	}
};

/**
 * Iterates the key/value pairs of two stores merged together
 */
template <typename T, typename U>
class mdb_merge_iterator : public store_iterator_impl<T, U>
{
public:
	mdb_merge_iterator (futurehead::transaction const & transaction_a, MDB_dbi db1_a, MDB_dbi db2_a) :
	impl1 (std::make_unique<futurehead::mdb_iterator<T, U>> (transaction_a, db1_a)),
	impl2 (std::make_unique<futurehead::mdb_iterator<T, U>> (transaction_a, db2_a))
	{
	}

	mdb_merge_iterator () :
	impl1 (std::make_unique<futurehead::mdb_iterator<T, U>> ()),
	impl2 (std::make_unique<futurehead::mdb_iterator<T, U>> ())
	{
	}

	mdb_merge_iterator (futurehead::transaction const & transaction_a, MDB_dbi db1_a, MDB_dbi db2_a, MDB_val const & val_a) :
	impl1 (std::make_unique<futurehead::mdb_iterator<T, U>> (transaction_a, db1_a, val_a)),
	impl2 (std::make_unique<futurehead::mdb_iterator<T, U>> (transaction_a, db2_a, val_a))
	{
	}

	mdb_merge_iterator (futurehead::mdb_merge_iterator<T, U> && other_a)
	{
		impl1 = std::move (other_a.impl1);
		impl2 = std::move (other_a.impl2);
	}

	mdb_merge_iterator (futurehead::mdb_merge_iterator<T, U> const &) = delete;

	futurehead::store_iterator_impl<T, U> & operator++ () override
	{
		++least_iterator ();
		return *this;
	}

	std::pair<futurehead::db_val<MDB_val>, futurehead::db_val<MDB_val>> * operator-> ()
	{
		return least_iterator ().operator-> ();
	}

	bool operator== (futurehead::store_iterator_impl<T, U> const & base_a) const override
	{
		debug_assert ((dynamic_cast<futurehead::mdb_merge_iterator<T, U> const *> (&base_a) != nullptr) && "Incompatible iterator comparison");
		auto & other (static_cast<futurehead::mdb_merge_iterator<T, U> const &> (base_a));
		return *impl1 == *other.impl1 && *impl2 == *other.impl2;
	}

	bool is_end_sentinal () const override
	{
		return least_iterator ().is_end_sentinal ();
	}

	void fill (std::pair<T, U> & value_a) const override
	{
		auto & current (least_iterator ());
		if (current->first.size () != 0)
		{
			value_a.first = static_cast<T> (current->first);
		}
		else
		{
			value_a.first = T ();
		}
		if (current->second.size () != 0)
		{
			value_a.second = static_cast<U> (current->second);
		}
		else
		{
			value_a.second = U ();
		}
	}
	futurehead::mdb_merge_iterator<T, U> & operator= (futurehead::mdb_merge_iterator<T, U> &&) = default;
	futurehead::mdb_merge_iterator<T, U> & operator= (futurehead::mdb_merge_iterator<T, U> const &) = delete;

	mutable bool from_first_database{ false };

private:
	futurehead::mdb_iterator<T, U> & least_iterator () const
	{
		futurehead::mdb_iterator<T, U> * result;
		if (impl1->is_end_sentinal ())
		{
			result = impl2.get ();
			from_first_database = false;
		}
		else if (impl2->is_end_sentinal ())
		{
			result = impl1.get ();
			from_first_database = true;
		}
		else
		{
			auto key_cmp (mdb_cmp (mdb_cursor_txn (impl1->cursor), mdb_cursor_dbi (impl1->cursor), impl1->current.first, impl2->current.first));

			if (key_cmp < 0)
			{
				result = impl1.get ();
				from_first_database = true;
			}
			else if (key_cmp > 0)
			{
				result = impl2.get ();
				from_first_database = false;
			}
			else
			{
				auto val_cmp (mdb_cmp (mdb_cursor_txn (impl1->cursor), mdb_cursor_dbi (impl1->cursor), impl1->current.second, impl2->current.second));
				result = val_cmp < 0 ? impl1.get () : impl2.get ();
				from_first_database = (result == impl1.get ());
			}
		}
		return *result;
	}

	std::unique_ptr<futurehead::mdb_iterator<T, U>> impl1;
	std::unique_ptr<futurehead::mdb_iterator<T, U>> impl2;
};
}
