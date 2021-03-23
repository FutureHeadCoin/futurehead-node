#include <futurehead/crypto_lib/random_pool.hpp>
#include <futurehead/lib/utility.hpp>
#include <futurehead/futurehead_node/daemon.hpp>
#include <futurehead/node/cli.hpp>
#include <futurehead/node/daemonconfig.hpp>
#include <futurehead/node/ipc/ipc_server.hpp>
#include <futurehead/node/json_handler.hpp>
#include <futurehead/node/node.hpp>
#include <futurehead/node/testing.hpp>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include <numeric>
#include <sstream>

#include <argon2.h>

// Some builds (mac) fail due to "Boost.Stacktrace requires `_Unwind_Backtrace` function".
#ifndef _WIN32
#ifdef FUTUREHEAD_STACKTRACE_BACKTRACE
#define BOOST_STACKTRACE_USE_BACKTRACE
#endif
#ifndef _GNU_SOURCE
#define BEFORE_GNU_SOURCE 0
#define _GNU_SOURCE
#else
#define BEFORE_GNU_SOURCE 1
#endif
#endif
#include <boost/stacktrace.hpp>
#ifndef _WIN32
#if !BEFORE_GNU_SOURCE
#undef _GNU_SOURCE
#endif
#endif

namespace
{
class uint64_from_hex // For use with boost::lexical_cast to read hexadecimal strings
{
public:
	uint64_t value;
};
std::istream & operator>> (std::istream & in, uint64_from_hex & out_val);

class address_library_pair
{
public:
	uint64_t address;
	std::string library;

	address_library_pair (uint64_t address, std::string library);
	bool operator< (const address_library_pair & other) const;
	bool operator== (const address_library_pair & other) const;
};
}

int main (int argc, char * const * argv)
{
	futurehead::set_umask ();
	futurehead::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	boost::program_options::options_description description ("Command line options");
	// clang-format off
	description.add_options ()
		("help", "Print out options")
		("version", "Prints out version")
		("config", boost::program_options::value<std::vector<std::string>>()->multitoken(), "Pass node configuration values. This takes precedence over any values in the configuration file. This option can be repeated multiple times.")
		("daemon", "Start node daemon")
		("compare_rep_weights", "Display a summarized comparison between the hardcoded bootstrap weights and representative weights from the ledger. Full comparison is output to logs")
		("debug_block_count", "Display the number of block")
		("debug_bootstrap_generate", "Generate bootstrap sequence of blocks")
		("debug_dump_frontier_unchecked_dependents", "Dump frontiers which have matching unchecked keys")
		("debug_dump_online_weight", "Dump online_weights table")
		("debug_dump_representatives", "List representatives and weights")
		("debug_account_count", "Display the number of accounts")
		("debug_mass_activity", "(Deprecated) Generates fake debug activity. Can use slow_test's generate_mass_activity test for the same behavior.")
		("debug_profile_generate", "Profile work generation")
		("debug_profile_validate", "Profile work validation")
		("debug_opencl", "OpenCL work generation")
		("debug_profile_kdf", "Profile kdf function")
		("debug_output_last_backtrace_dump", "Displays the contents of the latest backtrace in the event of a futurehead_node crash")
		("debug_generate_crash_report", "Consolidates the futurehead_node_backtrace.dump file. Requires addr2line installed on Linux")
		("debug_sys_logging", "Test the system logger")
		("debug_verify_profile", "Profile signature verification")
		("debug_verify_profile_batch", "Profile batch signature verification")
		("debug_profile_bootstrap", "Profile bootstrap style blocks processing (at least 10GB of free storage space required)")
		("debug_profile_sign", "Profile signature generation")
		("debug_profile_process", "Profile active blocks processing (only for futurehead_test_network)")
		("debug_profile_votes", "Profile votes processing (only for futurehead_test_network)")
		("debug_profile_frontiers_confirmation", "Profile frontiers confirmation speed (only for futurehead_test_network)")
		("debug_random_feed", "Generates output to RNG test suites")
		("debug_rpc", "Read an RPC command from stdin and invoke it. Network operations will have no effect.")
		("debug_peers", "Display peer IPv6:port connections")
		("debug_cemented_block_count", "Displays the number of cemented (confirmed) blocks")
		("debug_stacktrace", "Display an example stacktrace")
		("debug_account_versions", "Display the total counts of each version for all accounts (including unpocketed)")
		("validate_blocks,debug_validate_blocks", "Check all blocks for correct hash, signature, work value")
		("platform", boost::program_options::value<std::string> (), "Defines the <platform> for OpenCL commands")
		("device", boost::program_options::value<std::string> (), "Defines <device> for OpenCL command")
		("threads", boost::program_options::value<std::string> (), "Defines <threads> count for various commands")
		("difficulty", boost::program_options::value<std::string> (), "Defines <difficulty> for OpenCL command, HEX")
		("multiplier", boost::program_options::value<std::string> (), "Defines <multiplier> for work generation. Overrides <difficulty>")
		("count", boost::program_options::value<std::string> (), "Defines <count> for various commands")
		("pow_sleep_interval", boost::program_options::value<std::string> (), "Defines the amount to sleep inbetween each pow calculation attempt")
		("address_column", boost::program_options::value<std::string> (), "Defines which column the addresses are located, 0 indexed (check --debug_output_last_backtrace_dump output)")
		("silent", "Silent command execution");
	// clang-format on
	futurehead::add_node_options (description);
	futurehead::add_node_flag_options (description);
	boost::program_options::variables_map vm;
	try
	{
		boost::program_options::store (boost::program_options::parse_command_line (argc, argv, description), vm);
	}
	catch (boost::program_options::error const & err)
	{
		std::cerr << err.what () << std::endl;
		return 1;
	}
	boost::program_options::notify (vm);
	int result (0);

	auto network (vm.find ("network"));
	if (network != vm.end ())
	{
		auto err (futurehead::network_constants::set_active_network (network->second.as<std::string> ()));
		if (err)
		{
			std::cerr << futurehead::network_constants::active_network_err_msg << std::endl;
			std::exit (1);
		}
	}

	auto data_path_it = vm.find ("data_path");
	if (data_path_it == vm.end ())
	{
		std::string error_string;
		if (!futurehead::migrate_working_path (error_string))
		{
			std::cerr << error_string << std::endl;

			return 1;
		}
	}

	boost::filesystem::path data_path ((data_path_it != vm.end ()) ? data_path_it->second.as<std::string> () : futurehead::working_path ());
	auto ec = futurehead::handle_node_options (vm);
	if (ec == futurehead::error_cli::unknown_command)
	{
		if (vm.count ("daemon") > 0)
		{
			futurehead_daemon::daemon daemon;
			futurehead::node_flags flags;
			auto flags_ec = futurehead::update_flags (flags, vm);
			if (flags_ec)
			{
				std::cerr << flags_ec.message () << std::endl;
				std::exit (1);
			}
			daemon.run (data_path, flags);
		}
		else if (vm.count ("compare_rep_weights"))
		{
			if (!futurehead::network_constants ().is_test_network ())
			{
				auto node_flags = futurehead::inactive_node_flag_defaults ();
				futurehead::update_flags (node_flags, vm);
				node_flags.generate_cache.reps = true;
				futurehead::inactive_node inactive_node (data_path, node_flags);
				auto node = inactive_node.node;

				auto const hardcoded = node->get_bootstrap_weights ().second;
				auto const ledger_unfiltered = node->ledger.cache.rep_weights.get_rep_amounts ();

				auto get_total = [](decltype (hardcoded) const & reps) -> futurehead::uint128_union {
					return std::accumulate (reps.begin (), reps.end (), futurehead::uint128_t{ 0 }, [](auto sum, auto const & rep) { return sum + rep.second; });
				};

				// Hardcoded weights are filtered to a cummulative weight of 99%, need to do the same for ledger weights
				std::remove_const_t<decltype (ledger_unfiltered)> ledger;
				{
					std::vector<std::pair<futurehead::account, futurehead::uint128_t>> sorted;
					sorted.reserve (ledger_unfiltered.size ());
					std::copy (ledger_unfiltered.begin (), ledger_unfiltered.end (), std::back_inserter (sorted));
					std::sort (sorted.begin (), sorted.end (), [](auto const & left, auto const & right) { return left.second > right.second; });
					auto const total_unfiltered = get_total (ledger_unfiltered);
					futurehead::uint128_t sum{ 0 };
					auto target = (total_unfiltered.number () / 100) * 99;
					for (auto i (sorted.begin ()), n (sorted.end ()); i != n && sum <= target; sum += i->second, ++i)
					{
						ledger.insert (*i);
					}
				}

				auto const total_ledger = get_total (ledger);
				auto const total_hardcoded = get_total (hardcoded);

				struct mismatched_t
				{
					futurehead::account rep;
					futurehead::uint128_union hardcoded;
					futurehead::uint128_union ledger;
					futurehead::uint128_union diff;
					std::string get_entry () const
					{
						return boost::str (boost::format ("representative %1% hardcoded %2% ledger %3% mismatch %4%")
						% rep.to_account () % hardcoded.format_balance (futurehead::Mxrb_ratio, 0, true) % ledger.format_balance (futurehead::Mxrb_ratio, 0, true) % diff.format_balance (futurehead::Mxrb_ratio, 0, true));
					}
				};

				std::vector<mismatched_t> mismatched;
				mismatched.reserve (hardcoded.size ());
				std::transform (hardcoded.begin (), hardcoded.end (), std::back_inserter (mismatched), [&ledger](auto const & rep) {
					auto ledger_rep (ledger.find (rep.first));
					futurehead::uint128_t ledger_weight = (ledger_rep == ledger.end () ? 0 : ledger_rep->second);
					auto absolute = ledger_weight > rep.second ? ledger_weight - rep.second : rep.second - ledger_weight;
					return mismatched_t{ rep.first, rep.second, ledger_weight, absolute };
				});

				// Sort by descending difference
				std::sort (mismatched.begin (), mismatched.end (), [](mismatched_t const & left, mismatched_t const & right) { return left.diff > right.diff; });

				futurehead::uint128_union const mismatch_total = std::accumulate (mismatched.begin (), mismatched.end (), futurehead::uint128_t{ 0 }, [](auto sum, mismatched_t const & sample) { return sum + sample.diff.number (); });
				futurehead::uint128_union const mismatch_mean = mismatch_total.number () / mismatched.size ();

				futurehead::uint512_union mismatch_variance = std::accumulate (mismatched.begin (), mismatched.end (), futurehead::uint512_t (0), [M = mismatch_mean.number (), N = mismatched.size ()](futurehead::uint512_t sum, mismatched_t const & sample) {
					auto x = sample.diff.number ();
					futurehead::uint512_t const mean_diff = x > M ? x - M : M - x;
					futurehead::uint512_t const sqr = mean_diff * mean_diff;
					return sum + sqr;
				})
				/ mismatched.size ();

				futurehead::uint128_union const mismatch_stddev = futurehead::narrow_cast<futurehead::uint128_t> (boost::multiprecision::sqrt (mismatch_variance.number ()));

				auto const outlier_threshold = std::max (futurehead::Gxrb_ratio, mismatch_mean.number () + 1 * mismatch_stddev.number ());
				decltype (mismatched) outliers;
				std::copy_if (mismatched.begin (), mismatched.end (), std::back_inserter (outliers), [outlier_threshold](mismatched_t const & sample) {
					return sample.diff > outlier_threshold;
				});

				auto const newcomer_threshold = std::max (futurehead::Gxrb_ratio, mismatch_mean.number ());
				std::vector<std::pair<futurehead::account, futurehead::uint128_t>> newcomers;
				std::copy_if (ledger.begin (), ledger.end (), std::back_inserter (newcomers), [&hardcoded](auto const & rep) {
					return !hardcoded.count (rep.first) && rep.second;
				});

				// Sort by descending weight
				std::sort (newcomers.begin (), newcomers.end (), [](auto const & left, auto const & right) { return left.second > right.second; });

				auto newcomer_entry = [](auto const & rep) {
					return boost::str (boost::format ("representative %1% hardcoded --- ledger %2%") % rep.first.to_account () % futurehead::uint128_union (rep.second).format_balance (futurehead::Mxrb_ratio, 0, true));
				};

				std::cout << boost::str (boost::format ("hardcoded weight %1% mfpsc\nledger weight %2% mfpsc\nmismatched\n\tsamples %3%\n\ttotal %4% mfpsc\n\tmean %5% mfpsc\n\tsigma %6% mfpsc\n")
				% total_hardcoded.format_balance (futurehead::Mxrb_ratio, 0, true)
				% total_ledger.format_balance (futurehead::Mxrb_ratio, 0, true)
				% mismatched.size ()
				% mismatch_total.format_balance (futurehead::Mxrb_ratio, 0, true)
				% mismatch_mean.format_balance (futurehead::Mxrb_ratio, 0, true)
				% mismatch_stddev.format_balance (futurehead::Mxrb_ratio, 0, true));

				if (!outliers.empty ())
				{
					std::cout << "outliers\n";
					for (auto const & outlier : outliers)
					{
						std::cout << '\t' << outlier.get_entry () << '\n';
					}
				}

				if (!newcomers.empty ())
				{
					std::cout << "newcomers\n";
					for (auto const & newcomer : newcomers)
					{
						if (newcomer.second > newcomer_threshold)
						{
							std::cout << '\t' << newcomer_entry (newcomer) << '\n';
						}
					}
				}

				// Log more data
				auto const log_threshold = futurehead::Gxrb_ratio;
				for (auto const & sample : mismatched)
				{
					if (sample.diff > log_threshold)
					{
						node->logger.always_log (sample.get_entry ());
					}
				}
				for (auto const & newcomer : newcomers)
				{
					if (newcomer.second > log_threshold)
					{
						node->logger.always_log (newcomer_entry (newcomer));
					}
				}
			}
			else
			{
				std::cout << "Not available for the test network" << std::endl;
				result = -1;
			}
		}
		else if (vm.count ("debug_block_count"))
		{
			auto inactive_node = futurehead::default_inactive_node (data_path, vm);
			auto node = inactive_node->node;
			auto transaction (node->store.tx_begin_read ());
			std::cout << boost::str (boost::format ("Block count: %1%\n") % node->store.block_count (transaction).sum ());
		}
		else if (vm.count ("debug_bootstrap_generate"))
		{
			auto key_it = vm.find ("key");
			if (key_it != vm.end ())
			{
				futurehead::uint256_union key;
				if (!key.decode_hex (key_it->second.as<std::string> ()))
				{
					futurehead::keypair genesis (key.to_string ());
					futurehead::work_pool work (std::numeric_limits<unsigned>::max ());
					std::cout << "Genesis: " << genesis.prv.data.to_string () << "\n"
					          << "Public: " << genesis.pub.to_string () << "\n"
					          << "Account: " << genesis.pub.to_account () << "\n";
					futurehead::keypair landing;
					std::cout << "Landing: " << landing.prv.data.to_string () << "\n"
					          << "Public: " << landing.pub.to_string () << "\n"
					          << "Account: " << landing.pub.to_account () << "\n";
					for (auto i (0); i != 32; ++i)
					{
						futurehead::keypair rep;
						std::cout << "Rep" << i << ": " << rep.prv.data.to_string () << "\n"
						          << "Public: " << rep.pub.to_string () << "\n"
						          << "Account: " << rep.pub.to_account () << "\n";
					}
					futurehead::network_constants network_constants;
					futurehead::uint128_t balance (std::numeric_limits<futurehead::uint128_t>::max ());
					futurehead::open_block genesis_block (reinterpret_cast<const futurehead::block_hash &> (genesis.pub), genesis.pub, genesis.pub, genesis.prv, genesis.pub, *work.generate (futurehead::work_version::work_1, genesis.pub, network_constants.publish_thresholds.epoch_1));
					std::cout << genesis_block.to_json ();
					std::cout.flush ();
					futurehead::block_hash previous (genesis_block.hash ());
					for (auto i (0); i != 8; ++i)
					{
						futurehead::uint128_t yearly_distribution (futurehead::uint128_t (1) << (127 - (i == 7 ? 6 : i)));
						auto weekly_distribution (yearly_distribution / 52);
						for (auto j (0); j != 52; ++j)
						{
							debug_assert (balance > weekly_distribution);
							balance = balance < (weekly_distribution * 2) ? 0 : balance - weekly_distribution;
							futurehead::send_block send (previous, landing.pub, balance, genesis.prv, genesis.pub, *work.generate (futurehead::work_version::work_1, previous, network_constants.publish_thresholds.epoch_1));
							previous = send.hash ();
							std::cout << send.to_json ();
							std::cout.flush ();
						}
					}
				}
				else
				{
					std::cerr << "Invalid key\n";
					result = -1;
				}
			}
			else
			{
				std::cerr << "Bootstrapping requires one <key> option\n";
				result = -1;
			}
		}
		else if (vm.count ("debug_dump_online_weight"))
		{
			auto inactive_node = futurehead::default_inactive_node (data_path, vm);
			auto node = inactive_node->node;
			auto current (node->online_reps.online_stake ());
			std::cout << boost::str (boost::format ("Online Weight %1%\n") % current);
			auto transaction (node->store.tx_begin_read ());
			for (auto i (node->store.online_weight_begin (transaction)), n (node->store.online_weight_end ()); i != n; ++i)
			{
				using time_point = std::chrono::system_clock::time_point;
				time_point ts (std::chrono::duration_cast<time_point::duration> (std::chrono::nanoseconds (i->first)));
				std::time_t timestamp = std::chrono::system_clock::to_time_t (ts);
				std::string weight;
				i->second.encode_dec (weight);
				std::cout << boost::str (boost::format ("Timestamp %1% Weight %2%\n") % ctime (&timestamp) % weight);
			}
		}
		else if (vm.count ("debug_dump_representatives"))
		{
			auto node_flags = futurehead::inactive_node_flag_defaults ();
			futurehead::update_flags (node_flags, vm);
			node_flags.generate_cache.reps = true;
			futurehead::inactive_node inactive_node (data_path, node_flags);
			auto node = inactive_node.node;
			auto transaction (node->store.tx_begin_read ());
			futurehead::uint128_t total;
			auto rep_amounts = node->ledger.cache.rep_weights.get_rep_amounts ();
			std::map<futurehead::account, futurehead::uint128_t> ordered_reps (rep_amounts.begin (), rep_amounts.end ());
			for (auto const & rep : ordered_reps)
			{
				total += rep.second;
				std::cout << boost::str (boost::format ("%1% %2% %3%\n") % rep.first.to_account () % rep.second.convert_to<std::string> () % total.convert_to<std::string> ());
			}
		}
		else if (vm.count ("debug_dump_frontier_unchecked_dependents"))
		{
			auto inactive_node = futurehead::default_inactive_node (data_path, vm);
			auto node = inactive_node->node;
			std::cout << "Outputting any frontier hashes which have associated key hashes in the unchecked table (may take some time)...\n";

			// Cache the account heads to make searching quicker against unchecked keys.
			auto transaction (node->store.tx_begin_read ());
			std::unordered_set<futurehead::block_hash> frontier_hashes;
			for (auto i (node->store.latest_begin (transaction)), n (node->store.latest_end ()); i != n; ++i)
			{
				frontier_hashes.insert (i->second.head);
			}

			// Check all unchecked keys for matching frontier hashes. Indicates an issue with process_batch algorithm
			for (auto i (node->store.unchecked_begin (transaction)), n (node->store.unchecked_end ()); i != n; ++i)
			{
				auto it = frontier_hashes.find (i->first.key ());
				if (it != frontier_hashes.cend ())
				{
					std::cout << it->to_string () << "\n";
				}
			}
		}
		else if (vm.count ("debug_account_count"))
		{
			auto node_flags = futurehead::inactive_node_flag_defaults ();
			futurehead::update_flags (node_flags, vm);
			node_flags.generate_cache.account_count = true;
			futurehead::inactive_node inactive_node (data_path, node_flags);
			std::cout << boost::str (boost::format ("Frontier count: %1%\n") % inactive_node.node->ledger.cache.account_count);
		}
		else if (vm.count ("debug_mass_activity"))
		{
			futurehead::system system (1);
			uint32_t count (1000000);
			system.generate_mass_activity (count, *system.nodes[0]);
		}
		else if (vm.count ("debug_profile_kdf"))
		{
			futurehead::network_params network_params;
			futurehead::uint256_union result;
			futurehead::uint256_union salt (0);
			std::string password ("");
			while (true)
			{
				auto begin1 (std::chrono::high_resolution_clock::now ());
				auto success (argon2_hash (1, network_params.kdf_work, 1, password.data (), password.size (), salt.bytes.data (), salt.bytes.size (), result.bytes.data (), result.bytes.size (), NULL, 0, Argon2_d, 0x10));
				(void)success;
				auto end1 (std::chrono::high_resolution_clock::now ());
				std::cerr << boost::str (boost::format ("Derivation time: %1%us\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
			}
		}
		else if (vm.count ("debug_profile_generate"))
		{
			futurehead::network_constants network_constants;
			uint64_t difficulty{ network_constants.publish_full.base };
			auto multiplier_it = vm.find ("multiplier");
			if (multiplier_it != vm.end ())
			{
				try
				{
					auto multiplier (boost::lexical_cast<double> (multiplier_it->second.as<std::string> ()));
					difficulty = futurehead::difficulty::from_multiplier (multiplier, difficulty);
				}
				catch (boost::bad_lexical_cast &)
				{
					std::cerr << "Invalid multiplier\n";
					return -1;
				}
			}
			else
			{
				auto difficulty_it = vm.find ("difficulty");
				if (difficulty_it != vm.end ())
				{
					if (futurehead::from_string_hex (difficulty_it->second.as<std::string> (), difficulty))
					{
						std::cerr << "Invalid difficulty\n";
						return -1;
					}
				}
			}

			auto pow_rate_limiter = std::chrono::nanoseconds (0);
			auto pow_sleep_interval_it = vm.find ("pow_sleep_interval");
			if (pow_sleep_interval_it != vm.cend ())
			{
				pow_rate_limiter = std::chrono::nanoseconds (boost::lexical_cast<uint64_t> (pow_sleep_interval_it->second.as<std::string> ()));
			}

			futurehead::work_pool work (std::numeric_limits<unsigned>::max (), pow_rate_limiter);
			futurehead::change_block block (0, 0, futurehead::keypair ().prv, 0, 0);
			if (!result)
			{
				std::cerr << boost::str (boost::format ("Starting generation profiling. Difficulty: %1$#x (%2%x from base difficulty %3$#x)\n") % difficulty % futurehead::to_string (futurehead::difficulty::to_multiplier (difficulty, network_constants.publish_full.base), 4) % network_constants.publish_full.base);
				while (!result)
				{
					block.hashables.previous.qwords[0] += 1;
					auto begin1 (std::chrono::high_resolution_clock::now ());
					block.block_work_set (*work.generate (futurehead::work_version::work_1, block.root (), difficulty));
					auto end1 (std::chrono::high_resolution_clock::now ());
					std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
				}
			}
		}
		else if (vm.count ("debug_profile_validate"))
		{
			uint64_t difficulty{ futurehead::network_constants ().publish_full.base };
			std::cerr << "Starting validation profile" << std::endl;
			auto start (std::chrono::steady_clock::now ());
			bool valid{ false };
			futurehead::block_hash hash{ 0 };
			uint64_t count{ 10000000U }; // 10M
			for (uint64_t i (0); i < count; ++i)
			{
				valid = futurehead::work_v1::value (hash, i) > difficulty;
			}
			std::ostringstream oss (valid ? "true" : "false"); // IO forces compiler to not dismiss the variable
			auto total_time (std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now () - start).count ());
			uint64_t average (total_time / count);
			std::cout << "Average validation time: " << std::to_string (average) << " ns (" << std::to_string (static_cast<unsigned> (count * 1e9 / total_time)) << " validations/s)" << std::endl;
			return average;
		}
		else if (vm.count ("debug_opencl"))
		{
			futurehead::network_constants network_constants;
			bool error (false);
			futurehead::opencl_environment environment (error);
			if (!error)
			{
				unsigned short platform (0);
				auto platform_it = vm.find ("platform");
				if (platform_it != vm.end ())
				{
					try
					{
						platform = boost::lexical_cast<unsigned short> (platform_it->second.as<std::string> ());
					}
					catch (boost::bad_lexical_cast &)
					{
						std::cerr << "Invalid platform id\n";
						return -1;
					}
				}
				unsigned short device (0);
				auto device_it = vm.find ("device");
				if (device_it != vm.end ())
				{
					try
					{
						device = boost::lexical_cast<unsigned short> (device_it->second.as<std::string> ());
					}
					catch (boost::bad_lexical_cast &)
					{
						std::cerr << "Invalid device id\n";
						return -1;
					}
				}
				unsigned threads (1024 * 1024);
				auto threads_it = vm.find ("threads");
				if (threads_it != vm.end ())
				{
					try
					{
						threads = boost::lexical_cast<unsigned> (threads_it->second.as<std::string> ());
					}
					catch (boost::bad_lexical_cast &)
					{
						std::cerr << "Invalid threads count\n";
						return -1;
					}
				}
				uint64_t difficulty (network_constants.publish_full.base);
				auto multiplier_it = vm.find ("multiplier");
				if (multiplier_it != vm.end ())
				{
					try
					{
						auto multiplier (boost::lexical_cast<double> (multiplier_it->second.as<std::string> ()));
						difficulty = futurehead::difficulty::from_multiplier (multiplier, difficulty);
					}
					catch (boost::bad_lexical_cast &)
					{
						std::cerr << "Invalid multiplier\n";
						return -1;
					}
				}
				else
				{
					auto difficulty_it = vm.find ("difficulty");
					if (difficulty_it != vm.end ())
					{
						if (futurehead::from_string_hex (difficulty_it->second.as<std::string> (), difficulty))
						{
							std::cerr << "Invalid difficulty\n";
							return -1;
						}
					}
				}
				if (!result)
				{
					error |= platform >= environment.platforms.size ();
					if (!error)
					{
						error |= device >= environment.platforms[platform].devices.size ();
						if (!error)
						{
							futurehead::logger_mt logger;
							futurehead::opencl_config config (platform, device, threads);
							auto opencl (futurehead::opencl_work::create (true, config, logger));
							futurehead::work_pool work_pool (std::numeric_limits<unsigned>::max (), std::chrono::nanoseconds (0), opencl ? [&opencl](futurehead::work_version const version_a, futurehead::root const & root_a, uint64_t difficulty_a, std::atomic<int> &) {
								return opencl->generate_work (version_a, root_a, difficulty_a);
							}
							                                                                                                       : std::function<boost::optional<uint64_t> (futurehead::work_version const, futurehead::root const &, uint64_t, std::atomic<int> &)> (nullptr));
							futurehead::change_block block (0, 0, futurehead::keypair ().prv, 0, 0);
							std::cerr << boost::str (boost::format ("Starting OpenCL generation profiling. Platform: %1%. Device: %2%. Threads: %3%. Difficulty: %4$#x (%5%x from base difficulty %6$#x)\n") % platform % device % threads % difficulty % futurehead::to_string (futurehead::difficulty::to_multiplier (difficulty, network_constants.publish_full.base), 4) % network_constants.publish_full.base);
							for (uint64_t i (0); true; ++i)
							{
								block.hashables.previous.qwords[0] += 1;
								auto begin1 (std::chrono::high_resolution_clock::now ());
								block.block_work_set (*work_pool.generate (futurehead::work_version::work_1, block.root (), difficulty));
								auto end1 (std::chrono::high_resolution_clock::now ());
								std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
							}
						}
						else
						{
							std::cout << "Not available device id\n"
							          << std::endl;
							result = -1;
						}
					}
					else
					{
						std::cout << "Not available platform id\n"
						          << std::endl;
						result = -1;
					}
				}
			}
			else
			{
				std::cout << "Error initializing OpenCL" << std::endl;
				result = -1;
			}
		}
		else if (vm.count ("debug_output_last_backtrace_dump"))
		{
			if (boost::filesystem::exists ("futurehead_node_backtrace.dump"))
			{
				// There is a backtrace, so output the contents
				std::ifstream ifs ("futurehead_node_backtrace.dump");

				boost::stacktrace::stacktrace st = boost::stacktrace::stacktrace::from_dump (ifs);
				std::cout << "Latest crash backtrace:\n"
				          << st << std::endl;
			}
		}
		else if (vm.count ("debug_generate_crash_report"))
		{
			if (boost::filesystem::exists ("futurehead_node_backtrace.dump"))
			{
				// There is a backtrace, so output the contents
				std::ifstream ifs ("futurehead_node_backtrace.dump");
				boost::stacktrace::stacktrace st = boost::stacktrace::stacktrace::from_dump (ifs);

				std::string crash_report_filename = "futurehead_node_crash_report.txt";

#if defined(_WIN32) || defined(__APPLE__)
				// Only linux has load addresses, so just write the dump to a readable file.
				// It's the best we can do to keep consistency.
				std::ofstream ofs (crash_report_filename);
				ofs << st;
#else
				// Read all the futurehead node files
				boost::system::error_code err;
				auto running_executable_filepath = boost::dll::program_location (err);
				if (!err)
				{
					auto num = 0;
					auto format = boost::format ("futurehead_node_crash_load_address_dump_%1%.txt");
					std::vector<address_library_pair> base_addresses;

					// The first one only has the load address
					uint64_from_hex base_address;
					std::string line;
					if (boost::filesystem::exists (boost::str (format % num)))
					{
						std::getline (std::ifstream (boost::str (format % num)), line);
						if (boost::conversion::try_lexical_convert (line, base_address))
						{
							base_addresses.emplace_back (base_address.value, running_executable_filepath.string ());
						}
					}
					++num;

					// Now do the rest of the files
					while (boost::filesystem::exists (boost::str (format % num)))
					{
						std::ifstream ifs_dump_filename (boost::str (format % num));

						// 2 lines, the path to the dynamic library followed by the load address
						std::string dynamic_lib_path;
						std::getline (ifs_dump_filename, dynamic_lib_path);
						std::getline (ifs_dump_filename, line);

						if (boost::conversion::try_lexical_convert (line, base_address))
						{
							base_addresses.emplace_back (base_address.value, dynamic_lib_path);
						}

						++num;
					}

					std::sort (base_addresses.begin (), base_addresses.end ());

					auto address_column_it = vm.find ("address_column");
					auto column = -1;
					if (address_column_it != vm.end ())
					{
						if (!boost::conversion::try_lexical_convert (address_column_it->second.as<std::string> (), column))
						{
							std::cerr << "Error: Invalid address column\n";
							result = -1;
						}
					}

					// Extract the addresses from the dump file.
					std::stringstream stacktrace_ss;
					stacktrace_ss << st;
					std::vector<uint64_t> backtrace_addresses;
					while (std::getline (stacktrace_ss, line))
					{
						std::istringstream iss (line);
						std::vector<std::string> results (std::istream_iterator<std::string>{ iss }, std::istream_iterator<std::string> ());

						if (column != -1)
						{
							if (column < results.size ())
							{
								uint64_from_hex address_hex;
								if (boost::conversion::try_lexical_convert (results[column], address_hex))
								{
									backtrace_addresses.push_back (address_hex.value);
								}
								else
								{
									std::cerr << "Error: Address column does not point to valid addresses\n";
									result = -1;
								}
							}
							else
							{
								std::cerr << "Error: Address column too high\n";
								result = -1;
							}
						}
						else
						{
							for (const auto & text : results)
							{
								uint64_from_hex address_hex;
								if (boost::conversion::try_lexical_convert (text, address_hex))
								{
									backtrace_addresses.push_back (address_hex.value);
									break;
								}
							}
						}
					}

					// Recreate the crash report with an empty file
					boost::filesystem::remove (crash_report_filename);
					{
						std::ofstream ofs (crash_report_filename);
						futurehead::set_secure_perm_file (crash_report_filename);
					}

					// Hold the results from all addr2line calls, if all fail we can assume that addr2line is not installed,
					// and inform the user that it needs installing
					std::vector<int> system_codes;

					auto run_addr2line = [&backtrace_addresses, &base_addresses, &system_codes, &crash_report_filename](bool use_relative_addresses) {
						for (auto backtrace_address : backtrace_addresses)
						{
							// Find the closest address to it
							for (auto base_address : boost::adaptors::reverse (base_addresses))
							{
								if (backtrace_address > base_address.address)
								{
									// Addresses need to be in hex for addr2line to work
									auto address = use_relative_addresses ? backtrace_address - base_address.address : backtrace_address;
									std::stringstream ss;
									ss << std::uppercase << std::hex << address;

									// Call addr2line to convert the address into something readable.
									auto res = std::system (boost::str (boost::format ("addr2line -fCi %1% -e %2% >> %3%") % ss.str () % base_address.library % crash_report_filename).c_str ());
									system_codes.push_back (res);
									break;
								}
							}
						}
					};

					// First run addr2line using absolute addresses
					run_addr2line (false);
					{
						std::ofstream ofs (crash_report_filename, std::ios_base::out | std::ios_base::app);
						ofs << std::endl
						    << "Using relative addresses:" << std::endl; // Add an empty line to separate the absolute & relative output
					}

					// Now run using relative addresses. This will give actual results for other dlls, the results from the futurehead_node executable.
					run_addr2line (true);

					if (std::find (system_codes.begin (), system_codes.end (), 0) == system_codes.end ())
					{
						std::cerr << "Error: Check that addr2line is installed and that futurehead_node_crash_load_address_dump_*.txt files exist." << std::endl;
						result = -1;
					}
					else
					{
						// Delete the crash dump files. The user won't care about them after this.
						num = 0;
						while (boost::filesystem::exists (boost::str (format % num)))
						{
							boost::filesystem::remove (boost::str (format % num));
							++num;
						}

						boost::filesystem::remove ("futurehead_node_backtrace.dump");
					}
				}
				else
				{
					std::cerr << "Error: Could not determine running executable path" << std::endl;
					result = -1;
				}
#endif
			}
			else
			{
				std::cerr << "Error: futurehead_node_backtrace.dump could not be found";
				result = -1;
			}
		}
		else if (vm.count ("debug_verify_profile"))
		{
			futurehead::keypair key;
			futurehead::uint256_union message;
			auto signature = futurehead::sign_message (key.prv, key.pub, message);
			auto begin (std::chrono::high_resolution_clock::now ());
			for (auto i (0u); i < 1000; ++i)
			{
				futurehead::validate_message (key.pub, message, signature);
			}
			auto end (std::chrono::high_resolution_clock::now ());
			std::cerr << "Signature verifications " << std::chrono::duration_cast<std::chrono::microseconds> (end - begin).count () << std::endl;
		}
		else if (vm.count ("debug_verify_profile_batch"))
		{
			futurehead::keypair key;
			size_t batch_count (1000);
			futurehead::uint256_union message;
			futurehead::uint512_union signature (futurehead::sign_message (key.prv, key.pub, message));
			std::vector<unsigned char const *> messages (batch_count, message.bytes.data ());
			std::vector<size_t> lengths (batch_count, sizeof (message));
			std::vector<unsigned char const *> pub_keys (batch_count, key.pub.bytes.data ());
			std::vector<unsigned char const *> signatures (batch_count, signature.bytes.data ());
			std::vector<int> verifications;
			verifications.resize (batch_count);
			auto begin (std::chrono::high_resolution_clock::now ());
			futurehead::validate_message_batch (messages.data (), lengths.data (), pub_keys.data (), signatures.data (), batch_count, verifications.data ());
			auto end (std::chrono::high_resolution_clock::now ());
			std::cerr << "Batch signature verifications " << std::chrono::duration_cast<std::chrono::microseconds> (end - begin).count () << std::endl;
		}
		else if (vm.count ("debug_profile_sign"))
		{
			std::cerr << "Starting blocks signing profiling\n";
			while (true)
			{
				futurehead::keypair key;
				futurehead::block_hash latest (0);
				auto begin1 (std::chrono::high_resolution_clock::now ());
				for (uint64_t balance (0); balance < 1000; ++balance)
				{
					futurehead::send_block send (latest, key.pub, balance, key.prv, key.pub, 0);
					latest = send.hash ();
				}
				auto end1 (std::chrono::high_resolution_clock::now ());
				std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
			}
		}
		else if (vm.count ("debug_profile_process"))
		{
			futurehead::network_constants::set_active_network (futurehead::futurehead_networks::futurehead_test_network);
			futurehead::network_params test_params;
			futurehead::block_builder builder;
			size_t num_accounts (100000);
			size_t num_iterations (5); // 100,000 * 5 * 2 = 1,000,000 blocks
			size_t max_blocks (2 * num_accounts * num_iterations + num_accounts * 2); //  1,000,000 + 2 * 100,000 = 1,200,000 blocks
			std::cout << boost::str (boost::format ("Starting pregenerating %1% blocks\n") % max_blocks);
			futurehead::system system;
			futurehead::work_pool work (std::numeric_limits<unsigned>::max ());
			futurehead::logging logging;
			auto path (futurehead::unique_path ());
			logging.init (path);
			futurehead::node_flags node_flags;
			futurehead::update_flags (node_flags, vm);
			auto node (std::make_shared<futurehead::node> (system.io_ctx, 24001, path, system.alarm, logging, work, node_flags));
			futurehead::block_hash genesis_latest (node->latest (test_params.ledger.test_genesis_key.pub));
			futurehead::uint128_t genesis_balance (std::numeric_limits<futurehead::uint128_t>::max ());
			// Generating keys
			std::vector<futurehead::keypair> keys (num_accounts);
			std::vector<futurehead::root> frontiers (num_accounts);
			std::vector<futurehead::uint128_t> balances (num_accounts, 1000000000);
			// Generating blocks
			std::deque<std::shared_ptr<futurehead::block>> blocks;
			for (auto i (0); i != num_accounts; ++i)
			{
				genesis_balance = genesis_balance - 1000000000;

				auto send = builder.state ()
				            .account (test_params.ledger.test_genesis_key.pub)
				            .previous (genesis_latest)
				            .representative (test_params.ledger.test_genesis_key.pub)
				            .balance (genesis_balance)
				            .link (keys[i].pub)
				            .sign (test_params.ledger.test_genesis_key.prv, test_params.ledger.test_genesis_key.pub)
				            .work (*work.generate (futurehead::work_version::work_1, genesis_latest, node->network_params.network.publish_thresholds.epoch_1))
				            .build ();

				genesis_latest = send->hash ();
				blocks.push_back (std::move (send));

				auto open = builder.state ()
				            .account (keys[i].pub)
				            .previous (0)
				            .representative (keys[i].pub)
				            .balance (balances[i])
				            .link (genesis_latest)
				            .sign (keys[i].prv, keys[i].pub)
				            .work (*work.generate (futurehead::work_version::work_1, keys[i].pub, node->network_params.network.publish_thresholds.epoch_1))
				            .build ();

				frontiers[i] = open->hash ();
				blocks.push_back (std::move (open));
			}
			for (auto i (0); i != num_iterations; ++i)
			{
				for (auto j (0); j != num_accounts; ++j)
				{
					size_t other (num_accounts - j - 1);
					// Sending to other account
					--balances[j];

					auto send = builder.state ()
					            .account (keys[j].pub)
					            .previous (frontiers[j])
					            .representative (keys[j].pub)
					            .balance (balances[j])
					            .link (keys[other].pub)
					            .sign (keys[j].prv, keys[j].pub)
					            .work (*work.generate (futurehead::work_version::work_1, frontiers[j], node->network_params.network.publish_thresholds.epoch_1))
					            .build ();

					frontiers[j] = send->hash ();
					blocks.push_back (std::move (send));
					// Receiving
					++balances[other];

					auto receive = builder.state ()
					               .account (keys[other].pub)
					               .previous (frontiers[other])
					               .representative (keys[other].pub)
					               .balance (balances[other])
					               .link (static_cast<futurehead::block_hash const &> (frontiers[j]))
					               .sign (keys[other].prv, keys[other].pub)
					               .work (*work.generate (futurehead::work_version::work_1, frontiers[other], node->network_params.network.publish_thresholds.epoch_1))
					               .build ();

					frontiers[other] = receive->hash ();
					blocks.push_back (std::move (receive));
				}
			}
			// Processing blocks
			std::cout << boost::str (boost::format ("Starting processing %1% blocks\n") % max_blocks);
			auto begin (std::chrono::high_resolution_clock::now ());
			while (!blocks.empty ())
			{
				auto block (blocks.front ());
				node->process_active (block);
				blocks.pop_front ();
			}
			futurehead::timer<std::chrono::seconds> timer_l (futurehead::timer_state::started);
			while (node->ledger.cache.block_count != max_blocks + 1)
			{
				std::this_thread::sleep_for (std::chrono::milliseconds (10));
				// Message each 15 seconds
				if (timer_l.after_deadline (std::chrono::seconds (15)))
				{
					timer_l.restart ();
					std::cout << boost::str (boost::format ("%1% (%2%) blocks processed (unchecked), %3% remaining") % node->ledger.cache.block_count % node->ledger.cache.unchecked_count % node->block_processor.size ()) << std::endl;
				}
			}
			// Waiting for final transaction commit
			uint64_t block_count (0);
			while (block_count < max_blocks + 1)
			{
				std::this_thread::sleep_for (std::chrono::milliseconds (10));
				auto transaction (node->store.tx_begin_read ());
				block_count = node->store.block_count (transaction).sum ();
			}
			auto end (std::chrono::high_resolution_clock::now ());
			auto time (std::chrono::duration_cast<std::chrono::microseconds> (end - begin).count ());
			node->stop ();
			std::cout << boost::str (boost::format ("%|1$ 12d| us \n%2% blocks per second\n") % time % (max_blocks * 1000000 / time));
		}
		else if (vm.count ("debug_profile_votes"))
		{
			futurehead::network_constants::set_active_network (futurehead::futurehead_networks::futurehead_test_network);
			futurehead::network_params test_params;
			futurehead::block_builder builder;
			size_t num_elections (40000);
			size_t num_representatives (25);
			size_t max_votes (num_elections * num_representatives); // 40,000 * 25 = 1,000,000 votes
			std::cerr << boost::str (boost::format ("Starting pregenerating %1% votes\n") % max_votes);
			futurehead::system system (1);
			futurehead::work_pool work (std::numeric_limits<unsigned>::max ());
			futurehead::logging logging;
			auto path (futurehead::unique_path ());
			logging.init (path);
			auto node (std::make_shared<futurehead::node> (system.io_ctx, 24001, path, system.alarm, logging, work));
			futurehead::block_hash genesis_latest (node->latest (test_params.ledger.test_genesis_key.pub));
			futurehead::uint128_t genesis_balance (std::numeric_limits<futurehead::uint128_t>::max ());
			// Generating keys
			std::vector<futurehead::keypair> keys (num_representatives);
			futurehead::uint128_t balance ((node->config.online_weight_minimum.number () / num_representatives) + 1);
			for (auto i (0); i != num_representatives; ++i)
			{
				auto transaction (node->store.tx_begin_write ());
				genesis_balance = genesis_balance - balance;

				auto send = builder.state ()
				            .account (test_params.ledger.test_genesis_key.pub)
				            .previous (genesis_latest)
				            .representative (test_params.ledger.test_genesis_key.pub)
				            .balance (genesis_balance)
				            .link (keys[i].pub)
				            .sign (test_params.ledger.test_genesis_key.prv, test_params.ledger.test_genesis_key.pub)
				            .work (*work.generate (futurehead::work_version::work_1, genesis_latest, node->network_params.network.publish_thresholds.epoch_1))
				            .build ();

				genesis_latest = send->hash ();
				node->ledger.process (transaction, *send);

				auto open = builder.state ()
				            .account (keys[i].pub)
				            .previous (0)
				            .representative (keys[i].pub)
				            .balance (balance)
				            .link (genesis_latest)
				            .sign (keys[i].prv, keys[i].pub)
				            .work (*work.generate (futurehead::work_version::work_1, keys[i].pub, node->network_params.network.publish_thresholds.epoch_1))
				            .build ();

				node->ledger.process (transaction, *open);
			}
			// Generating blocks
			std::deque<std::shared_ptr<futurehead::block>> blocks;
			for (auto i (0); i != num_elections; ++i)
			{
				genesis_balance = genesis_balance - 1;
				futurehead::keypair destination;

				auto send = builder.state ()
				            .account (test_params.ledger.test_genesis_key.pub)
				            .previous (genesis_latest)
				            .representative (test_params.ledger.test_genesis_key.pub)
				            .balance (genesis_balance)
				            .link (destination.pub)
				            .sign (test_params.ledger.test_genesis_key.prv, test_params.ledger.test_genesis_key.pub)
				            .work (*work.generate (futurehead::work_version::work_1, genesis_latest, node->network_params.network.publish_thresholds.epoch_1))
				            .build ();

				genesis_latest = send->hash ();
				blocks.push_back (std::move (send));
			}
			// Generating votes
			std::deque<std::shared_ptr<futurehead::vote>> votes;
			for (auto j (0); j != num_representatives; ++j)
			{
				uint64_t sequence (1);
				for (auto & i : blocks)
				{
					auto vote (std::make_shared<futurehead::vote> (keys[j].pub, keys[j].prv, sequence, std::vector<futurehead::block_hash> (1, i->hash ())));
					votes.push_back (vote);
					sequence++;
				}
			}
			// Processing block & start elections
			while (!blocks.empty ())
			{
				auto block (blocks.front ());
				node->process_active (block);
				blocks.pop_front ();
			}
			node->block_processor.flush ();
			// Processing votes
			std::cerr << boost::str (boost::format ("Starting processing %1% votes\n") % max_votes);
			auto begin (std::chrono::high_resolution_clock::now ());
			while (!votes.empty ())
			{
				auto vote (votes.front ());
				auto channel (std::make_shared<futurehead::transport::channel_udp> (node->network.udp_channels, node->network.endpoint (), node->network_params.protocol.protocol_version));
				node->vote_processor.vote (vote, channel);
				votes.pop_front ();
			}
			while (!node->active.empty ())
			{
				std::this_thread::sleep_for (std::chrono::milliseconds (100));
			}
			auto end (std::chrono::high_resolution_clock::now ());
			auto time (std::chrono::duration_cast<std::chrono::microseconds> (end - begin).count ());
			node->stop ();
			std::cerr << boost::str (boost::format ("%|1$ 12d| us \n%2% votes per second\n") % time % (max_votes * 1000000 / time));
		}
		else if (vm.count ("debug_profile_frontiers_confirmation"))
		{
			futurehead::force_futurehead_test_network ();
			futurehead::network_params test_params;
			futurehead::block_builder builder;
			size_t count (32 * 1024);
			auto count_it = vm.find ("count");
			if (count_it != vm.end ())
			{
				try
				{
					count = boost::lexical_cast<size_t> (count_it->second.as<std::string> ());
				}
				catch (boost::bad_lexical_cast &)
				{
					std::cerr << "Invalid count\n";
					return -1;
				}
			}
			std::cout << boost::str (boost::format ("Starting generating %1% blocks...\n") % (count * 2));
			boost::asio::io_context io_ctx1;
			boost::asio::io_context io_ctx2;
			futurehead::alarm alarm1 (io_ctx1);
			futurehead::alarm alarm2 (io_ctx2);
			futurehead::work_pool work (std::numeric_limits<unsigned>::max ());
			futurehead::logging logging;
			auto path1 (futurehead::unique_path ());
			auto path2 (futurehead::unique_path ());
			logging.init (path1);
			futurehead::node_config config1 (24000, logging);
			futurehead::node_flags flags;
			flags.disable_lazy_bootstrap = true;
			flags.disable_legacy_bootstrap = true;
			flags.disable_wallet_bootstrap = true;
			flags.disable_bootstrap_listener = true;
			auto node1 (std::make_shared<futurehead::node> (io_ctx1, path1, alarm1, config1, work, flags, 0));
			futurehead::block_hash genesis_latest (node1->latest (test_params.ledger.test_genesis_key.pub));
			futurehead::uint128_t genesis_balance (std::numeric_limits<futurehead::uint128_t>::max ());
			// Generating blocks
			std::deque<std::shared_ptr<futurehead::block>> blocks;
			for (auto i (0); i != count; ++i)
			{
				futurehead::keypair key;
				genesis_balance = genesis_balance - 1;

				auto send = builder.state ()
				            .account (test_params.ledger.test_genesis_key.pub)
				            .previous (genesis_latest)
				            .representative (test_params.ledger.test_genesis_key.pub)
				            .balance (genesis_balance)
				            .link (key.pub)
				            .sign (test_params.ledger.test_genesis_key.prv, test_params.ledger.test_genesis_key.pub)
				            .work (*work.generate (futurehead::work_version::work_1, genesis_latest, test_params.network.publish_thresholds.epoch_1))
				            .build ();

				genesis_latest = send->hash ();

				auto open = builder.state ()
				            .account (key.pub)
				            .previous (0)
				            .representative (key.pub)
				            .balance (1)
				            .link (genesis_latest)
				            .sign (key.prv, key.pub)
				            .work (*work.generate (futurehead::work_version::work_1, key.pub, test_params.network.publish_thresholds.epoch_1))
				            .build ();

				blocks.push_back (std::move (send));
				blocks.push_back (std::move (open));
				if (i % 20000 == 0 && i != 0)
				{
					std::cout << boost::str (boost::format ("%1% blocks generated\n") % (i * 2));
				}
			}
			node1->start ();
			futurehead::thread_runner runner1 (io_ctx1, node1->config.io_threads);

			std::cout << boost::str (boost::format ("Processing %1% blocks\n") % (count * 2));
			for (auto & block : blocks)
			{
				node1->block_processor.add (block);
			}
			node1->block_processor.flush ();
			auto iteration (0);
			while (node1->ledger.cache.block_count != count * 2 + 1)
			{
				std::this_thread::sleep_for (std::chrono::milliseconds (500));
				if (++iteration % 60 == 0)
				{
					std::cout << boost::str (boost::format ("%1% blocks processed\n") % node1->ledger.cache.block_count);
				}
			}
			// Confirm blocks for node1
			for (auto & block : blocks)
			{
				node1->confirmation_height_processor.add (block->hash ());
			}
			while (node1->ledger.cache.cemented_count != node1->ledger.cache.block_count)
			{
				std::this_thread::sleep_for (std::chrono::milliseconds (500));
				if (++iteration % 60 == 0)
				{
					std::cout << boost::str (boost::format ("%1% blocks cemented\n") % node1->ledger.cache.cemented_count);
				}
			}

			// Start new node
			futurehead::node_config config2 (24001, logging);
			// Config override
			std::vector<std::string> config_overrides;
			auto config (vm.find ("config"));
			if (config != vm.end ())
			{
				config_overrides = config->second.as<std::vector<std::string>> ();
			}
			if (!config_overrides.empty ())
			{
				auto path (futurehead::unique_path ());
				futurehead::daemon_config daemon_config (path);
				auto error = futurehead::read_node_config_toml (path, daemon_config, config_overrides);
				if (error)
				{
					std::cerr << "\n"
					          << error.get_message () << std::endl;
					std::exit (1);
				}
				else
				{
					config2.frontiers_confirmation = daemon_config.node.frontiers_confirmation;
					config2.active_elections_size = daemon_config.node.active_elections_size;
				}
			}
			auto node2 (std::make_shared<futurehead::node> (io_ctx2, path2, alarm2, config2, work, flags, 1));
			node2->start ();
			futurehead::thread_runner runner2 (io_ctx2, node2->config.io_threads);
			std::cout << boost::str (boost::format ("Processing %1% blocks (test node)\n") % (count * 2));
			// Processing block
			while (!blocks.empty ())
			{
				auto block (blocks.front ());
				node2->block_processor.add (block);
				blocks.pop_front ();
			}
			node2->block_processor.flush ();
			while (node2->ledger.cache.block_count != count * 2 + 1)
			{
				std::this_thread::sleep_for (std::chrono::milliseconds (500));
				if (++iteration % 60 == 0)
				{
					std::cout << boost::str (boost::format ("%1% blocks processed\n") % node2->ledger.cache.block_count);
				}
			}
			// Insert representative
			std::cout << "Initializing representative\n";
			auto wallet (node1->wallets.create (futurehead::random_wallet_id ()));
			wallet->insert_adhoc (test_params.ledger.test_genesis_key.prv);
			node2->network.merge_peer (node1->network.endpoint ());
			while (node2->rep_crawler.representative_count () == 0)
			{
				std::this_thread::sleep_for (std::chrono::milliseconds (10));
				if (++iteration % 500 == 0)
				{
					std::cout << "Representative initialization iteration...\n";
				}
			}
			auto begin (std::chrono::high_resolution_clock::now ());
			std::cout << boost::str (boost::format ("Starting confirming %1% frontiers (test node)\n") % (count + 1));
			// Wait for full frontiers confirmation
			while (node2->ledger.cache.cemented_count != node2->ledger.cache.block_count)
			{
				std::this_thread::sleep_for (std::chrono::milliseconds (25));
				if (++iteration % 1200 == 0)
				{
					std::cout << boost::str (boost::format ("%1% blocks confirmed\n") % node2->ledger.cache.cemented_count);
				}
			}
			auto end (std::chrono::high_resolution_clock::now ());
			auto time (std::chrono::duration_cast<std::chrono::microseconds> (end - begin).count ());
			std::cout << boost::str (boost::format ("%|1$ 12d| us \n%2% frontiers per second\n") % time % ((count + 1) * 1000000 / time));
			io_ctx1.stop ();
			io_ctx2.stop ();
			runner1.join ();
			runner2.join ();
			node1->stop ();
			node2->stop ();
		}
		else if (vm.count ("debug_random_feed"))
		{
			/*
			 * This command redirects an infinite stream of bytes from the random pool to standard out.
			 * The result can be fed into various tools for testing RNGs and entropy pools.
			 *
			 * Example, running the entire dieharder test suite:
			 *
			 *   ./futurehead_node --debug_random_feed | dieharder -a -g 200
			 */
			futurehead::raw_key seed;
			for (;;)
			{
				futurehead::random_pool::generate_block (seed.data.bytes.data (), seed.data.bytes.size ());
				std::cout.write (reinterpret_cast<const char *> (seed.data.bytes.data ()), seed.data.bytes.size ());
			}
		}
		else if (vm.count ("debug_rpc"))
		{
			std::string rpc_input_l;
			std::ostringstream command_l;
			while (std::cin >> rpc_input_l)
			{
				command_l << rpc_input_l;
			}

			auto response_handler_l ([](std::string const & response_a) {
				std::cout << response_a;
				// Terminate as soon as we have the result, even if background threads (like work generation) are running.
				std::exit (0);
			});

			auto node_flags = futurehead::inactive_node_flag_defaults ();
			futurehead::update_flags (node_flags, vm);
			node_flags.generate_cache.enable_all ();
			futurehead::inactive_node inactive_node_l (data_path, node_flags);

			futurehead::node_rpc_config config;
			futurehead::ipc::ipc_server server (*inactive_node_l.node, config);
			auto handler_l (std::make_shared<futurehead::json_handler> (*inactive_node_l.node, config, command_l.str (), response_handler_l));
			handler_l->process_request ();
		}
		else if (vm.count ("validate_blocks") || vm.count ("debug_validate_blocks"))
		{
			futurehead::timer<std::chrono::seconds> timer;
			timer.start ();
			auto inactive_node = futurehead::default_inactive_node (data_path, vm);
			auto node = inactive_node->node;
			bool const silent (vm.count ("silent"));
			unsigned threads_count (1);
			auto threads_it = vm.find ("threads");
			if (threads_it != vm.end ())
			{
				if (!boost::conversion::try_lexical_convert (threads_it->second.as<std::string> (), threads_count))
				{
					std::cerr << "Invalid threads count\n";
					return -1;
				}
			}
			threads_count = std::max (1u, threads_count);
			std::vector<std::thread> threads;
			std::mutex mutex;
			futurehead::condition_variable condition;
			std::atomic<bool> finished (false);
			std::deque<std::pair<futurehead::account, futurehead::account_info>> accounts;
			std::atomic<size_t> count (0);
			std::atomic<uint64_t> block_count (0);
			std::atomic<uint64_t> errors (0);

			auto print_error_message = [&silent, &errors](std::string const & error_message_a) {
				if (!silent)
				{
					static std::mutex cerr_mutex;
					futurehead::lock_guard<std::mutex> lock (cerr_mutex);
					std::cerr << error_message_a;
				}
				++errors;
			};

			auto start_threads = [node, &threads_count, &threads, &mutex, &condition, &finished](const auto & function_a, auto & deque_a) {
				for (auto i (0); i < threads_count; ++i)
				{
					threads.emplace_back ([&function_a, node, &mutex, &condition, &finished, &deque_a]() {
						auto transaction (node->store.tx_begin_read ());
						futurehead::unique_lock<std::mutex> lock (mutex);
						while (!deque_a.empty () || !finished)
						{
							while (deque_a.empty () && !finished)
							{
								condition.wait (lock);
							}
							if (!deque_a.empty ())
							{
								auto pair (deque_a.front ());
								deque_a.pop_front ();
								lock.unlock ();
								function_a (node, transaction, pair.first, pair.second);
								lock.lock ();
							}
						}
					});
				}
			};

			auto check_account = [&print_error_message, &silent, &count, &block_count](std::shared_ptr<futurehead::node> const & node, futurehead::read_transaction const & transaction, futurehead::account const & account, futurehead::account_info const & info) {
				++count;
				if (!silent && (count % 20000) == 0)
				{
					std::cout << boost::str (boost::format ("%1% accounts validated\n") % count);
				}
				futurehead::confirmation_height_info confirmation_height_info;
				node->store.confirmation_height_get (transaction, account, confirmation_height_info);

				if (confirmation_height_info.height > info.block_count)
				{
					print_error_message (boost::str (boost::format ("Confirmation height %1% greater than block count %2% for account: %3%\n") % confirmation_height_info.height % info.block_count % account.to_account ()));
				}

				auto hash (info.open_block);
				futurehead::block_hash calculated_hash (0);
				auto block (node->store.block_get (transaction, hash)); // Block data
				uint64_t height (0);
				uint64_t previous_timestamp (0);
				futurehead::account calculated_representative (0);
				while (!hash.is_zero () && block != nullptr)
				{
					++block_count;
					auto const & sideband (block->sideband ());
					// Check for state & open blocks if account field is correct
					if (block->type () == futurehead::block_type::open || block->type () == futurehead::block_type::state)
					{
						if (block->account () != account)
						{
							print_error_message (boost::str (boost::format ("Incorrect account field for block %1%\n") % hash.to_string ()));
						}
					}
					// Check if sideband account is correct
					else if (sideband.account != account)
					{
						print_error_message (boost::str (boost::format ("Incorrect sideband account for block %1%\n") % hash.to_string ()));
					}
					// Check if previous field is correct
					if (calculated_hash != block->previous ())
					{
						print_error_message (boost::str (boost::format ("Incorrect previous field for block %1%\n") % hash.to_string ()));
					}
					// Check if previous & type for open blocks are correct
					if (height == 0 && !block->previous ().is_zero ())
					{
						print_error_message (boost::str (boost::format ("Incorrect previous for open block %1%\n") % hash.to_string ()));
					}
					if (height == 0 && block->type () != futurehead::block_type::open && block->type () != futurehead::block_type::state)
					{
						print_error_message (boost::str (boost::format ("Incorrect type for open block %1%\n") % hash.to_string ()));
					}
					// Check if block data is correct (calculating hash)
					calculated_hash = block->hash ();
					if (calculated_hash != hash)
					{
						print_error_message (boost::str (boost::format ("Invalid data inside block %1% calculated hash: %2%\n") % hash.to_string () % calculated_hash.to_string ()));
					}
					// Check if block signature is correct
					if (validate_message (account, hash, block->block_signature ()))
					{
						bool invalid (true);
						// Epoch blocks
						if (block->type () == futurehead::block_type::state)
						{
							auto & state_block (static_cast<futurehead::state_block &> (*block.get ()));
							futurehead::amount prev_balance (0);
							if (!state_block.hashables.previous.is_zero ())
							{
								prev_balance = node->ledger.balance (transaction, state_block.hashables.previous);
							}
							if (node->ledger.is_epoch_link (state_block.hashables.link) && state_block.hashables.balance == prev_balance)
							{
								invalid = validate_message (node->ledger.epoch_signer (block->link ()), hash, block->block_signature ());
							}
						}
						if (invalid)
						{
							print_error_message (boost::str (boost::format ("Invalid signature for block %1%\n") % hash.to_string ()));
						}
					}
					// Validate block details set in the sideband
					bool block_details_error = false;
					if (block->type () != futurehead::block_type::state)
					{
						// Not state
						block_details_error = sideband.details.is_send || sideband.details.is_receive || sideband.details.is_epoch;
					}
					else
					{
						auto prev_balance (node->ledger.balance (transaction, block->previous ()));
						if (block->balance () < prev_balance)
						{
							// State send
							block_details_error = !sideband.details.is_send || sideband.details.is_receive || sideband.details.is_epoch;
						}
						else
						{
							if (block->link ().is_zero ())
							{
								// State change
								block_details_error = sideband.details.is_send || sideband.details.is_receive || sideband.details.is_epoch;
							}
							else if (block->balance () == prev_balance && node->ledger.is_epoch_link (block->link ()))
							{
								// State epoch
								block_details_error = !sideband.details.is_epoch || sideband.details.is_send || sideband.details.is_receive;
							}
							else
							{
								// State receive
								block_details_error = !sideband.details.is_receive || sideband.details.is_send || sideband.details.is_epoch;
								block_details_error |= !node->store.source_exists (transaction, block->link ());
							}
						}
					}
					if (block_details_error)
					{
						print_error_message (boost::str (boost::format ("Incorrect sideband block details for block %1%\n") % hash.to_string ()));
					}
					// Check if block work value is correct
					if (block->difficulty () < futurehead::work_threshold (block->work_version (), block->sideband ().details))
					{
						print_error_message (boost::str (boost::format ("Invalid work for block %1% value: %2%\n") % hash.to_string () % futurehead::to_string_hex (block->block_work ())));
					}
					// Check if sideband height is correct
					++height;
					if (sideband.height != height)
					{
						print_error_message (boost::str (boost::format ("Incorrect sideband height for block %1%. Sideband: %2%. Expected: %3%\n") % hash.to_string () % sideband.height % height));
					}
					// Check if sideband timestamp is after previous timestamp
					if (sideband.timestamp < previous_timestamp)
					{
						print_error_message (boost::str (boost::format ("Incorrect sideband timestamp for block %1%\n") % hash.to_string ()));
					}
					previous_timestamp = sideband.timestamp;
					// Calculate representative block
					if (block->type () == futurehead::block_type::open || block->type () == futurehead::block_type::change || block->type () == futurehead::block_type::state)
					{
						calculated_representative = block->representative ();
					}
					// Retrieving successor block hash
					hash = node->store.block_successor (transaction, hash);
					// Retrieving block data
					if (!hash.is_zero ())
					{
						block = node->store.block_get (transaction, hash);
					}
				}
				// Check if required block exists
				if (!hash.is_zero () && block == nullptr)
				{
					print_error_message (boost::str (boost::format ("Required block in account %1% chain was not found in ledger: %2%\n") % account.to_account () % hash.to_string ()));
				}
				// Check account block count
				if (info.block_count != height)
				{
					print_error_message (boost::str (boost::format ("Incorrect block count for account %1%. Actual: %2%. Expected: %3%\n") % account.to_account () % height % info.block_count));
				}
				// Check account head block (frontier)
				if (info.head != calculated_hash)
				{
					print_error_message (boost::str (boost::format ("Incorrect frontier for account %1%. Actual: %2%. Expected: %3%\n") % account.to_account () % calculated_hash.to_string () % info.head.to_string ()));
				}
				// Check account representative block
				if (info.representative != calculated_representative)
				{
					print_error_message (boost::str (boost::format ("Incorrect representative for account %1%. Actual: %2%. Expected: %3%\n") % account.to_account () % calculated_representative.to_string () % info.representative.to_string ()));
				}
			};

			start_threads (check_account, accounts);

			if (!silent)
			{
				std::cout << boost::str (boost::format ("Performing %1% threads blocks hash, signature, work validation...\n") % threads_count);
			}
			size_t const accounts_deque_overflow (32 * 1024);
			auto transaction (node->store.tx_begin_read ());
			for (auto i (node->store.latest_begin (transaction)), n (node->store.latest_end ()); i != n; ++i)
			{
				{
					futurehead::unique_lock<std::mutex> lock (mutex);
					if (accounts.size () > accounts_deque_overflow)
					{
						auto wait_ms (250 * accounts.size () / accounts_deque_overflow);
						const auto wakeup (std::chrono::steady_clock::now () + std::chrono::milliseconds (wait_ms));
						condition.wait_until (lock, wakeup);
					}
					accounts.emplace_back (i->first, i->second);
				}
				condition.notify_all ();
			}
			{
				futurehead::lock_guard<std::mutex> lock (mutex);
				finished = true;
			}
			condition.notify_all ();
			for (auto & thread : threads)
			{
				thread.join ();
			}
			threads.clear ();
			if (!silent)
			{
				std::cout << boost::str (boost::format ("%1% accounts validated\n") % count);
			}

			// Validate total block count
			auto ledger_block_count (node->store.block_count (transaction).sum ());
			if (block_count != ledger_block_count)
			{
				print_error_message (boost::str (boost::format ("Incorrect total block count. Blocks validated %1%. Block count in database: %2%\n") % block_count % ledger_block_count));
			}

			// Validate pending blocks
			count = 0;
			finished = false;
			std::deque<std::pair<futurehead::pending_key, futurehead::pending_info>> pending;

			auto check_pending = [&print_error_message, &silent, &count](std::shared_ptr<futurehead::node> const & node, futurehead::read_transaction const & transaction, futurehead::pending_key const & key, futurehead::pending_info const & info) {
				++count;
				if (!silent && (count % 500000) == 0)
				{
					std::cout << boost::str (boost::format ("%1% pending blocks validated\n") % count);
				}
				// Check block existance
				auto block (node->store.block_get_no_sideband (transaction, key.hash));
				if (block == nullptr)
				{
					print_error_message (boost::str (boost::format ("Pending block does not exist %1%\n") % key.hash.to_string ()));
				}
				else
				{
					// Check if pending destination is correct
					futurehead::account destination (0);
					if (auto state = dynamic_cast<futurehead::state_block *> (block.get ()))
					{
						if (node->ledger.is_send (transaction, *state))
						{
							destination = state->hashables.link;
						}
					}
					else if (auto send = dynamic_cast<futurehead::send_block *> (block.get ()))
					{
						destination = send->hashables.destination;
					}
					else
					{
						print_error_message (boost::str (boost::format ("Incorrect type for pending block %1%\n") % key.hash.to_string ()));
					}
					if (key.account != destination)
					{
						print_error_message (boost::str (boost::format ("Incorrect destination for pending block %1%\n") % key.hash.to_string ()));
					}
					// Check if pending source is correct
					auto account (node->ledger.account (transaction, key.hash));
					if (info.source != account)
					{
						print_error_message (boost::str (boost::format ("Incorrect source for pending block %1%\n") % key.hash.to_string ()));
					}
					// Check if pending amount is correct
					auto amount (node->ledger.amount (transaction, key.hash));
					if (info.amount != amount)
					{
						print_error_message (boost::str (boost::format ("Incorrect amount for pending block %1%\n") % key.hash.to_string ()));
					}
				}
			};

			start_threads (check_pending, pending);

			size_t const pending_deque_overflow (64 * 1024);
			for (auto i (node->store.pending_begin (transaction)), n (node->store.pending_end ()); i != n; ++i)
			{
				{
					futurehead::unique_lock<std::mutex> lock (mutex);
					if (pending.size () > pending_deque_overflow)
					{
						auto wait_ms (50 * pending.size () / pending_deque_overflow);
						const auto wakeup (std::chrono::steady_clock::now () + std::chrono::milliseconds (wait_ms));
						condition.wait_until (lock, wakeup);
					}
					pending.emplace_back (i->first, i->second);
				}
				condition.notify_all ();
			}
			{
				futurehead::lock_guard<std::mutex> lock (mutex);
				finished = true;
			}
			condition.notify_all ();
			for (auto & thread : threads)
			{
				thread.join ();
			}
			if (!silent)
			{
				std::cout << boost::str (boost::format ("%1% pending blocks validated\n") % count);
				timer.stop ();
				std::cout << boost::str (boost::format ("%1% %2% validation time\n") % timer.value ().count () % timer.unit ());
			}
			if (errors == 0)
			{
				std::cout << "Validation status: Ok\n";
			}
			else
			{
				std::cout << boost::str (boost::format ("Validation status: Failed\n%1% errors found\n") % errors);
			}
		}
		else if (vm.count ("debug_profile_bootstrap"))
		{
			auto node_flags = futurehead::inactive_node_flag_defaults ();
			node_flags.read_only = false;
			futurehead::update_flags (node_flags, vm);
			futurehead::inactive_node node2 (futurehead::unique_path (), node_flags);
			futurehead::genesis genesis;
			auto begin (std::chrono::high_resolution_clock::now ());
			uint64_t block_count (0);
			size_t count (0);
			{
				auto inactive_node = futurehead::default_inactive_node (data_path, vm);
				auto node = inactive_node->node;
				auto transaction (node->store.tx_begin_read ());
				block_count = node->ledger.cache.block_count;
				std::cout << boost::str (boost::format ("Performing bootstrap emulation, %1% blocks in ledger...") % block_count) << std::endl;
				for (auto i (node->store.latest_begin (transaction)), n (node->store.latest_end ()); i != n; ++i)
				{
					futurehead::account const & account (i->first);
					futurehead::account_info const & info (i->second);
					auto hash (info.head);
					while (!hash.is_zero ())
					{
						// Retrieving block data
						auto block (node->store.block_get_no_sideband (transaction, hash));
						if (block != nullptr)
						{
							++count;
							if ((count % 500000) == 0)
							{
								std::cout << boost::str (boost::format ("%1% blocks retrieved") % count) << std::endl;
							}
							futurehead::unchecked_info unchecked_info (block, account, 0, futurehead::signature_verification::unknown);
							node2.node->block_processor.add (unchecked_info);
							// Retrieving previous block hash
							hash = block->previous ();
						}
					}
				}
			}
			futurehead::timer<std::chrono::seconds> timer_l (futurehead::timer_state::started);
			while (node2.node->ledger.cache.block_count != block_count)
			{
				std::this_thread::sleep_for (std::chrono::milliseconds (50));
				// Message each 60 seconds
				if (timer_l.after_deadline (std::chrono::seconds (60)))
				{
					timer_l.restart ();
					std::cout << boost::str (boost::format ("%1% (%2%) blocks processed (unchecked)") % node2.node->ledger.cache.block_count % node2.node->ledger.cache.unchecked_count) << std::endl;
				}
			}
			// Waiting for final transaction commit
			uint64_t block_count_2 (0);
			while (block_count_2 != block_count)
			{
				std::this_thread::sleep_for (std::chrono::milliseconds (50));
				auto transaction_2 (node2.node->store.tx_begin_read ());
				block_count_2 = node2.node->store.block_count (transaction_2).sum ();
			}
			auto end (std::chrono::high_resolution_clock::now ());
			auto time (std::chrono::duration_cast<std::chrono::microseconds> (end - begin).count ());
			auto us_in_second (1000000);
			auto seconds (time / us_in_second);
			futurehead::remove_temporary_directories ();
			std::cout << boost::str (boost::format ("%|1$ 12d| seconds \n%2% blocks per second") % seconds % (block_count * us_in_second / time)) << std::endl;
		}
		else if (vm.count ("debug_peers"))
		{
			auto inactive_node = futurehead::default_inactive_node (data_path, vm);
			auto node = inactive_node->node;
			auto transaction (node->store.tx_begin_read ());

			for (auto i (node->store.peers_begin (transaction)), n (node->store.peers_end ()); i != n; ++i)
			{
				std::cout << boost::str (boost::format ("%1%\n") % futurehead::endpoint (boost::asio::ip::address_v6 (i->first.address_bytes ()), i->first.port ()));
			}
		}
		else if (vm.count ("debug_cemented_block_count"))
		{
			auto node_flags = futurehead::inactive_node_flag_defaults ();
			node_flags.generate_cache.cemented_count = true;
			futurehead::update_flags (node_flags, vm);
			futurehead::inactive_node node (data_path, node_flags);
			std::cout << "Total cemented block count: " << node.node->ledger.cache.cemented_count << std::endl;
		}
		else if (vm.count ("debug_stacktrace"))
		{
			std::cout << boost::stacktrace::stacktrace ();
		}
		else if (vm.count ("debug_sys_logging"))
		{
#ifdef BOOST_WINDOWS
			if (!futurehead::event_log_reg_entry_exists () && !futurehead::is_windows_elevated ())
			{
				std::cerr << "The event log requires the HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\EventLog\\Futurehead\\Futurehead registry entry, run again as administator to create it.\n";
				return 1;
			}
#endif
			auto inactive_node = futurehead::default_inactive_node (data_path, vm);
			inactive_node->node->logger.always_log (futurehead::severity_level::error, "Testing system logger");
		}
		else if (vm.count ("debug_account_versions"))
		{
			auto inactive_node = futurehead::default_inactive_node (data_path, vm);
			auto node = inactive_node->node;

			auto transaction (node->store.tx_begin_read ());
			std::vector<std::unordered_set<futurehead::account>> opened_account_versions (futurehead::normalized_epoch (futurehead::epoch::max));

			// Cache the accounts in a collection to make searching quicker against unchecked keys. Group by epoch
			for (auto i (node->store.latest_begin (transaction)), n (node->store.latest_end ()); i != n; ++i)
			{
				auto const & account (i->first);
				auto const & account_info (i->second);

				// Epoch 0 will be index 0 for instance
				auto epoch_idx = futurehead::normalized_epoch (account_info.epoch ());
				opened_account_versions[epoch_idx].emplace (account);
			}

			// Iterate all pending blocks and collect the highest version for each unopened account
			std::unordered_map<futurehead::account, std::underlying_type_t<futurehead::epoch>> unopened_highest_pending;
			for (auto i (node->store.pending_begin (transaction)), n (node->store.pending_end ()); i != n; ++i)
			{
				futurehead::pending_key const & key (i->first);
				futurehead::pending_info const & info (i->second);
				auto & account = key.account;
				auto exists = std::any_of (opened_account_versions.begin (), opened_account_versions.end (), [&account](auto const & account_version) {
					return account_version.find (account) != account_version.end ();
				});
				if (!exists)
				{
					// This is an unopened account, store the highest pending version
					auto it = unopened_highest_pending.find (key.account);
					auto epoch = futurehead::normalized_epoch (info.epoch);
					if (it != unopened_highest_pending.cend ())
					{
						// Found it, compare against existing value
						if (epoch > it->second)
						{
							it->second = epoch;
						}
					}
					else
					{
						// New unopened account
						unopened_highest_pending.emplace (key.account, epoch);
					}
				}
			}

			auto output_account_version_number = [](auto version, auto num_accounts) {
				std::cout << "Account version " << version << " num accounts: " << num_accounts << "\n";
			};

			// Output total version counts for the opened accounts
			std::cout << "Opened accounts:\n";
			for (auto i = 0u; i < opened_account_versions.size (); ++i)
			{
				output_account_version_number (i, opened_account_versions[i].size ());
			}

			// Accumulate the version numbers for the highest pending epoch for each unopened account.
			std::vector<size_t> unopened_account_version_totals (futurehead::normalized_epoch (futurehead::epoch::max));
			for (auto & pair : unopened_highest_pending)
			{
				++unopened_account_version_totals[pair.second];
			}

			// Output total version counts for the unopened accounts
			std::cout << "\nUnopened accounts:\n";
			for (auto i = 0u; i < unopened_account_version_totals.size (); ++i)
			{
				output_account_version_number (i, unopened_account_version_totals[i]);
			}
		}
		else if (vm.count ("version"))
		{
			std::cout << "Version " << FUTUREHEAD_VERSION_STRING << "\n"
			          << "Build Info " << BUILD_INFO << std::endl;
		}
		else
		{
			std::cout << description << std::endl;
			result = -1;
		}
	}
	return result;
}

namespace
{
std::istream & operator>> (std::istream & in, uint64_from_hex & out_val)
{
	in >> std::hex >> out_val.value;
	return in;
}

address_library_pair::address_library_pair (uint64_t address, std::string library) :
address (address), library (library)
{
}

bool address_library_pair::operator< (const address_library_pair & other) const
{
	return address < other.address;
}

bool address_library_pair::operator== (const address_library_pair & other) const
{
	return address == other.address;
}
}
