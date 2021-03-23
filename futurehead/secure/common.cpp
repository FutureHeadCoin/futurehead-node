#define IGNORE_GTEST_INCL
#include <futurehead/core_test/testutil.hpp>
#include <futurehead/crypto_lib/random_pool.hpp>
#include <futurehead/lib/config.hpp>
#include <futurehead/lib/numbers.hpp>
#include <futurehead/secure/blockstore.hpp>
#include <futurehead/secure/common.hpp>

#include <crypto/cryptopp/words.h>

#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/variant/get.hpp>

#include <limits>
#include <queue>

#include <crypto/ed25519-donna/ed25519.h>

size_t constexpr futurehead::send_block::size;
size_t constexpr futurehead::receive_block::size;
size_t constexpr futurehead::open_block::size;
size_t constexpr futurehead::change_block::size;
size_t constexpr futurehead::state_block::size;

futurehead::futurehead_networks futurehead::network_constants::active_network = futurehead::futurehead_networks::ACTIVE_NETWORK;

namespace
{
	//TO-CHANGE TEST KEY
char const * test_private_key_data = "39D88952C5A3CF42E8A149AAFDC0E1F87EFC00F1EE3BB23A2A3866A537F64905";
char const * test_public_key_data = "AD01A4F5318894AE6BE9613E6C69D12A7D2F36D7B9F85BFAA42AE78F07410FD2"; // fpsc_3da3nmtm546nosoykrbyfjnx4cmx7wufhghrdhxcacq9jw5n45ykg3sd4e9i
char const * test_genesis_data = R"%%%({
	"type": "open",
    "source": "AD01A4F5318894AE6BE9613E6C69D12A7D2F36D7B9F85BFAA42AE78F07410FD2",
    "representative": "fpsc_3da3nmtm546nosoykrbyfjnx4cmx7wufhghrdhxcacq9jw5n45ykg3sd4e9i",
    "account": "fpsc_3da3nmtm546nosoykrbyfjnx4cmx7wufhghrdhxcacq9jw5n45ykg3sd4e9i",
    "work": "924a6893dc6a9bd2",
    "signature": "54F3B9153A9CAFAB2019B865354AB1EA8C662A533865E8FFFB31CF8BE7DC4F5532ADA8CE26752ACF24861D30C2B8F45CBC374885AED9201FB6F8ECDBEE681606"
	})%%%";

//TO-CHANGE BETA KEY
char const * beta_public_key_data = "8644D25DF7F813012DFA3B8FF10E18E05C57326B1A3A9BD58327606688E37854"; // fpsc_33k6tbgzhy1m16pzngwhy693jr4wcws8p8jtmhcr8bu1et6g8y4np5cph99r
char const * beta_genesis_data = R"%%%({
	"type": "open",
    "source": "8644D25DF7F813012DFA3B8FF10E18E05C57326B1A3A9BD58327606688E37854",
    "representative": "fpsc_33k6tbgzhy1m16pzngwhy693jr4wcws8p8jtmhcr8bu1et6g8y4np5cph99r",
    "account": "fpsc_33k6tbgzhy1m16pzngwhy693jr4wcws8p8jtmhcr8bu1et6g8y4np5cph99r",
    "work": "c8b98f2e51f8edf1",
    "signature": "EC9CF6D8393D2E04098309FF2D53EA8E5D5CD797A711ED977E94C319854CD4AB8C1C875B043A0124281E3F0A6B3708BB8220F0D54903D492CFD432BF8A5B8F0A"
	})%%%";

//TO-CHANGE LIVE KEY
char const * live_public_key_data = "D972827E48E3094295AB6BBE7359B51D3D97450187B365517D69F38FB64A85B5"; // fpsc_3pdkibz6jrrbacctptxygfeuc9bxkx4i53xmeoaqtthmjyu6o3fobuan6mjm
char const * live_genesis_data = R"%%%({
    "type": "open",
    "source": "D972827E48E3094295AB6BBE7359B51D3D97450187B365517D69F38FB64A85B5",
    "representative": "fpsc_3pdkibz6jrrbacctptxygfeuc9bxkx4i53xmeoaqtthmjyu6o3fobuan6mjm",
    "account": "fpsc_3pdkibz6jrrbacctptxygfeuc9bxkx4i53xmeoaqtthmjyu6o3fobuan6mjm",
    "work": "48c61c9ceb0e9578",
    "signature": "BC234C335B4A4145037100161A46FC7A13A870F0983BFE5294991A828F138F757C8EA43F7AF18ACEC721F49266C953B79E8619255E90E58060C3E97FEF560602"
	})%%%";

std::shared_ptr<futurehead::block> parse_block_from_genesis_data (std::string const & genesis_data_a)
{
	boost::property_tree::ptree tree;
	std::stringstream istream (genesis_data_a);
	boost::property_tree::read_json (istream, tree);
	return futurehead::deserialize_block_json (tree);
}
}

futurehead::network_params::network_params () :
network_params (network_constants::active_network)
{
}

futurehead::network_params::network_params (futurehead::futurehead_networks network_a) :
network (network_a), ledger (network), voting (network), node (network), portmapping (network), bootstrap (network)
{
	unsigned constexpr kdf_full_work = 64 * 1024;
	unsigned constexpr kdf_test_work = 8;
	kdf_work = network.is_test_network () ? kdf_test_work : kdf_full_work;
	header_magic_number = network.is_test_network () ? std::array<uint8_t, 2>{ { 'R', 'A' } } : network.is_beta_network () ? std::array<uint8_t, 2>{ { 'N', 'D' } } : std::array<uint8_t, 2>{ { 'R', 'C' } };
}

uint8_t futurehead::protocol_constants::protocol_version_min (bool use_epoch_2_min_version_a) const
{
	return use_epoch_2_min_version_a ? protocol_version_min_epoch_2 : protocol_version_min_pre_epoch_2;
}

futurehead::ledger_constants::ledger_constants (futurehead::network_constants & network_constants) :
ledger_constants (network_constants.network ())
{
}

futurehead::ledger_constants::ledger_constants (futurehead::futurehead_networks network_a) :
zero_key ("0"),
test_genesis_key (test_private_key_data),
futurehead_test_account (test_public_key_data),
futurehead_beta_account (beta_public_key_data),
futurehead_live_account (live_public_key_data),
futurehead_test_genesis (test_genesis_data),
futurehead_beta_genesis (beta_genesis_data),
futurehead_live_genesis (live_genesis_data),
genesis_account (network_a == futurehead::futurehead_networks::futurehead_test_network ? futurehead_test_account : network_a == futurehead::futurehead_networks::futurehead_beta_network ? futurehead_beta_account : futurehead_live_account),
genesis_block (network_a == futurehead::futurehead_networks::futurehead_test_network ? futurehead_test_genesis : network_a == futurehead::futurehead_networks::futurehead_beta_network ? futurehead_beta_genesis : futurehead_live_genesis),
genesis_hash (parse_block_from_genesis_data (genesis_block)->hash ()),
genesis_amount (std::numeric_limits<futurehead::uint128_t>::max ()),
burn_account (0)
{
	futurehead::link epoch_link_v1;
	const char * epoch_message_v1 ("epoch v1 block");
	strncpy ((char *)epoch_link_v1.bytes.data (), epoch_message_v1, epoch_link_v1.bytes.size ());
	epochs.add (futurehead::epoch::epoch_1, genesis_account, epoch_link_v1);

	futurehead::link epoch_link_v2;
	futurehead::account futurehead_live_epoch_v2_signer;
	auto error (futurehead_live_epoch_v2_signer.decode_account ("fpsc_3qb6o6i1tkzr6jwr5s7eehfxwg9x6eemitdinbpi7u8bjjwsgqfj4wzser3x"));
	debug_assert (!error);
	auto epoch_v2_signer (network_a == futurehead::futurehead_networks::futurehead_test_network ? futurehead_test_account : network_a == futurehead::futurehead_networks::futurehead_beta_network ? futurehead_beta_account : futurehead_live_epoch_v2_signer);
	const char * epoch_message_v2 ("epoch v2 block");
	strncpy ((char *)epoch_link_v2.bytes.data (), epoch_message_v2, epoch_link_v2.bytes.size ());
	epochs.add (futurehead::epoch::epoch_2, epoch_v2_signer, epoch_link_v2);
}

futurehead::random_constants::random_constants ()
{
	futurehead::random_pool::generate_block (not_an_account.bytes.data (), not_an_account.bytes.size ());
	futurehead::random_pool::generate_block (random_128.bytes.data (), random_128.bytes.size ());
}

futurehead::node_constants::node_constants (futurehead::network_constants & network_constants)
{
	period = network_constants.is_test_network () ? std::chrono::seconds (1) : std::chrono::seconds (60);
	half_period = network_constants.is_test_network () ? std::chrono::milliseconds (500) : std::chrono::milliseconds (30 * 1000);
	idle_timeout = network_constants.is_test_network () ? period * 15 : period * 2;
	cutoff = period * 5;
	syn_cookie_cutoff = std::chrono::seconds (5);
	backup_interval = std::chrono::minutes (5);
	search_pending_interval = network_constants.is_test_network () ? std::chrono::seconds (1) : std::chrono::seconds (5 * 60);
	peer_interval = search_pending_interval;
	unchecked_cleaning_interval = std::chrono::minutes (30);
	process_confirmed_interval = network_constants.is_test_network () ? std::chrono::milliseconds (50) : std::chrono::milliseconds (500);
	max_peers_per_ip = network_constants.is_test_network () ? 10 : 5;
	max_weight_samples = network_constants.is_live_network () ? 4032 : 864;
	weight_period = 5 * 60; // 5 minutes
}

futurehead::voting_constants::voting_constants (futurehead::network_constants & network_constants)
{
	max_cache = network_constants.is_test_network () ? 2 : 64 * 1024;
}

futurehead::portmapping_constants::portmapping_constants (futurehead::network_constants & network_constants)
{
	lease_duration = std::chrono::seconds (1787); // ~30 minutes
	health_check_period = std::chrono::seconds (53);
}

futurehead::bootstrap_constants::bootstrap_constants (futurehead::network_constants & network_constants)
{
	lazy_max_pull_blocks = network_constants.is_test_network () ? 2 : 512;
	lazy_min_pull_blocks = network_constants.is_test_network () ? 1 : 32;
	frontier_retry_limit = network_constants.is_test_network () ? 2 : 16;
	lazy_retry_limit = network_constants.is_test_network () ? 2 : frontier_retry_limit * 10;
	lazy_destinations_retry_limit = network_constants.is_test_network () ? 1 : frontier_retry_limit / 4;
	gap_cache_bootstrap_start_interval = network_constants.is_test_network () ? std::chrono::milliseconds (5) : std::chrono::milliseconds (30 * 1000);
}

/* Convenience constants for core_test which is always on the test network */
namespace
{
futurehead::ledger_constants test_constants (futurehead::futurehead_networks::futurehead_test_network);
}

futurehead::keypair const & futurehead::zero_key (test_constants.zero_key);
futurehead::keypair const & futurehead::test_genesis_key (test_constants.test_genesis_key);
futurehead::account const & futurehead::futurehead_test_account (test_constants.futurehead_test_account);
std::string const & futurehead::futurehead_test_genesis (test_constants.futurehead_test_genesis);
futurehead::account const & futurehead::genesis_account (test_constants.genesis_account);
futurehead::block_hash const & futurehead::genesis_hash (test_constants.genesis_hash);
futurehead::uint128_t const & futurehead::genesis_amount (test_constants.genesis_amount);
futurehead::account const & futurehead::burn_account (test_constants.burn_account);

// Create a new random keypair
futurehead::keypair::keypair ()
{
	random_pool::generate_block (prv.data.bytes.data (), prv.data.bytes.size ());
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a private key
futurehead::keypair::keypair (futurehead::raw_key && prv_a) :
prv (std::move (prv_a))
{
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a hex string of the private key
futurehead::keypair::keypair (std::string const & prv_a)
{
	auto error (prv.data.decode_hex (prv_a));
	(void)error;
	debug_assert (!error);
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Serialize a block prefixed with an 8-bit typecode
void futurehead::serialize_block (futurehead::stream & stream_a, futurehead::block const & block_a)
{
	write (stream_a, block_a.type ());
	block_a.serialize (stream_a);
}

futurehead::account_info::account_info (futurehead::block_hash const & head_a, futurehead::account const & representative_a, futurehead::block_hash const & open_block_a, futurehead::amount const & balance_a, uint64_t modified_a, uint64_t block_count_a, futurehead::epoch epoch_a) :
head (head_a),
representative (representative_a),
open_block (open_block_a),
balance (balance_a),
modified (modified_a),
block_count (block_count_a),
epoch_m (epoch_a)
{
}

bool futurehead::account_info::deserialize (futurehead::stream & stream_a)
{
	auto error (false);
	try
	{
		futurehead::read (stream_a, head.bytes);
		futurehead::read (stream_a, representative.bytes);
		futurehead::read (stream_a, open_block.bytes);
		futurehead::read (stream_a, balance.bytes);
		futurehead::read (stream_a, modified);
		futurehead::read (stream_a, block_count);
		futurehead::read (stream_a, epoch_m);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool futurehead::account_info::operator== (futurehead::account_info const & other_a) const
{
	return head == other_a.head && representative == other_a.representative && open_block == other_a.open_block && balance == other_a.balance && modified == other_a.modified && block_count == other_a.block_count && epoch () == other_a.epoch ();
}

bool futurehead::account_info::operator!= (futurehead::account_info const & other_a) const
{
	return !(*this == other_a);
}

size_t futurehead::account_info::db_size () const
{
	debug_assert (reinterpret_cast<const uint8_t *> (this) == reinterpret_cast<const uint8_t *> (&head));
	debug_assert (reinterpret_cast<const uint8_t *> (&head) + sizeof (head) == reinterpret_cast<const uint8_t *> (&representative));
	debug_assert (reinterpret_cast<const uint8_t *> (&representative) + sizeof (representative) == reinterpret_cast<const uint8_t *> (&open_block));
	debug_assert (reinterpret_cast<const uint8_t *> (&open_block) + sizeof (open_block) == reinterpret_cast<const uint8_t *> (&balance));
	debug_assert (reinterpret_cast<const uint8_t *> (&balance) + sizeof (balance) == reinterpret_cast<const uint8_t *> (&modified));
	debug_assert (reinterpret_cast<const uint8_t *> (&modified) + sizeof (modified) == reinterpret_cast<const uint8_t *> (&block_count));
	debug_assert (reinterpret_cast<const uint8_t *> (&block_count) + sizeof (block_count) == reinterpret_cast<const uint8_t *> (&epoch_m));
	return sizeof (head) + sizeof (representative) + sizeof (open_block) + sizeof (balance) + sizeof (modified) + sizeof (block_count) + sizeof (epoch_m);
}

futurehead::epoch futurehead::account_info::epoch () const
{
	return epoch_m;
}

size_t futurehead::block_counts::sum () const
{
	return send + receive + open + change + state;
}

futurehead::pending_info::pending_info (futurehead::account const & source_a, futurehead::amount const & amount_a, futurehead::epoch epoch_a) :
source (source_a),
amount (amount_a),
epoch (epoch_a)
{
}

bool futurehead::pending_info::deserialize (futurehead::stream & stream_a)
{
	auto error (false);
	try
	{
		futurehead::read (stream_a, source.bytes);
		futurehead::read (stream_a, amount.bytes);
		futurehead::read (stream_a, epoch);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

size_t futurehead::pending_info::db_size () const
{
	return sizeof (source) + sizeof (amount) + sizeof (epoch);
}

bool futurehead::pending_info::operator== (futurehead::pending_info const & other_a) const
{
	return source == other_a.source && amount == other_a.amount && epoch == other_a.epoch;
}

futurehead::pending_key::pending_key (futurehead::account const & account_a, futurehead::block_hash const & hash_a) :
account (account_a),
hash (hash_a)
{
}

bool futurehead::pending_key::deserialize (futurehead::stream & stream_a)
{
	auto error (false);
	try
	{
		futurehead::read (stream_a, account.bytes);
		futurehead::read (stream_a, hash.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool futurehead::pending_key::operator== (futurehead::pending_key const & other_a) const
{
	return account == other_a.account && hash == other_a.hash;
}

futurehead::account const & futurehead::pending_key::key () const
{
	return account;
}

futurehead::unchecked_info::unchecked_info (std::shared_ptr<futurehead::block> block_a, futurehead::account const & account_a, uint64_t modified_a, futurehead::signature_verification verified_a, bool confirmed_a) :
block (block_a),
account (account_a),
modified (modified_a),
verified (verified_a),
confirmed (confirmed_a)
{
}

void futurehead::unchecked_info::serialize (futurehead::stream & stream_a) const
{
	debug_assert (block != nullptr);
	futurehead::serialize_block (stream_a, *block);
	futurehead::write (stream_a, account.bytes);
	futurehead::write (stream_a, modified);
	futurehead::write (stream_a, verified);
}

bool futurehead::unchecked_info::deserialize (futurehead::stream & stream_a)
{
	block = futurehead::deserialize_block (stream_a);
	bool error (block == nullptr);
	if (!error)
	{
		try
		{
			futurehead::read (stream_a, account.bytes);
			futurehead::read (stream_a, modified);
			futurehead::read (stream_a, verified);
		}
		catch (std::runtime_error const &)
		{
			error = true;
		}
	}
	return error;
}

futurehead::endpoint_key::endpoint_key (const std::array<uint8_t, 16> & address_a, uint16_t port_a) :
address (address_a), network_port (boost::endian::native_to_big (port_a))
{
}

const std::array<uint8_t, 16> & futurehead::endpoint_key::address_bytes () const
{
	return address;
}

uint16_t futurehead::endpoint_key::port () const
{
	return boost::endian::big_to_native (network_port);
}

futurehead::confirmation_height_info::confirmation_height_info (uint64_t confirmation_height_a, futurehead::block_hash const & confirmed_frontier_a) :
height (confirmation_height_a),
frontier (confirmed_frontier_a)
{
}

void futurehead::confirmation_height_info::serialize (futurehead::stream & stream_a) const
{
	futurehead::write (stream_a, height);
	futurehead::write (stream_a, frontier);
}

bool futurehead::confirmation_height_info::deserialize (futurehead::stream & stream_a)
{
	auto error (false);
	try
	{
		futurehead::read (stream_a, height);
		futurehead::read (stream_a, frontier);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

futurehead::block_info::block_info (futurehead::account const & account_a, futurehead::amount const & balance_a) :
account (account_a),
balance (balance_a)
{
}

bool futurehead::vote::operator== (futurehead::vote const & other_a) const
{
	auto blocks_equal (true);
	if (blocks.size () != other_a.blocks.size ())
	{
		blocks_equal = false;
	}
	else
	{
		for (auto i (0); blocks_equal && i < blocks.size (); ++i)
		{
			auto block (blocks[i]);
			auto other_block (other_a.blocks[i]);
			if (block.which () != other_block.which ())
			{
				blocks_equal = false;
			}
			else if (block.which ())
			{
				if (boost::get<futurehead::block_hash> (block) != boost::get<futurehead::block_hash> (other_block))
				{
					blocks_equal = false;
				}
			}
			else
			{
				if (!(*boost::get<std::shared_ptr<futurehead::block>> (block) == *boost::get<std::shared_ptr<futurehead::block>> (other_block)))
				{
					blocks_equal = false;
				}
			}
		}
	}
	return sequence == other_a.sequence && blocks_equal && account == other_a.account && signature == other_a.signature;
}

bool futurehead::vote::operator!= (futurehead::vote const & other_a) const
{
	return !(*this == other_a);
}

void futurehead::vote::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("account", account.to_account ());
	tree.put ("signature", signature.number ());
	tree.put ("sequence", std::to_string (sequence));
	boost::property_tree::ptree blocks_tree;
	for (auto block : blocks)
	{
		boost::property_tree::ptree entry;
		if (block.which ())
		{
			entry.put ("", boost::get<futurehead::block_hash> (block).to_string ());
		}
		else
		{
			entry.put ("", boost::get<std::shared_ptr<futurehead::block>> (block)->hash ().to_string ());
		}
		blocks_tree.push_back (std::make_pair ("", entry));
	}
	tree.add_child ("blocks", blocks_tree);
}

std::string futurehead::vote::to_json () const
{
	std::stringstream stream;
	boost::property_tree::ptree tree;
	serialize_json (tree);
	boost::property_tree::write_json (stream, tree);
	return stream.str ();
}

futurehead::vote::vote (futurehead::vote const & other_a) :
sequence (other_a.sequence),
blocks (other_a.blocks),
account (other_a.account),
signature (other_a.signature)
{
}

futurehead::vote::vote (bool & error_a, futurehead::stream & stream_a, futurehead::block_uniquer * uniquer_a)
{
	error_a = deserialize (stream_a, uniquer_a);
}

futurehead::vote::vote (bool & error_a, futurehead::stream & stream_a, futurehead::block_type type_a, futurehead::block_uniquer * uniquer_a)
{
	try
	{
		futurehead::read (stream_a, account.bytes);
		futurehead::read (stream_a, signature.bytes);
		futurehead::read (stream_a, sequence);

		while (stream_a.in_avail () > 0)
		{
			if (type_a == futurehead::block_type::not_a_block)
			{
				futurehead::block_hash block_hash;
				futurehead::read (stream_a, block_hash);
				blocks.push_back (block_hash);
			}
			else
			{
				std::shared_ptr<futurehead::block> block (futurehead::deserialize_block (stream_a, type_a, uniquer_a));
				if (block == nullptr)
				{
					throw std::runtime_error ("Block is null");
				}
				blocks.push_back (block);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}

	if (blocks.empty ())
	{
		error_a = true;
	}
}

futurehead::vote::vote (futurehead::account const & account_a, futurehead::raw_key const & prv_a, uint64_t sequence_a, std::shared_ptr<futurehead::block> block_a) :
sequence (sequence_a),
blocks (1, block_a),
account (account_a),
signature (futurehead::sign_message (prv_a, account_a, hash ()))
{
}

futurehead::vote::vote (futurehead::account const & account_a, futurehead::raw_key const & prv_a, uint64_t sequence_a, std::vector<futurehead::block_hash> const & blocks_a) :
sequence (sequence_a),
account (account_a)
{
	debug_assert (!blocks_a.empty ());
	debug_assert (blocks_a.size () <= 12);
	blocks.reserve (blocks_a.size ());
	std::copy (blocks_a.cbegin (), blocks_a.cend (), std::back_inserter (blocks));
	signature = futurehead::sign_message (prv_a, account_a, hash ());
}

std::string futurehead::vote::hashes_string () const
{
	std::string result;
	for (auto hash : *this)
	{
		result += hash.to_string ();
		result += ", ";
	}
	return result;
}

const std::string futurehead::vote::hash_prefix = "vote ";

futurehead::block_hash futurehead::vote::hash () const
{
	futurehead::block_hash result;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (result.bytes));
	if (blocks.size () > 1 || (!blocks.empty () && blocks.front ().which ()))
	{
		blake2b_update (&hash, hash_prefix.data (), hash_prefix.size ());
	}
	for (auto block_hash : *this)
	{
		blake2b_update (&hash, block_hash.bytes.data (), sizeof (block_hash.bytes));
	}
	union
	{
		uint64_t qword;
		std::array<uint8_t, 8> bytes;
	};
	qword = sequence;
	blake2b_update (&hash, bytes.data (), sizeof (bytes));
	blake2b_final (&hash, result.bytes.data (), sizeof (result.bytes));
	return result;
}

futurehead::block_hash futurehead::vote::full_hash () const
{
	futurehead::block_hash result;
	blake2b_state state;
	blake2b_init (&state, sizeof (result.bytes));
	blake2b_update (&state, hash ().bytes.data (), sizeof (hash ().bytes));
	blake2b_update (&state, account.bytes.data (), sizeof (account.bytes.data ()));
	blake2b_update (&state, signature.bytes.data (), sizeof (signature.bytes.data ()));
	blake2b_final (&state, result.bytes.data (), sizeof (result.bytes));
	return result;
}

void futurehead::vote::serialize (futurehead::stream & stream_a, futurehead::block_type type) const
{
	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, sequence);
	for (auto const & block : blocks)
	{
		if (block.which ())
		{
			debug_assert (type == futurehead::block_type::not_a_block);
			write (stream_a, boost::get<futurehead::block_hash> (block));
		}
		else
		{
			if (type == futurehead::block_type::not_a_block)
			{
				write (stream_a, boost::get<std::shared_ptr<futurehead::block>> (block)->hash ());
			}
			else
			{
				boost::get<std::shared_ptr<futurehead::block>> (block)->serialize (stream_a);
			}
		}
	}
}

void futurehead::vote::serialize (futurehead::stream & stream_a) const
{
	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, sequence);
	for (auto const & block : blocks)
	{
		if (block.which ())
		{
			write (stream_a, futurehead::block_type::not_a_block);
			write (stream_a, boost::get<futurehead::block_hash> (block));
		}
		else
		{
			futurehead::serialize_block (stream_a, *boost::get<std::shared_ptr<futurehead::block>> (block));
		}
	}
}

bool futurehead::vote::deserialize (futurehead::stream & stream_a, futurehead::block_uniquer * uniquer_a)
{
	auto error (false);
	try
	{
		futurehead::read (stream_a, account);
		futurehead::read (stream_a, signature);
		futurehead::read (stream_a, sequence);

		futurehead::block_type type;

		while (true)
		{
			if (futurehead::try_read (stream_a, type))
			{
				// Reached the end of the stream
				break;
			}

			if (type == futurehead::block_type::not_a_block)
			{
				futurehead::block_hash block_hash;
				futurehead::read (stream_a, block_hash);
				blocks.push_back (block_hash);
			}
			else
			{
				std::shared_ptr<futurehead::block> block (futurehead::deserialize_block (stream_a, type, uniquer_a));
				if (block == nullptr)
				{
					throw std::runtime_error ("Block is empty");
				}

				blocks.push_back (block);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	if (blocks.empty ())
	{
		error = true;
	}

	return error;
}

bool futurehead::vote::validate () const
{
	return futurehead::validate_message (account, hash (), signature);
}

futurehead::block_hash futurehead::iterate_vote_blocks_as_hash::operator() (boost::variant<std::shared_ptr<futurehead::block>, futurehead::block_hash> const & item) const
{
	futurehead::block_hash result;
	if (item.which ())
	{
		result = boost::get<futurehead::block_hash> (item);
	}
	else
	{
		result = boost::get<std::shared_ptr<futurehead::block>> (item)->hash ();
	}
	return result;
}

boost::transform_iterator<futurehead::iterate_vote_blocks_as_hash, futurehead::vote_blocks_vec_iter> futurehead::vote::begin () const
{
	return boost::transform_iterator<futurehead::iterate_vote_blocks_as_hash, futurehead::vote_blocks_vec_iter> (blocks.begin (), futurehead::iterate_vote_blocks_as_hash ());
}

boost::transform_iterator<futurehead::iterate_vote_blocks_as_hash, futurehead::vote_blocks_vec_iter> futurehead::vote::end () const
{
	return boost::transform_iterator<futurehead::iterate_vote_blocks_as_hash, futurehead::vote_blocks_vec_iter> (blocks.end (), futurehead::iterate_vote_blocks_as_hash ());
}

futurehead::vote_uniquer::vote_uniquer (futurehead::block_uniquer & uniquer_a) :
uniquer (uniquer_a)
{
}

std::shared_ptr<futurehead::vote> futurehead::vote_uniquer::unique (std::shared_ptr<futurehead::vote> vote_a)
{
	auto result (vote_a);
	if (result != nullptr && !result->blocks.empty ())
	{
		if (!result->blocks.front ().which ())
		{
			result->blocks.front () = uniquer.unique (boost::get<std::shared_ptr<futurehead::block>> (result->blocks.front ()));
		}
		futurehead::block_hash key (vote_a->full_hash ());
		futurehead::lock_guard<std::mutex> lock (mutex);
		auto & existing (votes[key]);
		if (auto block_l = existing.lock ())
		{
			result = block_l;
		}
		else
		{
			existing = vote_a;
		}

		release_assert (std::numeric_limits<CryptoPP::word32>::max () > votes.size ());
		for (auto i (0); i < cleanup_count && !votes.empty (); ++i)
		{
			auto random_offset = futurehead::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (votes.size () - 1));

			auto existing (std::next (votes.begin (), random_offset));
			if (existing == votes.end ())
			{
				existing = votes.begin ();
			}
			if (existing != votes.end ())
			{
				if (auto block_l = existing->second.lock ())
				{
					// Still live
				}
				else
				{
					votes.erase (existing);
				}
			}
		}
	}
	return result;
}

size_t futurehead::vote_uniquer::size ()
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	return votes.size ();
}

std::unique_ptr<futurehead::container_info_component> futurehead::collect_container_info (vote_uniquer & vote_uniquer, const std::string & name)
{
	auto count = vote_uniquer.size ();
	auto sizeof_element = sizeof (vote_uniquer::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "votes", count, sizeof_element }));
	return composite;
}

futurehead::genesis::genesis ()
{
	static futurehead::network_params network_params;
	open = parse_block_from_genesis_data (network_params.ledger.genesis_block);
	debug_assert (open != nullptr);
}

futurehead::block_hash futurehead::genesis::hash () const
{
	return open->hash ();
}

futurehead::wallet_id futurehead::random_wallet_id ()
{
	futurehead::wallet_id wallet_id;
	futurehead::uint256_union dummy_secret;
	random_pool::generate_block (dummy_secret.bytes.data (), dummy_secret.bytes.size ());
	ed25519_publickey (dummy_secret.bytes.data (), wallet_id.bytes.data ());
	return wallet_id;
}

futurehead::unchecked_key::unchecked_key (futurehead::block_hash const & previous_a, futurehead::block_hash const & hash_a) :
previous (previous_a),
hash (hash_a)
{
}

bool futurehead::unchecked_key::deserialize (futurehead::stream & stream_a)
{
	auto error (false);
	try
	{
		futurehead::read (stream_a, previous.bytes);
		futurehead::read (stream_a, hash.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool futurehead::unchecked_key::operator== (futurehead::unchecked_key const & other_a) const
{
	return previous == other_a.previous && hash == other_a.hash;
}

futurehead::block_hash const & futurehead::unchecked_key::key () const
{
	return previous;
}

void futurehead::generate_cache::enable_all ()
{
	reps = true;
	cemented_count = true;
	unchecked_count = true;
	account_count = true;
	epoch_2 = true;
}
