#pragma once

#include <futurehead/secure/blockstore.hpp>

#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>
#include <rocksdb/utilities/transaction.h>

namespace
{
inline bool is_read (futurehead::transaction const & transaction_a)
{
	return (dynamic_cast<const futurehead::read_transaction *> (&transaction_a) != nullptr);
}

inline rocksdb::ReadOptions const & snapshot_options (futurehead::transaction const & transaction_a)
{
	debug_assert (is_read (transaction_a));
	return *static_cast<const rocksdb::ReadOptions *> (transaction_a.get_handle ());
}
}

namespace futurehead
{
using rocksdb_val = db_val<rocksdb::Slice>;

template <typename T, typename U>
class rocksdb_iterator : public store_iterator_impl<T, U>
{
public:
	rocksdb_iterator (rocksdb::DB * db, futurehead::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * handle_a)
	{
		rocksdb::Iterator * iter;
		if (is_read (transaction_a))
		{
			iter = db->NewIterator (snapshot_options (transaction_a), handle_a);
		}
		else
		{
			rocksdb::ReadOptions ropts;
			ropts.fill_cache = false;
			iter = tx (transaction_a)->GetIterator (ropts, handle_a);
		}

		cursor.reset (iter);
		cursor->SeekToFirst ();

		if (cursor->Valid ())
		{
			current.first.value = cursor->key ();
			current.second.value = cursor->value ();
		}
		else
		{
			clear ();
		}
	}

	rocksdb_iterator () = default;

	rocksdb_iterator (rocksdb::DB * db, futurehead::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * handle_a, rocksdb_val const & val_a)
	{
		rocksdb::Iterator * iter;
		if (is_read (transaction_a))
		{
			iter = db->NewIterator (snapshot_options (transaction_a), handle_a);
		}
		else
		{
			iter = tx (transaction_a)->GetIterator (rocksdb::ReadOptions (), handle_a);
		}

		cursor.reset (iter);
		cursor->Seek (val_a);

		if (cursor->Valid ())
		{
			current.first = cursor->key ();
			current.second = cursor->value ();
		}
		else
		{
			clear ();
		}
	}

	rocksdb_iterator (futurehead::rocksdb_iterator<T, U> && other_a)
	{
		cursor = other_a.cursor;
		other_a.cursor = nullptr;
		current = other_a.current;
	}

	rocksdb_iterator (futurehead::rocksdb_iterator<T, U> const &) = delete;

	futurehead::store_iterator_impl<T, U> & operator++ () override
	{
		cursor->Next ();
		if (cursor->Valid ())
		{
			current.first = cursor->key ();
			current.second = cursor->value ();

			if (current.first.size () != sizeof (T))
			{
				clear ();
			}
		}
		else
		{
			clear ();
		}

		return *this;
	}

	std::pair<futurehead::rocksdb_val, futurehead::rocksdb_val> * operator-> ()
	{
		return &current;
	}

	bool operator== (futurehead::store_iterator_impl<T, U> const & base_a) const override
	{
		auto const other_a (boost::polymorphic_downcast<futurehead::rocksdb_iterator<T, U> const *> (&base_a));

		if (!current.first.data () && !other_a->current.first.data ())
		{
			return true;
		}
		else if (!current.first.data () || !other_a->current.first.data ())
		{
			return false;
		}

		auto result (std::memcmp (current.first.data (), other_a->current.first.data (), current.first.size ()) == 0);
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
	}
	void clear ()
	{
		current.first = futurehead::rocksdb_val{};
		current.second = futurehead::rocksdb_val{};
		debug_assert (is_end_sentinal ());
	}
	futurehead::rocksdb_iterator<T, U> & operator= (futurehead::rocksdb_iterator<T, U> && other_a)
	{
		cursor = std::move (other_a.cursor);
		current = other_a.current;
		return *this;
	}
	futurehead::store_iterator_impl<T, U> & operator= (futurehead::store_iterator_impl<T, U> const &) = delete;

	std::unique_ptr<rocksdb::Iterator> cursor;
	std::pair<futurehead::rocksdb_val, futurehead::rocksdb_val> current;

private:
	rocksdb::Transaction * tx (futurehead::transaction const & transaction_a) const
	{
		return static_cast<rocksdb::Transaction *> (transaction_a.get_handle ());
	}
};
}
