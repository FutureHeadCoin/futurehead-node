#pragma once

#include <futurehead/crypto/blake2/blake2.h>
#include <futurehead/lib/blockbuilders.hpp>
#include <futurehead/lib/blocks.hpp>
#include <futurehead/lib/config.hpp>
#include <futurehead/lib/epoch.hpp>
#include <futurehead/lib/numbers.hpp>
#include <futurehead/lib/rep_weights.hpp>
#include <futurehead/lib/utility.hpp>

#include <boost/iterator/transform_iterator.hpp>
#include <boost/optional/optional.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/variant/variant.hpp>

#include <unordered_map>

namespace boost
{
template <>
struct hash<::futurehead::uint256_union>
{
	size_t operator() (::futurehead::uint256_union const & value_a) const
	{
		return std::hash<::futurehead::uint256_union> () (value_a);
	}
};

template <>
struct hash<::futurehead::block_hash>
{
	size_t operator() (::futurehead::block_hash const & value_a) const
	{
		return std::hash<::futurehead::block_hash> () (value_a);
	}
};

template <>
struct hash<::futurehead::public_key>
{
	size_t operator() (::futurehead::public_key const & value_a) const
	{
		return std::hash<::futurehead::public_key> () (value_a);
	}
};
template <>
struct hash<::futurehead::uint512_union>
{
	size_t operator() (::futurehead::uint512_union const & value_a) const
	{
		return std::hash<::futurehead::uint512_union> () (value_a);
	}
};
template <>
struct hash<::futurehead::qualified_root>
{
	size_t operator() (::futurehead::qualified_root const & value_a) const
	{
		return std::hash<::futurehead::qualified_root> () (value_a);
	}
};
}
namespace futurehead
{
/**
 * A key pair. The private key is generated from the random pool, or passed in
 * as a hex string. The public key is derived using ed25519.
 */
class keypair
{
public:
	keypair ();
	keypair (std::string const &);
	keypair (futurehead::raw_key &&);
	futurehead::public_key pub;
	futurehead::raw_key prv;
};

/**
 * Latest information about an account
 */
class account_info final
{
public:
	account_info () = default;
	account_info (futurehead::block_hash const &, futurehead::account const &, futurehead::block_hash const &, futurehead::amount const &, uint64_t, uint64_t, epoch);
	bool deserialize (futurehead::stream &);
	bool operator== (futurehead::account_info const &) const;
	bool operator!= (futurehead::account_info const &) const;
	size_t db_size () const;
	futurehead::epoch epoch () const;
	futurehead::block_hash head{ 0 };
	futurehead::account representative{ 0 };
	futurehead::block_hash open_block{ 0 };
	futurehead::amount balance{ 0 };
	/** Seconds since posix epoch */
	uint64_t modified{ 0 };
	uint64_t block_count{ 0 };
	futurehead::epoch epoch_m{ futurehead::epoch::epoch_0 };
};

/**
 * Information on an uncollected send
 */
class pending_info final
{
public:
	pending_info () = default;
	pending_info (futurehead::account const &, futurehead::amount const &, futurehead::epoch);
	size_t db_size () const;
	bool deserialize (futurehead::stream &);
	bool operator== (futurehead::pending_info const &) const;
	futurehead::account source{ 0 };
	futurehead::amount amount{ 0 };
	futurehead::epoch epoch{ futurehead::epoch::epoch_0 };
};
class pending_key final
{
public:
	pending_key () = default;
	pending_key (futurehead::account const &, futurehead::block_hash const &);
	bool deserialize (futurehead::stream &);
	bool operator== (futurehead::pending_key const &) const;
	futurehead::account const & key () const;
	futurehead::account account{ 0 };
	futurehead::block_hash hash{ 0 };
};

class endpoint_key final
{
public:
	endpoint_key () = default;

	/*
	 * @param address_a This should be in network byte order
	 * @param port_a This should be in host byte order
	 */
	endpoint_key (const std::array<uint8_t, 16> & address_a, uint16_t port_a);

	/*
	 * @return The ipv6 address in network byte order
	 */
	const std::array<uint8_t, 16> & address_bytes () const;

	/*
	 * @return The port in host byte order
	 */
	uint16_t port () const;

private:
	// Both stored internally in network byte order
	std::array<uint8_t, 16> address;
	uint16_t network_port{ 0 };
};

enum class no_value
{
	dummy
};

class unchecked_key final
{
public:
	unchecked_key () = default;
	unchecked_key (futurehead::block_hash const &, futurehead::block_hash const &);
	bool deserialize (futurehead::stream &);
	bool operator== (futurehead::unchecked_key const &) const;
	futurehead::block_hash const & key () const;
	futurehead::block_hash previous{ 0 };
	futurehead::block_hash hash{ 0 };
};

/**
 * Tag for block signature verification result
 */
enum class signature_verification : uint8_t
{
	unknown = 0,
	invalid = 1,
	valid = 2,
	valid_epoch = 3 // Valid for epoch blocks
};

/**
 * Information on an unchecked block
 */
class unchecked_info final
{
public:
	unchecked_info () = default;
	unchecked_info (std::shared_ptr<futurehead::block>, futurehead::account const &, uint64_t, futurehead::signature_verification = futurehead::signature_verification::unknown, bool = false);
	void serialize (futurehead::stream &) const;
	bool deserialize (futurehead::stream &);
	std::shared_ptr<futurehead::block> block;
	futurehead::account account{ 0 };
	/** Seconds since posix epoch */
	uint64_t modified{ 0 };
	futurehead::signature_verification verified{ futurehead::signature_verification::unknown };
	bool confirmed{ false };
};

class block_info final
{
public:
	block_info () = default;
	block_info (futurehead::account const &, futurehead::amount const &);
	futurehead::account account{ 0 };
	futurehead::amount balance{ 0 };
};
class block_counts final
{
public:
	size_t sum () const;
	size_t send{ 0 };
	size_t receive{ 0 };
	size_t open{ 0 };
	size_t change{ 0 };
	size_t state{ 0 };
};

class confirmation_height_info final
{
public:
	confirmation_height_info () = default;
	confirmation_height_info (uint64_t, futurehead::block_hash const &);
	void serialize (futurehead::stream &) const;
	bool deserialize (futurehead::stream &);
	uint64_t height;
	futurehead::block_hash frontier;
};

namespace confirmation_height
{
	/** When the uncemented count (block count - cemented count) is less than this use the unbounded processor */
	uint64_t const unbounded_cutoff{ 16384 };
}

using vote_blocks_vec_iter = std::vector<boost::variant<std::shared_ptr<futurehead::block>, futurehead::block_hash>>::const_iterator;
class iterate_vote_blocks_as_hash final
{
public:
	iterate_vote_blocks_as_hash () = default;
	futurehead::block_hash operator() (boost::variant<std::shared_ptr<futurehead::block>, futurehead::block_hash> const & item) const;
};
class vote final
{
public:
	vote () = default;
	vote (futurehead::vote const &);
	vote (bool &, futurehead::stream &, futurehead::block_uniquer * = nullptr);
	vote (bool &, futurehead::stream &, futurehead::block_type, futurehead::block_uniquer * = nullptr);
	vote (futurehead::account const &, futurehead::raw_key const &, uint64_t, std::shared_ptr<futurehead::block>);
	vote (futurehead::account const &, futurehead::raw_key const &, uint64_t, std::vector<futurehead::block_hash> const &);
	std::string hashes_string () const;
	futurehead::block_hash hash () const;
	futurehead::block_hash full_hash () const;
	bool operator== (futurehead::vote const &) const;
	bool operator!= (futurehead::vote const &) const;
	void serialize (futurehead::stream &, futurehead::block_type) const;
	void serialize (futurehead::stream &) const;
	void serialize_json (boost::property_tree::ptree & tree) const;
	bool deserialize (futurehead::stream &, futurehead::block_uniquer * = nullptr);
	bool validate () const;
	boost::transform_iterator<futurehead::iterate_vote_blocks_as_hash, futurehead::vote_blocks_vec_iter> begin () const;
	boost::transform_iterator<futurehead::iterate_vote_blocks_as_hash, futurehead::vote_blocks_vec_iter> end () const;
	std::string to_json () const;
	// Vote round sequence number
	uint64_t sequence;
	// The blocks, or block hashes, that this vote is for
	std::vector<boost::variant<std::shared_ptr<futurehead::block>, futurehead::block_hash>> blocks;
	// Account that's voting
	futurehead::account account;
	// Signature of sequence + block hashes
	futurehead::signature signature;
	static const std::string hash_prefix;
};
/**
 * This class serves to find and return unique variants of a vote in order to minimize memory usage
 */
class vote_uniquer final
{
public:
	using value_type = std::pair<const futurehead::block_hash, std::weak_ptr<futurehead::vote>>;

	vote_uniquer (futurehead::block_uniquer &);
	std::shared_ptr<futurehead::vote> unique (std::shared_ptr<futurehead::vote>);
	size_t size ();

private:
	futurehead::block_uniquer & uniquer;
	std::mutex mutex;
	std::unordered_map<std::remove_const_t<value_type::first_type>, value_type::second_type> votes;
	static unsigned constexpr cleanup_count = 2;
};

std::unique_ptr<container_info_component> collect_container_info (vote_uniquer & vote_uniquer, const std::string & name);

enum class vote_code
{
	invalid, // Vote is not signed correctly
	replay, // Vote does not have the highest sequence number, it's a replay
	vote, // Vote has the highest sequence number
	indeterminate // Unknown if replay or vote
};

enum class process_result
{
	progress, // Hasn't been seen before, signed correctly
	bad_signature, // Signature was bad, forged or transmission error
	old, // Already seen and was valid
	negative_spend, // Malicious attempt to spend a negative amount
	fork, // Malicious fork based on previous
	unreceivable, // Source block doesn't exist, has already been received, or requires an account upgrade (epoch blocks)
	gap_previous, // Block marked as previous is unknown
	gap_source, // Block marked as source is unknown
	opened_burn_account, // The impossible happened, someone found the private key associated with the public key '0'.
	balance_mismatch, // Balance and amount delta don't match
	representative_mismatch, // Representative is changed when it is not allowed
	block_position, // This block cannot follow the previous block
	insufficient_work // Insufficient work for this block, even though it passed the minimal validation
};
class process_return final
{
public:
	futurehead::process_result code;
	futurehead::account account;
	futurehead::amount amount;
	futurehead::account pending_account;
	boost::optional<bool> state_is_send;
	futurehead::signature_verification verified;
	futurehead::amount previous_balance;
};
enum class tally_result
{
	vote,
	changed,
	confirm
};

class genesis final
{
public:
	genesis ();
	futurehead::block_hash hash () const;
	std::shared_ptr<futurehead::block> open;
};

class network_params;

/** Protocol versions whose value may depend on the active network */
class protocol_constants
{
public:
	/** Current protocol version */
	uint8_t const protocol_version = 0x12;

	/** Minimum accepted protocol version */
	uint8_t protocol_version_min (bool epoch_2_started) const;

	/** Do not request telemetry metrics to nodes older than this version */
	uint8_t const telemetry_protocol_version_min = 0x12;

private:
	/* Minimum protocol version before an epoch 2 block is seen */
	uint8_t const protocol_version_min_pre_epoch_2 = 0x11;
	/* Minimum protocol version after an epoch 2 block is seen */
	uint8_t const protocol_version_min_epoch_2 = 0x12;
};

// Some places use the decltype of protocol_version instead of protocol_version_min. To keep those checks simpler we check that the decltypes match ignoring differences in const
static_assert (std::is_same<std::remove_const_t<decltype (protocol_constants ().protocol_version)>, decltype (protocol_constants ().protocol_version_min (false))>::value, "protocol_min should match");

/** Genesis keys and ledger constants for network variants */
class ledger_constants
{
public:
	ledger_constants (futurehead::network_constants & network_constants);
	ledger_constants (futurehead::futurehead_networks network_a);
	futurehead::keypair zero_key;
	futurehead::keypair test_genesis_key;
	futurehead::account futurehead_test_account;
	futurehead::account futurehead_beta_account;
	futurehead::account futurehead_live_account;
	std::string futurehead_test_genesis;
	std::string futurehead_beta_genesis;
	std::string futurehead_live_genesis;
	futurehead::account genesis_account;
	std::string genesis_block;
	futurehead::block_hash genesis_hash;
	futurehead::uint128_t genesis_amount;
	futurehead::account burn_account;
	futurehead::epochs epochs;
};

/** Constants which depend on random values (this class should never be used globally due to CryptoPP globals potentially not being initialized) */
class random_constants
{
public:
	random_constants ();
	futurehead::account not_an_account;
	futurehead::uint128_union random_128;
};

/** Node related constants whose value depends on the active network */
class node_constants
{
public:
	node_constants (futurehead::network_constants & network_constants);
	std::chrono::seconds period;
	std::chrono::milliseconds half_period;
	/** Default maximum idle time for a socket before it's automatically closed */
	std::chrono::seconds idle_timeout;
	std::chrono::seconds cutoff;
	std::chrono::seconds syn_cookie_cutoff;
	std::chrono::minutes backup_interval;
	std::chrono::seconds search_pending_interval;
	std::chrono::seconds peer_interval;
	std::chrono::minutes unchecked_cleaning_interval;
	std::chrono::milliseconds process_confirmed_interval;
	/** Maximum number of peers per IP */
	size_t max_peers_per_ip;

	/** The maximum amount of samples for a 2 week period on live or 3 days on beta */
	uint64_t max_weight_samples;
	uint64_t weight_period;
};

/** Voting related constants whose value depends on the active network */
class voting_constants
{
public:
	voting_constants (futurehead::network_constants & network_constants);
	size_t max_cache;
};

/** Port-mapping related constants whose value depends on the active network */
class portmapping_constants
{
public:
	portmapping_constants (futurehead::network_constants & network_constants);
	// Timeouts are primes so they infrequently happen at the same time
	std::chrono::seconds lease_duration;
	std::chrono::seconds health_check_period;
};

/** Bootstrap related constants whose value depends on the active network */
class bootstrap_constants
{
public:
	bootstrap_constants (futurehead::network_constants & network_constants);
	uint32_t lazy_max_pull_blocks;
	uint32_t lazy_min_pull_blocks;
	unsigned frontier_retry_limit;
	unsigned lazy_retry_limit;
	unsigned lazy_destinations_retry_limit;
	std::chrono::milliseconds gap_cache_bootstrap_start_interval;
};

/** Constants whose value depends on the active network */
class network_params
{
public:
	/** Populate values based on the current active network */
	network_params ();

	/** Populate values based on \p network_a */
	network_params (futurehead::futurehead_networks network_a);

	std::array<uint8_t, 2> header_magic_number;
	unsigned kdf_work;
	network_constants network;
	protocol_constants protocol;
	ledger_constants ledger;
	random_constants random;
	voting_constants voting;
	node_constants node;
	portmapping_constants portmapping;
	bootstrap_constants bootstrap;
};

enum class confirmation_height_mode
{
	automatic,
	unbounded,
	bounded
};

/* Holds flags for various cacheable data. For most CLI operations caching is unnecessary
 * (e.g getting the cemented block count) so it can be disabled for performance reasons. */
class generate_cache
{
public:
	bool reps = true;
	bool cemented_count = true;
	bool unchecked_count = true;
	bool account_count = true;
	bool epoch_2 = true;

	void enable_all ();
};

/* Holds an in-memory cache of various counts */
class ledger_cache
{
public:
	futurehead::rep_weights rep_weights;
	std::atomic<uint64_t> cemented_count{ 0 };
	std::atomic<uint64_t> block_count{ 0 };
	std::atomic<uint64_t> unchecked_count{ 0 };
	std::atomic<uint64_t> account_count{ 0 };
	std::atomic<bool> epoch_2_started{ false };
};

/* Defines the possible states for an election to stop in */
enum class election_status_type : uint8_t
{
	ongoing = 0,
	active_confirmed_quorum = 1,
	active_confirmation_height = 2,
	inactive_confirmation_height = 3,
	stopped = 5
};

/* Holds a summary of an election */
class election_status final
{
public:
	std::shared_ptr<futurehead::block> winner;
	futurehead::amount tally;
	std::chrono::milliseconds election_end;
	std::chrono::milliseconds election_duration;
	unsigned confirmation_request_count;
	unsigned block_count;
	unsigned voter_count;
	election_status_type type;
};

futurehead::wallet_id random_wallet_id ();
}
