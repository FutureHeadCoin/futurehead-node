#pragma once

#include <boost/config.hpp>
#include <boost/version.hpp>

#include <algorithm>
#include <string>

namespace boost
{
	namespace filesystem
	{
		class path;
	}
} // namespace boost

#define xstr(a) ver_str(a)
#define ver_str(a) #a

/**
* Returns build version information
*/
const char *const FUTUREHEAD_VERSION_STRING = xstr(TAG_VERSION_STRING);
const char *const FUTUREHEAD_MAJOR_VERSION_STRING = xstr(MAJOR_VERSION_STRING);
const char *const FUTUREHEAD_MINOR_VERSION_STRING = xstr(MINOR_VERSION_STRING);
const char *const FUTUREHEAD_PATCH_VERSION_STRING = xstr(PATCH_VERSION_STRING);
const char *const FUTUREHEAD_PRE_RELEASE_VERSION_STRING = xstr(PRE_RELEASE_VERSION_STRING);

const char *const BUILD_INFO = xstr(GIT_COMMIT_HASH BOOST_COMPILER) " \"BOOST " xstr(BOOST_VERSION) "\" BUILT " xstr(__DATE__);

/** Is TSAN/ASAN test build */
#if defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
const bool is_sanitizer_build = true;
#else
const bool is_sanitizer_build = false;
#endif
// GCC builds
#elif defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
const bool is_sanitizer_build = true;
#else
const bool is_sanitizer_build = false;
#endif

namespace futurehead
{
	uint8_t get_major_node_version();
	uint8_t get_minor_node_version();
	uint8_t get_patch_node_version();
	uint8_t get_pre_release_node_version();

	/**
 * Network variants with different genesis blocks and network parameters
 * @warning Enum values are used in integral comparisons; do not change.
 */
	enum class futurehead_networks
	{
		// Low work parameters, publicly known genesis key, test IP ports
		futurehead_test_network = 0,
		rai_test_network = 0,
		// Normal work parameters, secret beta genesis key, beta IP ports
		futurehead_beta_network = 1,
		rai_beta_network = 1,
		// Normal work parameters, secret live key, live IP ports
		futurehead_live_network = 2,
		rai_live_network = 2,
	};

	struct work_thresholds
	{
		uint64_t const epoch_1;
		uint64_t const epoch_2;
		uint64_t const epoch_2_receive;

		// Automatically calculated. The base threshold is the maximum of all thresholds and is used for all work multiplier calculations
		uint64_t const base;

		// Automatically calculated. The entry threshold is the minimum of all thresholds and defines the required work to enter the node, but does not guarantee a block is processed
		uint64_t const entry;

		constexpr work_thresholds(uint64_t epoch_1_a, uint64_t epoch_2_a, uint64_t epoch_2_receive_a) : epoch_1(epoch_1_a), epoch_2(epoch_2_a), epoch_2_receive(epoch_2_receive_a),
																										base(std::max({epoch_1, epoch_2, epoch_2_receive})),
																										entry(std::min({epoch_1, epoch_2, epoch_2_receive}))
		{
		}
		work_thresholds() = delete;
		work_thresholds operator=(futurehead::work_thresholds const &other_a)
		{
			return other_a;
		}
	};

	class network_constants
	{
	public:
		network_constants() : network_constants(network_constants::active_network)
		{
		}

		network_constants(futurehead_networks network_a) : current_network(network_a),
														   publish_thresholds(is_live_network() ? publish_full : is_beta_network() ? publish_beta : publish_test)
		{
			// A representative is classified as principal based on its weight and this factor
			principal_weight_factor = 1000; // 0.1%
			//TO-CHANGE PORTS OF THE WALLETS
			default_node_port = is_live_network() ? 7155 : is_beta_network() ? 54000 : 44000;
			default_rpc_port = is_live_network() ? 7156 : is_beta_network() ? 55000 : 45000;
			default_ipc_port = is_live_network() ? 7157 : is_beta_network() ? 56000 : 46000;
			default_websocket_port = is_live_network() ? 7158 : is_beta_network() ? 57000 : 47000;
			request_interval_ms = is_test_network() ? 20 : 500;
		}

		/** Network work thresholds. Define these inline as constexpr when moving to cpp17. */
		static const futurehead::work_thresholds publish_full;
		static const futurehead::work_thresholds publish_beta;
		static const futurehead::work_thresholds publish_test;

		/** Error message when an invalid network is specified */
		static const char *active_network_err_msg;

		/** The network this param object represents. This may differ from the global active network; this is needed for certain --debug... commands */
		futurehead_networks current_network{futurehead::network_constants::active_network};
		futurehead::work_thresholds publish_thresholds;

		unsigned principal_weight_factor;
		uint16_t default_node_port;
		uint16_t default_rpc_port;
		uint16_t default_ipc_port;
		uint16_t default_websocket_port;
		unsigned request_interval_ms;

		/** Returns the network this object contains values for */
		futurehead_networks network() const
		{
			return current_network;
		}

		/**
	 * Optionally called on startup to override the global active network.
	 * If not called, the compile-time option will be used.
	 * @param network_a The new active network
	 */
		static void set_active_network(futurehead_networks network_a)
		{
			active_network = network_a;
		}

		/**
	 * Optionally called on startup to override the global active network.
	 * If not called, the compile-time option will be used.
	 * @param network_a The new active network. Valid values are "live", "beta" and "test"
	 */
		static bool set_active_network(std::string network_a)
		{
			auto error{false};
			if (network_a == "live")
			{
				active_network = futurehead::futurehead_networks::futurehead_live_network;
			}
			else if (network_a == "beta")
			{
				active_network = futurehead::futurehead_networks::futurehead_beta_network;
			}
			else if (network_a == "test")
			{
				active_network = futurehead::futurehead_networks::futurehead_test_network;
			}
			else
			{
				error = true;
			}
			return error;
		}

		const char *get_current_network_as_string() const
		{
			return is_live_network() ? "live" : is_beta_network() ? "beta" : "test";
		}

		bool is_live_network() const
		{
			return current_network == futurehead_networks::futurehead_live_network;
		}
		bool is_beta_network() const
		{
			return current_network == futurehead_networks::futurehead_beta_network;
		}
		bool is_test_network() const
		{
			return current_network == futurehead_networks::futurehead_test_network;
		}

		/** Initial value is ACTIVE_NETWORK compile flag, but can be overridden by a CLI flag */
		static futurehead::futurehead_networks active_network;
	};

	std::string get_config_path(boost::filesystem::path const &data_path);
	std::string get_rpc_config_path(boost::filesystem::path const &data_path);
	std::string get_node_toml_config_path(boost::filesystem::path const &data_path);
	std::string get_rpc_toml_config_path(boost::filesystem::path const &data_path);
	std::string get_access_toml_config_path(boost::filesystem::path const &data_path);
	std::string get_qtwallet_toml_config_path(boost::filesystem::path const &data_path);
	std::string get_tls_toml_config_path(boost::filesystem::path const &data_path);

	/** Called by gtest_main to enforce test network */
	void force_futurehead_test_network();

	/** Checks if we are running inside a valgrind instance */
	bool running_within_valgrind();
} // namespace futurehead