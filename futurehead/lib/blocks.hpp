#pragma once

#include <futurehead/crypto/blake2/blake2.h>
#include <futurehead/lib/epoch.hpp>
#include <futurehead/lib/errors.hpp>
#include <futurehead/lib/numbers.hpp>
#include <futurehead/lib/optional_ptr.hpp>
#include <futurehead/lib/stream.hpp>
#include <futurehead/lib/utility.hpp>
#include <futurehead/lib/work.hpp>

#include <boost/property_tree/ptree_fwd.hpp>

#include <unordered_map>

namespace futurehead
{
class block_visitor;
class mutable_block_visitor;
enum class block_type : uint8_t
{
	invalid = 0,
	not_a_block = 1,
	send = 2,
	receive = 3,
	open = 4,
	change = 5,
	state = 6
};
class block_details
{
	static_assert (std::is_same<std::underlying_type<futurehead::epoch>::type, uint8_t> (), "Epoch enum is not the proper type");
	static_assert (static_cast<uint8_t> (futurehead::epoch::max) < (1 << 5), "Epoch max is too large for the sideband");

public:
	block_details () = default;
	block_details (futurehead::epoch const epoch_a, bool const is_send_a, bool const is_receive_a, bool const is_epoch_a);
	static constexpr size_t size ();
	bool operator== (block_details const & other_a) const;
	void serialize (futurehead::stream &) const;
	bool deserialize (futurehead::stream &);
	futurehead::epoch epoch{ futurehead::epoch::epoch_0 };
	bool is_send{ false };
	bool is_receive{ false };
	bool is_epoch{ false };

private:
	uint8_t packed () const;
	void unpack (uint8_t);
};

std::string state_subtype (futurehead::block_details const);

class block_sideband final
{
public:
	block_sideband () = default;
	block_sideband (futurehead::account const &, futurehead::block_hash const &, futurehead::amount const &, uint64_t, uint64_t, futurehead::block_details const &);
	block_sideband (futurehead::account const &, futurehead::block_hash const &, futurehead::amount const &, uint64_t, uint64_t, futurehead::epoch, bool is_send, bool is_receive, bool is_epoch);
	void serialize (futurehead::stream &, futurehead::block_type) const;
	bool deserialize (futurehead::stream &, futurehead::block_type);
	static size_t size (futurehead::block_type);
	futurehead::block_hash successor{ 0 };
	futurehead::account account{ 0 };
	futurehead::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
	futurehead::block_details details;
};
class block
{
public:
	// Return a digest of the hashables in this block.
	futurehead::block_hash const & hash () const;
	// Return a digest of hashables and non-hashables in this block.
	futurehead::block_hash full_hash () const;
	futurehead::block_sideband const & sideband () const;
	void sideband_set (futurehead::block_sideband const &);
	bool has_sideband () const;
	std::string to_json () const;
	virtual void hash (blake2b_state &) const = 0;
	virtual uint64_t block_work () const = 0;
	virtual void block_work_set (uint64_t) = 0;
	virtual futurehead::account const & account () const;
	// Previous block in account's chain, zero for open block
	virtual futurehead::block_hash const & previous () const = 0;
	// Source block for open/receive blocks, zero otherwise.
	virtual futurehead::block_hash const & source () const;
	// Previous block or account number for open blocks
	virtual futurehead::root const & root () const = 0;
	// Qualified root value based on previous() and root()
	virtual futurehead::qualified_root qualified_root () const;
	// Link field for state blocks, zero otherwise.
	virtual futurehead::link const & link () const;
	virtual futurehead::account const & representative () const;
	virtual futurehead::amount const & balance () const;
	virtual void serialize (futurehead::stream &) const = 0;
	virtual void serialize_json (std::string &, bool = false) const = 0;
	virtual void serialize_json (boost::property_tree::ptree &) const = 0;
	virtual void visit (futurehead::block_visitor &) const = 0;
	virtual void visit (futurehead::mutable_block_visitor &) = 0;
	virtual bool operator== (futurehead::block const &) const = 0;
	virtual futurehead::block_type type () const = 0;
	virtual futurehead::signature const & block_signature () const = 0;
	virtual void signature_set (futurehead::signature const &) = 0;
	virtual ~block () = default;
	virtual bool valid_predecessor (futurehead::block const &) const = 0;
	static size_t size (futurehead::block_type);
	virtual futurehead::work_version work_version () const;
	uint64_t difficulty () const;
	// If there are any changes to the hashables, call this to update the cached hash
	void refresh ();

protected:
	mutable futurehead::block_hash cached_hash{ 0 };
	/**
	 * Contextual details about a block, some fields may or may not be set depending on block type.
	 * This field is set via sideband_set in ledger processing or deserializing blocks from the database.
	 * Otherwise it may be null (for example, an old block or fork).
	 */
	futurehead::optional_ptr<futurehead::block_sideband> sideband_m;

private:
	futurehead::block_hash generate_hash () const;
};
class send_hashables
{
public:
	send_hashables () = default;
	send_hashables (futurehead::block_hash const &, futurehead::account const &, futurehead::amount const &);
	send_hashables (bool &, futurehead::stream &);
	send_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	futurehead::block_hash previous;
	futurehead::account destination;
	futurehead::amount balance;
	static size_t constexpr size = sizeof (previous) + sizeof (destination) + sizeof (balance);
};
class send_block : public futurehead::block
{
public:
	send_block () = default;
	send_block (futurehead::block_hash const &, futurehead::account const &, futurehead::amount const &, futurehead::raw_key const &, futurehead::public_key const &, uint64_t);
	send_block (bool &, futurehead::stream &);
	send_block (bool &, boost::property_tree::ptree const &);
	virtual ~send_block () = default;
	using futurehead::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	futurehead::block_hash const & previous () const override;
	futurehead::root const & root () const override;
	futurehead::amount const & balance () const override;
	void serialize (futurehead::stream &) const override;
	bool deserialize (futurehead::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (futurehead::block_visitor &) const override;
	void visit (futurehead::mutable_block_visitor &) override;
	futurehead::block_type type () const override;
	futurehead::signature const & block_signature () const override;
	void signature_set (futurehead::signature const &) override;
	bool operator== (futurehead::block const &) const override;
	bool operator== (futurehead::send_block const &) const;
	bool valid_predecessor (futurehead::block const &) const override;
	send_hashables hashables;
	futurehead::signature signature;
	uint64_t work;
	static size_t constexpr size = futurehead::send_hashables::size + sizeof (signature) + sizeof (work);
};
class receive_hashables
{
public:
	receive_hashables () = default;
	receive_hashables (futurehead::block_hash const &, futurehead::block_hash const &);
	receive_hashables (bool &, futurehead::stream &);
	receive_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	futurehead::block_hash previous;
	futurehead::block_hash source;
	static size_t constexpr size = sizeof (previous) + sizeof (source);
};
class receive_block : public futurehead::block
{
public:
	receive_block () = default;
	receive_block (futurehead::block_hash const &, futurehead::block_hash const &, futurehead::raw_key const &, futurehead::public_key const &, uint64_t);
	receive_block (bool &, futurehead::stream &);
	receive_block (bool &, boost::property_tree::ptree const &);
	virtual ~receive_block () = default;
	using futurehead::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	futurehead::block_hash const & previous () const override;
	futurehead::block_hash const & source () const override;
	futurehead::root const & root () const override;
	void serialize (futurehead::stream &) const override;
	bool deserialize (futurehead::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (futurehead::block_visitor &) const override;
	void visit (futurehead::mutable_block_visitor &) override;
	futurehead::block_type type () const override;
	futurehead::signature const & block_signature () const override;
	void signature_set (futurehead::signature const &) override;
	bool operator== (futurehead::block const &) const override;
	bool operator== (futurehead::receive_block const &) const;
	bool valid_predecessor (futurehead::block const &) const override;
	receive_hashables hashables;
	futurehead::signature signature;
	uint64_t work;
	static size_t constexpr size = futurehead::receive_hashables::size + sizeof (signature) + sizeof (work);
};
class open_hashables
{
public:
	open_hashables () = default;
	open_hashables (futurehead::block_hash const &, futurehead::account const &, futurehead::account const &);
	open_hashables (bool &, futurehead::stream &);
	open_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	futurehead::block_hash source;
	futurehead::account representative;
	futurehead::account account;
	static size_t constexpr size = sizeof (source) + sizeof (representative) + sizeof (account);
};
class open_block : public futurehead::block
{
public:
	open_block () = default;
	open_block (futurehead::block_hash const &, futurehead::account const &, futurehead::account const &, futurehead::raw_key const &, futurehead::public_key const &, uint64_t);
	open_block (futurehead::block_hash const &, futurehead::account const &, futurehead::account const &, std::nullptr_t);
	open_block (bool &, futurehead::stream &);
	open_block (bool &, boost::property_tree::ptree const &);
	virtual ~open_block () = default;
	using futurehead::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	futurehead::block_hash const & previous () const override;
	futurehead::account const & account () const override;
	futurehead::block_hash const & source () const override;
	futurehead::root const & root () const override;
	futurehead::account const & representative () const override;
	void serialize (futurehead::stream &) const override;
	bool deserialize (futurehead::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (futurehead::block_visitor &) const override;
	void visit (futurehead::mutable_block_visitor &) override;
	futurehead::block_type type () const override;
	futurehead::signature const & block_signature () const override;
	void signature_set (futurehead::signature const &) override;
	bool operator== (futurehead::block const &) const override;
	bool operator== (futurehead::open_block const &) const;
	bool valid_predecessor (futurehead::block const &) const override;
	futurehead::open_hashables hashables;
	futurehead::signature signature;
	uint64_t work;
	static size_t constexpr size = futurehead::open_hashables::size + sizeof (signature) + sizeof (work);
};
class change_hashables
{
public:
	change_hashables () = default;
	change_hashables (futurehead::block_hash const &, futurehead::account const &);
	change_hashables (bool &, futurehead::stream &);
	change_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	futurehead::block_hash previous;
	futurehead::account representative;
	static size_t constexpr size = sizeof (previous) + sizeof (representative);
};
class change_block : public futurehead::block
{
public:
	change_block () = default;
	change_block (futurehead::block_hash const &, futurehead::account const &, futurehead::raw_key const &, futurehead::public_key const &, uint64_t);
	change_block (bool &, futurehead::stream &);
	change_block (bool &, boost::property_tree::ptree const &);
	virtual ~change_block () = default;
	using futurehead::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	futurehead::block_hash const & previous () const override;
	futurehead::root const & root () const override;
	futurehead::account const & representative () const override;
	void serialize (futurehead::stream &) const override;
	bool deserialize (futurehead::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (futurehead::block_visitor &) const override;
	void visit (futurehead::mutable_block_visitor &) override;
	futurehead::block_type type () const override;
	futurehead::signature const & block_signature () const override;
	void signature_set (futurehead::signature const &) override;
	bool operator== (futurehead::block const &) const override;
	bool operator== (futurehead::change_block const &) const;
	bool valid_predecessor (futurehead::block const &) const override;
	futurehead::change_hashables hashables;
	futurehead::signature signature;
	uint64_t work;
	static size_t constexpr size = futurehead::change_hashables::size + sizeof (signature) + sizeof (work);
};
class state_hashables
{
public:
	state_hashables () = default;
	state_hashables (futurehead::account const &, futurehead::block_hash const &, futurehead::account const &, futurehead::amount const &, futurehead::link const &);
	state_hashables (bool &, futurehead::stream &);
	state_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	// Account# / public key that operates this account
	// Uses:
	// Bulk signature validation in advance of further ledger processing
	// Arranging uncomitted transactions by account
	futurehead::account account;
	// Previous transaction in this chain
	futurehead::block_hash previous;
	// Representative of this account
	futurehead::account representative;
	// Current balance of this account
	// Allows lookup of account balance simply by looking at the head block
	futurehead::amount balance;
	// Link field contains source block_hash if receiving, destination account if sending
	futurehead::link link;
	// Serialized size
	static size_t constexpr size = sizeof (account) + sizeof (previous) + sizeof (representative) + sizeof (balance) + sizeof (link);
};
class state_block : public futurehead::block
{
public:
	state_block () = default;
	state_block (futurehead::account const &, futurehead::block_hash const &, futurehead::account const &, futurehead::amount const &, futurehead::link const &, futurehead::raw_key const &, futurehead::public_key const &, uint64_t);
	state_block (bool &, futurehead::stream &);
	state_block (bool &, boost::property_tree::ptree const &);
	virtual ~state_block () = default;
	using futurehead::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	futurehead::block_hash const & previous () const override;
	futurehead::account const & account () const override;
	futurehead::root const & root () const override;
	futurehead::link const & link () const override;
	futurehead::account const & representative () const override;
	futurehead::amount const & balance () const override;
	void serialize (futurehead::stream &) const override;
	bool deserialize (futurehead::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (futurehead::block_visitor &) const override;
	void visit (futurehead::mutable_block_visitor &) override;
	futurehead::block_type type () const override;
	futurehead::signature const & block_signature () const override;
	void signature_set (futurehead::signature const &) override;
	bool operator== (futurehead::block const &) const override;
	bool operator== (futurehead::state_block const &) const;
	bool valid_predecessor (futurehead::block const &) const override;
	futurehead::state_hashables hashables;
	futurehead::signature signature;
	uint64_t work;
	static size_t constexpr size = futurehead::state_hashables::size + sizeof (signature) + sizeof (work);
};
class block_visitor
{
public:
	virtual void send_block (futurehead::send_block const &) = 0;
	virtual void receive_block (futurehead::receive_block const &) = 0;
	virtual void open_block (futurehead::open_block const &) = 0;
	virtual void change_block (futurehead::change_block const &) = 0;
	virtual void state_block (futurehead::state_block const &) = 0;
	virtual ~block_visitor () = default;
};
class mutable_block_visitor
{
public:
	virtual void send_block (futurehead::send_block &) = 0;
	virtual void receive_block (futurehead::receive_block &) = 0;
	virtual void open_block (futurehead::open_block &) = 0;
	virtual void change_block (futurehead::change_block &) = 0;
	virtual void state_block (futurehead::state_block &) = 0;
	virtual ~mutable_block_visitor () = default;
};
/**
 * This class serves to find and return unique variants of a block in order to minimize memory usage
 */
class block_uniquer
{
public:
	using value_type = std::pair<const futurehead::uint256_union, std::weak_ptr<futurehead::block>>;

	std::shared_ptr<futurehead::block> unique (std::shared_ptr<futurehead::block>);
	size_t size ();

private:
	std::mutex mutex;
	std::unordered_map<std::remove_const_t<value_type::first_type>, value_type::second_type> blocks;
	static unsigned constexpr cleanup_count = 2;
};

std::unique_ptr<container_info_component> collect_container_info (block_uniquer & block_uniquer, const std::string & name);

std::shared_ptr<futurehead::block> deserialize_block (futurehead::stream &);
std::shared_ptr<futurehead::block> deserialize_block (futurehead::stream &, futurehead::block_type, futurehead::block_uniquer * = nullptr);
std::shared_ptr<futurehead::block> deserialize_block_json (boost::property_tree::ptree const &, futurehead::block_uniquer * = nullptr);
void serialize_block (futurehead::stream &, futurehead::block const &);
void block_memory_pool_purge ();
}
