#pragma once

#include <boost/multiprecision/cpp_int.hpp>

namespace futurehead
{
using uint128_t = boost::multiprecision::uint128_t;
using uint256_t = boost::multiprecision::uint256_t;
using uint512_t = boost::multiprecision::uint512_t;
// SI dividers
//TO-CHANGE
futurehead::uint128_t const Gxrb_ratio = futurehead::uint128_t ("1000000000000000000000000000000000"); // 10^33
futurehead::uint128_t const Mxrb_ratio = futurehead::uint128_t ("1000000000000000000000000000000"); // 10^30
futurehead::uint128_t const kxrb_ratio = futurehead::uint128_t ("1000000000000000000000000000"); // 10^27
futurehead::uint128_t const xrb_ratio = futurehead::uint128_t ("1000000000000000000000000"); // 10^24
futurehead::uint128_t const raw_ratio = futurehead::uint128_t ("1"); // 10^0

class uint128_union
{
public:
	uint128_union () = default;
	/**
	 * Decode from hex string
	 * @warning Aborts at runtime if the input is invalid
	 */
	uint128_union (std::string const &);
	uint128_union (uint64_t);
	uint128_union (futurehead::uint128_t const &);
	bool operator== (futurehead::uint128_union const &) const;
	bool operator!= (futurehead::uint128_union const &) const;
	bool operator< (futurehead::uint128_union const &) const;
	bool operator> (futurehead::uint128_union const &) const;
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	void encode_dec (std::string &) const;
	bool decode_dec (std::string const &, bool = false);
	bool decode_dec (std::string const &, futurehead::uint128_t);
	std::string format_balance (futurehead::uint128_t scale, int precision, bool group_digits) const;
	std::string format_balance (futurehead::uint128_t scale, int precision, bool group_digits, const std::locale & locale) const;
	futurehead::uint128_t number () const;
	void clear ();
	bool is_zero () const;
	std::string to_string () const;
	std::string to_string_dec () const;
	union
	{
		std::array<uint8_t, 16> bytes;
		std::array<char, 16> chars;
		std::array<uint32_t, 4> dwords;
		std::array<uint64_t, 2> qwords;
	};
};
static_assert (std::is_nothrow_move_constructible<uint128_union>::value, "uint128_union should be noexcept MoveConstructible");

// Balances are 128 bit.
class amount : public uint128_union
{
public:
	using uint128_union::uint128_union;
};
class raw_key;
class uint256_union
{
public:
	uint256_union () = default;
	/**
	 * Decode from hex string
	 * @warning Aborts at runtime if the input is invalid
	 */
	uint256_union (std::string const &);
	uint256_union (uint64_t);
	uint256_union (futurehead::uint256_t const &);
	void encrypt (futurehead::raw_key const &, futurehead::raw_key const &, uint128_union const &);
	uint256_union & operator^= (futurehead::uint256_union const &);
	uint256_union operator^ (futurehead::uint256_union const &) const;
	bool operator== (futurehead::uint256_union const &) const;
	bool operator!= (futurehead::uint256_union const &) const;
	bool operator< (futurehead::uint256_union const &) const;
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	void encode_dec (std::string &) const;
	bool decode_dec (std::string const &);

	void clear ();
	bool is_zero () const;
	std::string to_string () const;
	futurehead::uint256_t number () const;

	union
	{
		std::array<uint8_t, 32> bytes;
		std::array<char, 32> chars;
		std::array<uint32_t, 8> dwords;
		std::array<uint64_t, 4> qwords;
		std::array<uint128_union, 2> owords;
	};
};
static_assert (std::is_nothrow_move_constructible<uint256_union>::value, "uint256_union should be noexcept MoveConstructible");

class link;
class root;
class hash_or_account;

// All keys and hashes are 256 bit.
class block_hash final : public uint256_union
{
public:
	using uint256_union::uint256_union;
	operator futurehead::link const & () const;
	operator futurehead::root const & () const;
	operator futurehead::hash_or_account const & () const;
};

class public_key final : public uint256_union
{
public:
	using uint256_union::uint256_union;

	std::string to_node_id () const;
	bool decode_node_id (std::string const & source_a);
	void encode_account (std::string &) const;
	std::string to_account () const;
	bool decode_account (std::string const &);

	operator futurehead::link const & () const;
	operator futurehead::root const & () const;
	operator futurehead::hash_or_account const & () const;
};

class wallet_id : public uint256_union
{
	using uint256_union::uint256_union;
};

// These are synonymous
using account = public_key;

class hash_or_account
{
public:
	hash_or_account () = default;
	hash_or_account (uint64_t value_a);

	bool is_zero () const;
	void clear ();
	std::string to_string () const;
	bool decode_hex (std::string const &);
	bool decode_account (std::string const &);
	std::string to_account () const;

	operator futurehead::block_hash const & () const;
	operator futurehead::account const & () const;
	operator futurehead::uint256_union const & () const;

	bool operator== (futurehead::hash_or_account const &) const;
	bool operator!= (futurehead::hash_or_account const &) const;

	union
	{
		std::array<uint8_t, 32> bytes;
		futurehead::uint256_union raw; // This can be used when you don't want to explicitly mention either of the types
		futurehead::account account;
		futurehead::block_hash hash;
	};
};

// A link can either be a destination account or source hash
class link final : public hash_or_account
{
public:
	using hash_or_account::hash_or_account;
};

// A root can either be an open block hash or a previous hash
class root final : public hash_or_account
{
public:
	using hash_or_account::hash_or_account;

	futurehead::block_hash const & previous () const;
};

class private_key : public uint256_union
{
public:
	using uint256_union::uint256_union;
};

// The seed or private key
class raw_key final
{
public:
	~raw_key ();
	void decrypt (futurehead::uint256_union const &, futurehead::raw_key const &, uint128_union const &);
	bool operator== (futurehead::raw_key const &) const;
	bool operator!= (futurehead::raw_key const &) const;
	futurehead::private_key const & as_private_key () const;
	futurehead::uint256_union data;
};
class uint512_union
{
public:
	uint512_union () = default;
	uint512_union (futurehead::uint256_union const &, futurehead::uint256_union const &);
	uint512_union (futurehead::uint512_t const &);
	bool operator== (futurehead::uint512_union const &) const;
	bool operator!= (futurehead::uint512_union const &) const;
	futurehead::uint512_union & operator^= (futurehead::uint512_union const &);
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	void clear ();
	bool is_zero () const;
	futurehead::uint512_t number () const;
	std::string to_string () const;

	union
	{
		std::array<uint8_t, 64> bytes;
		std::array<uint32_t, 16> dwords;
		std::array<uint64_t, 8> qwords;
		std::array<uint256_union, 2> uint256s;
	};
};
static_assert (std::is_nothrow_move_constructible<uint512_union>::value, "uint512_union should be noexcept MoveConstructible");

class signature : public uint512_union
{
public:
	using uint512_union::uint512_union;
};

class qualified_root : public uint512_union
{
public:
	using uint512_union::uint512_union;

	futurehead::block_hash const & previous () const
	{
		return reinterpret_cast<futurehead::block_hash const &> (uint256s[0]);
	}
	futurehead::root const & root () const
	{
		return reinterpret_cast<futurehead::root const &> (uint256s[1]);
	}
};

futurehead::signature sign_message (futurehead::raw_key const &, futurehead::public_key const &, futurehead::uint256_union const &);
futurehead::signature sign_message (futurehead::raw_key const &, futurehead::public_key const &, uint8_t const *, size_t);
bool validate_message (futurehead::public_key const &, futurehead::uint256_union const &, futurehead::signature const &);
bool validate_message (futurehead::public_key const &, uint8_t const *, size_t, futurehead::signature const &);
bool validate_message_batch (unsigned const char **, size_t *, unsigned const char **, unsigned const char **, size_t, int *);
futurehead::private_key deterministic_key (futurehead::raw_key const &, uint32_t);
futurehead::public_key pub_key (futurehead::private_key const &);

/* Conversion methods */
std::string to_string_hex (uint64_t const);
bool from_string_hex (std::string const &, uint64_t &);

/**
 * Convert a double to string in fixed format
 * @param precision_a (optional) use a specific precision (default is the maximum)
 */
std::string to_string (double const, int const precision_a = std::numeric_limits<double>::digits10);

namespace difficulty
{
	uint64_t from_multiplier (double const, uint64_t const);
	double to_multiplier (uint64_t const, uint64_t const);
}
}

namespace std
{
template <>
struct hash<::futurehead::uint256_union>
{
	size_t operator() (::futurehead::uint256_union const & data_a) const
	{
		return data_a.qwords[0] + data_a.qwords[1] + data_a.qwords[2] + data_a.qwords[3];
	}
};
template <>
struct hash<::futurehead::account>
{
	size_t operator() (::futurehead::account const & data_a) const
	{
		return hash<::futurehead::uint256_union> () (data_a);
	}
};
template <>
struct hash<::futurehead::block_hash>
{
	size_t operator() (::futurehead::block_hash const & data_a) const
	{
		return hash<::futurehead::uint256_union> () (data_a);
	}
};
template <>
struct hash<::futurehead::private_key>
{
	size_t operator() (::futurehead::private_key const & data_a) const
	{
		return hash<::futurehead::uint256_union> () (data_a);
	}
};
template <>
struct hash<::futurehead::root>
{
	size_t operator() (::futurehead::root const & data_a) const
	{
		return hash<::futurehead::uint256_union> () (data_a);
	}
};
template <>
struct hash<::futurehead::wallet_id>
{
	size_t operator() (::futurehead::wallet_id const & data_a) const
	{
		return hash<::futurehead::uint256_union> () (data_a);
	}
};
template <>
struct hash<::futurehead::uint256_t>
{
	size_t operator() (::futurehead::uint256_t const & number_a) const
	{
		return number_a.convert_to<size_t> ();
	}
};
template <>
struct hash<::futurehead::uint512_union>
{
	size_t operator() (::futurehead::uint512_union const & data_a) const
	{
		return hash<::futurehead::uint256_union> () (data_a.uint256s[0]) + hash<::futurehead::uint256_union> () (data_a.uint256s[1]);
	}
};
template <>
struct hash<::futurehead::qualified_root>
{
	size_t operator() (::futurehead::qualified_root const & data_a) const
	{
		return hash<::futurehead::uint512_union> () (data_a);
	}
};
}
