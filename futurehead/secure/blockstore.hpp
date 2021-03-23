#pragma once

#include <futurehead/crypto_lib/random_pool.hpp>
#include <futurehead/lib/diagnosticsconfig.hpp>
#include <futurehead/lib/lmdbconfig.hpp>
#include <futurehead/lib/logger_mt.hpp>
#include <futurehead/lib/memory.hpp>
#include <futurehead/lib/rocksdbconfig.hpp>
#include <futurehead/secure/buffer.hpp>
#include <futurehead/secure/common.hpp>
#include <futurehead/secure/versioning.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>

#include <stack>

namespace futurehead
{
// Move to versioning with a specific version if required for a future upgrade
class state_block_w_sideband
{
public:
	std::shared_ptr<futurehead::state_block> state_block;
	futurehead::block_sideband sideband;
};

/**
 * Encapsulates database specific container
 */
template <typename Val>
class db_val
{
public:
	db_val (Val const & value_a) :
	value (value_a)
	{
	}

	db_val () :
	db_val (0, nullptr)
	{
	}

	db_val (futurehead::uint128_union const & val_a) :
	db_val (sizeof (val_a), const_cast<futurehead::uint128_union *> (&val_a))
	{
	}

	db_val (futurehead::uint256_union const & val_a) :
	db_val (sizeof (val_a), const_cast<futurehead::uint256_union *> (&val_a))
	{
	}

	db_val (futurehead::account_info const & val_a) :
	db_val (val_a.db_size (), const_cast<futurehead::account_info *> (&val_a))
	{
	}

	db_val (futurehead::account_info_v13 const & val_a) :
	db_val (val_a.db_size (), const_cast<futurehead::account_info_v13 *> (&val_a))
	{
	}

	db_val (futurehead::account_info_v14 const & val_a) :
	db_val (val_a.db_size (), const_cast<futurehead::account_info_v14 *> (&val_a))
	{
	}

	db_val (futurehead::pending_info const & val_a) :
	db_val (val_a.db_size (), const_cast<futurehead::pending_info *> (&val_a))
	{
		static_assert (std::is_standard_layout<futurehead::pending_info>::value, "Standard layout is required");
	}

	db_val (futurehead::pending_info_v14 const & val_a) :
	db_val (val_a.db_size (), const_cast<futurehead::pending_info_v14 *> (&val_a))
	{
		static_assert (std::is_standard_layout<futurehead::pending_info_v14>::value, "Standard layout is required");
	}

	db_val (futurehead::pending_key const & val_a) :
	db_val (sizeof (val_a), const_cast<futurehead::pending_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<futurehead::pending_key>::value, "Standard layout is required");
	}

	db_val (futurehead::unchecked_info const & val_a) :
	buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			futurehead::vectorstream stream (*buffer);
			val_a.serialize (stream);
		}
		convert_buffer_to_value ();
	}

	db_val (futurehead::unchecked_key const & val_a) :
	db_val (sizeof (val_a), const_cast<futurehead::unchecked_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<futurehead::unchecked_key>::value, "Standard layout is required");
	}

	db_val (futurehead::confirmation_height_info const & val_a) :
	buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			futurehead::vectorstream stream (*buffer);
			val_a.serialize (stream);
		}
		convert_buffer_to_value ();
	}

	db_val (futurehead::block_info const & val_a) :
	db_val (sizeof (val_a), const_cast<futurehead::block_info *> (&val_a))
	{
		static_assert (std::is_standard_layout<futurehead::block_info>::value, "Standard layout is required");
	}

	db_val (futurehead::endpoint_key const & val_a) :
	db_val (sizeof (val_a), const_cast<futurehead::endpoint_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<futurehead::endpoint_key>::value, "Standard layout is required");
	}

	db_val (std::shared_ptr<futurehead::block> const & val_a) :
	buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			futurehead::vectorstream stream (*buffer);
			futurehead::serialize_block (stream, *val_a);
		}
		convert_buffer_to_value ();
	}

	db_val (uint64_t val_a) :
	buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			boost::endian::native_to_big_inplace (val_a);
			futurehead::vectorstream stream (*buffer);
			futurehead::write (stream, val_a);
		}
		convert_buffer_to_value ();
	}

	explicit operator futurehead::account_info () const
	{
		futurehead::account_info result;
		debug_assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator futurehead::account_info_v13 () const
	{
		futurehead::account_info_v13 result;
		debug_assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator futurehead::account_info_v14 () const
	{
		futurehead::account_info_v14 result;
		debug_assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator futurehead::block_info () const
	{
		futurehead::block_info result;
		debug_assert (size () == sizeof (result));
		static_assert (sizeof (futurehead::block_info::account) + sizeof (futurehead::block_info::balance) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator futurehead::pending_info_v14 () const
	{
		futurehead::pending_info_v14 result;
		debug_assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator futurehead::pending_info () const
	{
		futurehead::pending_info result;
		debug_assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator futurehead::pending_key () const
	{
		futurehead::pending_key result;
		debug_assert (size () == sizeof (result));
		static_assert (sizeof (futurehead::pending_key::account) + sizeof (futurehead::pending_key::hash) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator futurehead::confirmation_height_info () const
	{
		futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		futurehead::confirmation_height_info result;
		bool error (result.deserialize (stream));
		(void)error;
		debug_assert (!error);
		return result;
	}

	explicit operator futurehead::unchecked_info () const
	{
		futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		futurehead::unchecked_info result;
		bool error (result.deserialize (stream));
		(void)error;
		debug_assert (!error);
		return result;
	}

	explicit operator futurehead::unchecked_key () const
	{
		futurehead::unchecked_key result;
		debug_assert (size () == sizeof (result));
		static_assert (sizeof (futurehead::unchecked_key::previous) + sizeof (futurehead::pending_key::hash) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator futurehead::uint128_union () const
	{
		return convert<futurehead::uint128_union> ();
	}

	explicit operator futurehead::amount () const
	{
		return convert<futurehead::amount> ();
	}

	explicit operator futurehead::block_hash () const
	{
		return convert<futurehead::block_hash> ();
	}

	explicit operator futurehead::public_key () const
	{
		return convert<futurehead::public_key> ();
	}

	explicit operator futurehead::uint256_union () const
	{
		return convert<futurehead::uint256_union> ();
	}

	explicit operator std::array<char, 64> () const
	{
		futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		std::array<char, 64> result;
		auto error = futurehead::try_read (stream, result);
		(void)error;
		debug_assert (!error);
		return result;
	}

	explicit operator futurehead::endpoint_key () const
	{
		futurehead::endpoint_key result;
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator state_block_w_sideband () const
	{
		futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		futurehead::state_block_w_sideband block_w_sideband;
		block_w_sideband.state_block = std::make_shared<futurehead::state_block> (error, stream);
		debug_assert (!error);

		error = block_w_sideband.sideband.deserialize (stream, futurehead::block_type::state);
		debug_assert (!error);

		return block_w_sideband;
	}

	explicit operator state_block_w_sideband_v14 () const
	{
		futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		futurehead::state_block_w_sideband_v14 block_w_sideband;
		block_w_sideband.state_block = std::make_shared<futurehead::state_block> (error, stream);
		debug_assert (!error);

		block_w_sideband.sideband.type = futurehead::block_type::state;
		error = block_w_sideband.sideband.deserialize (stream);
		debug_assert (!error);

		return block_w_sideband;
	}

	explicit operator futurehead::no_value () const
	{
		return no_value::dummy;
	}

	explicit operator std::shared_ptr<futurehead::block> () const
	{
		futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		std::shared_ptr<futurehead::block> result (futurehead::deserialize_block (stream));
		return result;
	}

	template <typename Block>
	std::shared_ptr<Block> convert_to_block () const
	{
		futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		auto result (std::make_shared<Block> (error, stream));
		debug_assert (!error);
		return result;
	}

	explicit operator std::shared_ptr<futurehead::send_block> () const
	{
		return convert_to_block<futurehead::send_block> ();
	}

	explicit operator std::shared_ptr<futurehead::receive_block> () const
	{
		return convert_to_block<futurehead::receive_block> ();
	}

	explicit operator std::shared_ptr<futurehead::open_block> () const
	{
		return convert_to_block<futurehead::open_block> ();
	}

	explicit operator std::shared_ptr<futurehead::change_block> () const
	{
		return convert_to_block<futurehead::change_block> ();
	}

	explicit operator std::shared_ptr<futurehead::state_block> () const
	{
		return convert_to_block<futurehead::state_block> ();
	}

	explicit operator std::shared_ptr<futurehead::vote> () const
	{
		futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		auto result (futurehead::make_shared<futurehead::vote> (error, stream));
		debug_assert (!error);
		return result;
	}

	explicit operator uint64_t () const
	{
		uint64_t result;
		futurehead::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (futurehead::try_read (stream, result));
		(void)error;
		debug_assert (!error);
		boost::endian::big_to_native_inplace (result);
		return result;
	}

	operator Val * () const
	{
		// Allow passing a temporary to a non-c++ function which doesn't have constness
		return const_cast<Val *> (&value);
	}

	operator Val const & () const
	{
		return value;
	}

	// Must be specialized
	void * data () const;
	size_t size () const;
	db_val (size_t size_a, void * data_a);
	void convert_buffer_to_value ();

	Val value;
	std::shared_ptr<std::vector<uint8_t>> buffer;

private:
	template <typename T>
	T convert () const
	{
		T result;
		debug_assert (size () == sizeof (result));
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
		return result;
	}
};

class transaction;
class block_store;

/**
 * Summation visitor for blocks, supporting amount and balance computations. These
 * computations are mutually dependant. The natural solution is to use mutual recursion
 * between balance and amount visitors, but this leads to very deep stacks. Hence, the
 * summation visitor uses an iterative approach.
 */
class summation_visitor final : public futurehead::block_visitor
{
	enum summation_type
	{
		invalid = 0,
		balance = 1,
		amount = 2
	};

	/** Represents an invocation frame */
	class frame final
	{
	public:
		frame (summation_type type_a, futurehead::block_hash balance_hash_a, futurehead::block_hash amount_hash_a) :
		type (type_a), balance_hash (balance_hash_a), amount_hash (amount_hash_a)
		{
		}

		/** The summation type guides the block visitor handlers */
		summation_type type{ invalid };
		/** Accumulated balance or amount */
		futurehead::uint128_t sum{ 0 };
		/** The current balance hash */
		futurehead::block_hash balance_hash{ 0 };
		/** The current amount hash */
		futurehead::block_hash amount_hash{ 0 };
		/** If true, this frame is awaiting an invocation result */
		bool awaiting_result{ false };
		/** Set by the invoked frame, representing the return value */
		futurehead::uint128_t incoming_result{ 0 };
	};

public:
	summation_visitor (futurehead::transaction const &, futurehead::block_store const &, bool is_v14_upgrade = false);
	virtual ~summation_visitor () = default;
	/** Computes the balance as of \p block_hash */
	futurehead::uint128_t compute_balance (futurehead::block_hash const & block_hash);
	/** Computes the amount delta between \p block_hash and its predecessor */
	futurehead::uint128_t compute_amount (futurehead::block_hash const & block_hash);

protected:
	futurehead::transaction const & transaction;
	futurehead::block_store const & store;
	futurehead::network_params network_params;

	/** The final result */
	futurehead::uint128_t result{ 0 };
	/** The current invocation frame */
	frame * current{ nullptr };
	/** Invocation frames */
	std::stack<frame> frames;
	/** Push a copy of \p hash of the given summation \p type */
	futurehead::summation_visitor::frame push (futurehead::summation_visitor::summation_type type, futurehead::block_hash const & hash);
	void sum_add (futurehead::uint128_t addend_a);
	void sum_set (futurehead::uint128_t value_a);
	/** The epilogue yields the result to previous frame, if any */
	void epilogue ();

	futurehead::uint128_t compute_internal (futurehead::summation_visitor::summation_type type, futurehead::block_hash const &);
	void send_block (futurehead::send_block const &) override;
	void receive_block (futurehead::receive_block const &) override;
	void open_block (futurehead::open_block const &) override;
	void change_block (futurehead::change_block const &) override;
	void state_block (futurehead::state_block const &) override;

private:
	bool is_v14_upgrade;
	std::shared_ptr<futurehead::block> block_get (futurehead::transaction const &, futurehead::block_hash const &) const;
};

/**
 * Determine the representative for this block
 */
class representative_visitor final : public futurehead::block_visitor
{
public:
	representative_visitor (futurehead::transaction const & transaction_a, futurehead::block_store & store_a);
	~representative_visitor () = default;
	void compute (futurehead::block_hash const & hash_a);
	void send_block (futurehead::send_block const & block_a) override;
	void receive_block (futurehead::receive_block const & block_a) override;
	void open_block (futurehead::open_block const & block_a) override;
	void change_block (futurehead::change_block const & block_a) override;
	void state_block (futurehead::state_block const & block_a) override;
	futurehead::transaction const & transaction;
	futurehead::block_store & store;
	futurehead::block_hash current;
	futurehead::block_hash result;
};
template <typename T, typename U>
class store_iterator_impl
{
public:
	virtual ~store_iterator_impl () = default;
	virtual futurehead::store_iterator_impl<T, U> & operator++ () = 0;
	virtual bool operator== (futurehead::store_iterator_impl<T, U> const & other_a) const = 0;
	virtual bool is_end_sentinal () const = 0;
	virtual void fill (std::pair<T, U> &) const = 0;
	futurehead::store_iterator_impl<T, U> & operator= (futurehead::store_iterator_impl<T, U> const &) = delete;
	bool operator== (futurehead::store_iterator_impl<T, U> const * other_a) const
	{
		return (other_a != nullptr && *this == *other_a) || (other_a == nullptr && is_end_sentinal ());
	}
	bool operator!= (futurehead::store_iterator_impl<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}
};
/**
 * Iterates the key/value pairs of a transaction
 */
template <typename T, typename U>
class store_iterator final
{
public:
	store_iterator (std::nullptr_t)
	{
	}
	store_iterator (std::unique_ptr<futurehead::store_iterator_impl<T, U>> impl_a) :
	impl (std::move (impl_a))
	{
		impl->fill (current);
	}
	store_iterator (futurehead::store_iterator<T, U> && other_a) :
	current (std::move (other_a.current)),
	impl (std::move (other_a.impl))
	{
	}
	futurehead::store_iterator<T, U> & operator++ ()
	{
		++*impl;
		impl->fill (current);
		return *this;
	}
	futurehead::store_iterator<T, U> & operator= (futurehead::store_iterator<T, U> && other_a) noexcept
	{
		impl = std::move (other_a.impl);
		current = std::move (other_a.current);
		return *this;
	}
	futurehead::store_iterator<T, U> & operator= (futurehead::store_iterator<T, U> const &) = delete;
	std::pair<T, U> * operator-> ()
	{
		return &current;
	}
	bool operator== (futurehead::store_iterator<T, U> const & other_a) const
	{
		return (impl == nullptr && other_a.impl == nullptr) || (impl != nullptr && *impl == other_a.impl.get ()) || (other_a.impl != nullptr && *other_a.impl == impl.get ());
	}
	bool operator!= (futurehead::store_iterator<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}

private:
	std::pair<T, U> current;
	std::unique_ptr<futurehead::store_iterator_impl<T, U>> impl;
};

// Keep this in alphabetical order
enum class tables
{
	accounts,
	blocks_info, // LMDB only
	cached_counts, // RocksDB only
	change_blocks,
	confirmation_height,
	frontiers,
	meta,
	online_weight,
	open_blocks,
	peers,
	pending,
	receive_blocks,
	representation,
	send_blocks,
	state_blocks,
	unchecked,
	vote
};

class transaction_impl
{
public:
	virtual ~transaction_impl () = default;
	virtual void * get_handle () const = 0;
};

class read_transaction_impl : public transaction_impl
{
public:
	virtual void reset () = 0;
	virtual void renew () = 0;
};

class write_transaction_impl : public transaction_impl
{
public:
	virtual void commit () const = 0;
	virtual void renew () = 0;
	virtual bool contains (futurehead::tables table_a) const = 0;
};

class transaction
{
public:
	virtual ~transaction () = default;
	virtual void * get_handle () const = 0;
};

/**
 * RAII wrapper of a read MDB_txn where the constructor starts the transaction
 * and the destructor aborts it.
 */
class read_transaction final : public transaction
{
public:
	explicit read_transaction (std::unique_ptr<futurehead::read_transaction_impl> read_transaction_impl);
	void * get_handle () const override;
	void reset () const;
	void renew () const;
	void refresh () const;

private:
	std::unique_ptr<futurehead::read_transaction_impl> impl;
};

/**
 * RAII wrapper of a read-write MDB_txn where the constructor starts the transaction
 * and the destructor commits it.
 */
class write_transaction final : public transaction
{
public:
	explicit write_transaction (std::unique_ptr<futurehead::write_transaction_impl> write_transaction_impl);
	void * get_handle () const override;
	void commit () const;
	void renew ();
	bool contains (futurehead::tables table_a) const;

private:
	std::unique_ptr<futurehead::write_transaction_impl> impl;
};

class ledger_cache;

/**
 * Manages block storage and iteration
 */
class block_store
{
public:
	virtual ~block_store () = default;
	virtual void initialize (futurehead::write_transaction const &, futurehead::genesis const &, futurehead::ledger_cache &) = 0;
	virtual void block_put (futurehead::write_transaction const &, futurehead::block_hash const &, futurehead::block const &) = 0;
	virtual futurehead::block_hash block_successor (futurehead::transaction const &, futurehead::block_hash const &) const = 0;
	virtual void block_successor_clear (futurehead::write_transaction const &, futurehead::block_hash const &) = 0;
	virtual std::shared_ptr<futurehead::block> block_get (futurehead::transaction const &, futurehead::block_hash const &) const = 0;
	virtual std::shared_ptr<futurehead::block> block_get_no_sideband (futurehead::transaction const &, futurehead::block_hash const &) const = 0;
	virtual std::shared_ptr<futurehead::block> block_get_v14 (futurehead::transaction const &, futurehead::block_hash const &, futurehead::block_sideband_v14 * = nullptr, bool * = nullptr) const = 0;
	virtual std::shared_ptr<futurehead::block> block_random (futurehead::transaction const &) = 0;
	virtual void block_del (futurehead::write_transaction const &, futurehead::block_hash const &, futurehead::block_type) = 0;
	virtual bool block_exists (futurehead::transaction const &, futurehead::block_hash const &) = 0;
	virtual bool block_exists (futurehead::transaction const &, futurehead::block_type, futurehead::block_hash const &) = 0;
	virtual futurehead::block_counts block_count (futurehead::transaction const &) = 0;
	virtual bool root_exists (futurehead::transaction const &, futurehead::root const &) = 0;
	virtual bool source_exists (futurehead::transaction const &, futurehead::block_hash const &) = 0;
	virtual futurehead::account block_account (futurehead::transaction const &, futurehead::block_hash const &) const = 0;
	virtual futurehead::account block_account_calculated (futurehead::block const &) const = 0;

	virtual void frontier_put (futurehead::write_transaction const &, futurehead::block_hash const &, futurehead::account const &) = 0;
	virtual futurehead::account frontier_get (futurehead::transaction const &, futurehead::block_hash const &) const = 0;
	virtual void frontier_del (futurehead::write_transaction const &, futurehead::block_hash const &) = 0;

	virtual void account_put (futurehead::write_transaction const &, futurehead::account const &, futurehead::account_info const &) = 0;
	virtual bool account_get (futurehead::transaction const &, futurehead::account const &, futurehead::account_info &) = 0;
	virtual void account_del (futurehead::write_transaction const &, futurehead::account const &) = 0;
	virtual bool account_exists (futurehead::transaction const &, futurehead::account const &) = 0;
	virtual size_t account_count (futurehead::transaction const &) = 0;
	virtual void confirmation_height_clear (futurehead::write_transaction const &, futurehead::account const &, uint64_t) = 0;
	virtual void confirmation_height_clear (futurehead::write_transaction const &) = 0;
	virtual futurehead::store_iterator<futurehead::account, futurehead::account_info> latest_begin (futurehead::transaction const &, futurehead::account const &) const = 0;
	virtual futurehead::store_iterator<futurehead::account, futurehead::account_info> latest_begin (futurehead::transaction const &) const = 0;
	virtual futurehead::store_iterator<futurehead::account, futurehead::account_info> latest_end () const = 0;

	virtual void pending_put (futurehead::write_transaction const &, futurehead::pending_key const &, futurehead::pending_info const &) = 0;
	virtual void pending_del (futurehead::write_transaction const &, futurehead::pending_key const &) = 0;
	virtual bool pending_get (futurehead::transaction const &, futurehead::pending_key const &, futurehead::pending_info &) = 0;
	virtual bool pending_exists (futurehead::transaction const &, futurehead::pending_key const &) = 0;
	virtual bool pending_any (futurehead::transaction const &, futurehead::account const &) = 0;
	virtual futurehead::store_iterator<futurehead::pending_key, futurehead::pending_info> pending_begin (futurehead::transaction const &, futurehead::pending_key const &) = 0;
	virtual futurehead::store_iterator<futurehead::pending_key, futurehead::pending_info> pending_begin (futurehead::transaction const &) = 0;
	virtual futurehead::store_iterator<futurehead::pending_key, futurehead::pending_info> pending_end () = 0;

	virtual bool block_info_get (futurehead::transaction const &, futurehead::block_hash const &, futurehead::block_info &) const = 0;
	virtual futurehead::uint128_t block_balance (futurehead::transaction const &, futurehead::block_hash const &) = 0;
	virtual futurehead::uint128_t block_balance_calculated (std::shared_ptr<futurehead::block> const &) const = 0;
	virtual futurehead::epoch block_version (futurehead::transaction const &, futurehead::block_hash const &) = 0;

	virtual void unchecked_clear (futurehead::write_transaction const &) = 0;
	virtual void unchecked_put (futurehead::write_transaction const &, futurehead::unchecked_key const &, futurehead::unchecked_info const &) = 0;
	virtual void unchecked_put (futurehead::write_transaction const &, futurehead::block_hash const &, std::shared_ptr<futurehead::block> const &) = 0;
	virtual std::vector<futurehead::unchecked_info> unchecked_get (futurehead::transaction const &, futurehead::block_hash const &) = 0;
	virtual bool unchecked_exists (futurehead::transaction const & transaction_a, futurehead::unchecked_key const & unchecked_key_a) = 0;
	virtual void unchecked_del (futurehead::write_transaction const &, futurehead::unchecked_key const &) = 0;
	virtual futurehead::store_iterator<futurehead::unchecked_key, futurehead::unchecked_info> unchecked_begin (futurehead::transaction const &) const = 0;
	virtual futurehead::store_iterator<futurehead::unchecked_key, futurehead::unchecked_info> unchecked_begin (futurehead::transaction const &, futurehead::unchecked_key const &) const = 0;
	virtual futurehead::store_iterator<futurehead::unchecked_key, futurehead::unchecked_info> unchecked_end () const = 0;
	virtual size_t unchecked_count (futurehead::transaction const &) = 0;

	// Return latest vote for an account from store
	virtual std::shared_ptr<futurehead::vote> vote_get (futurehead::transaction const &, futurehead::account const &) = 0;
	// Populate vote with the next sequence number
	virtual std::shared_ptr<futurehead::vote> vote_generate (futurehead::transaction const &, futurehead::account const &, futurehead::raw_key const &, std::shared_ptr<futurehead::block>) = 0;
	virtual std::shared_ptr<futurehead::vote> vote_generate (futurehead::transaction const &, futurehead::account const &, futurehead::raw_key const &, std::vector<futurehead::block_hash>) = 0;
	// Return either vote or the stored vote with a higher sequence number
	virtual std::shared_ptr<futurehead::vote> vote_max (futurehead::transaction const &, std::shared_ptr<futurehead::vote>) = 0;
	// Return latest vote for an account considering the vote cache
	virtual std::shared_ptr<futurehead::vote> vote_current (futurehead::transaction const &, futurehead::account const &) = 0;
	virtual void flush (futurehead::write_transaction const &) = 0;
	virtual futurehead::store_iterator<futurehead::account, std::shared_ptr<futurehead::vote>> vote_begin (futurehead::transaction const &) = 0;
	virtual futurehead::store_iterator<futurehead::account, std::shared_ptr<futurehead::vote>> vote_end () = 0;

	virtual void online_weight_put (futurehead::write_transaction const &, uint64_t, futurehead::amount const &) = 0;
	virtual void online_weight_del (futurehead::write_transaction const &, uint64_t) = 0;
	virtual futurehead::store_iterator<uint64_t, futurehead::amount> online_weight_begin (futurehead::transaction const &) const = 0;
	virtual futurehead::store_iterator<uint64_t, futurehead::amount> online_weight_end () const = 0;
	virtual size_t online_weight_count (futurehead::transaction const &) const = 0;
	virtual void online_weight_clear (futurehead::write_transaction const &) = 0;

	virtual void version_put (futurehead::write_transaction const &, int) = 0;
	virtual int version_get (futurehead::transaction const &) const = 0;

	virtual void peer_put (futurehead::write_transaction const & transaction_a, futurehead::endpoint_key const & endpoint_a) = 0;
	virtual void peer_del (futurehead::write_transaction const & transaction_a, futurehead::endpoint_key const & endpoint_a) = 0;
	virtual bool peer_exists (futurehead::transaction const & transaction_a, futurehead::endpoint_key const & endpoint_a) const = 0;
	virtual size_t peer_count (futurehead::transaction const & transaction_a) const = 0;
	virtual void peer_clear (futurehead::write_transaction const & transaction_a) = 0;
	virtual futurehead::store_iterator<futurehead::endpoint_key, futurehead::no_value> peers_begin (futurehead::transaction const & transaction_a) const = 0;
	virtual futurehead::store_iterator<futurehead::endpoint_key, futurehead::no_value> peers_end () const = 0;

	virtual void confirmation_height_put (futurehead::write_transaction const & transaction_a, futurehead::account const & account_a, futurehead::confirmation_height_info const & confirmation_height_info_a) = 0;
	virtual bool confirmation_height_get (futurehead::transaction const & transaction_a, futurehead::account const & account_a, futurehead::confirmation_height_info & confirmation_height_info_a) = 0;
	virtual bool confirmation_height_exists (futurehead::transaction const & transaction_a, futurehead::account const & account_a) const = 0;
	virtual void confirmation_height_del (futurehead::write_transaction const & transaction_a, futurehead::account const & account_a) = 0;
	virtual uint64_t confirmation_height_count (futurehead::transaction const & transaction_a) = 0;
	virtual futurehead::store_iterator<futurehead::account, futurehead::confirmation_height_info> confirmation_height_begin (futurehead::transaction const & transaction_a, futurehead::account const & account_a) = 0;
	virtual futurehead::store_iterator<futurehead::account, futurehead::confirmation_height_info> confirmation_height_begin (futurehead::transaction const & transaction_a) = 0;
	virtual futurehead::store_iterator<futurehead::account, futurehead::confirmation_height_info> confirmation_height_end () = 0;

	virtual uint64_t block_account_height (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a) const = 0;
	virtual std::mutex & get_cache_mutex () = 0;

	virtual bool copy_db (boost::filesystem::path const & destination) = 0;
	virtual void rebuild_db (futurehead::write_transaction const & transaction_a) = 0;

	/** Not applicable to all sub-classes */
	virtual void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) = 0;

	virtual bool init_error () const = 0;

	/** Start read-write transaction */
	virtual futurehead::write_transaction tx_begin_write (std::vector<futurehead::tables> const & tables_to_lock = {}, std::vector<futurehead::tables> const & tables_no_lock = {}) = 0;

	/** Start read-only transaction */
	virtual futurehead::read_transaction tx_begin_read () = 0;

	virtual std::string vendor_get () const = 0;
};

std::unique_ptr<futurehead::block_store> make_store (futurehead::logger_mt & logger, boost::filesystem::path const & path, bool open_read_only = false, bool add_db_postfix = false, futurehead::rocksdb_config const & rocksdb_config = futurehead::rocksdb_config{}, futurehead::txn_tracking_config const & txn_tracking_config_a = futurehead::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), futurehead::lmdb_config const & lmdb_config_a = futurehead::lmdb_config{}, size_t batch_size = 512, bool backup_before_upgrade = false, bool rocksdb_backend = false);
}

namespace std
{
template <>
struct hash<::futurehead::tables>
{
	size_t operator() (::futurehead::tables const & table_a) const
	{
		return static_cast<size_t> (table_a);
	}
};
}
