#include <futurehead/lib/errors.hpp>
#include <futurehead/lib/json_error_response.hpp>
#include <futurehead/lib/logger_mt.hpp>
#include <futurehead/lib/rpc_handler_interface.hpp>
#include <futurehead/lib/rpcconfig.hpp>
#include <futurehead/rpc/rpc_handler.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <unordered_set>

namespace
{
std::unordered_set<std::string> create_rpc_control_impls ();
std::unordered_set<std::string> rpc_control_impl_set = create_rpc_control_impls ();
std::string filter_request (boost::property_tree::ptree tree_a);
}

futurehead::rpc_handler::rpc_handler (futurehead::rpc_config const & rpc_config, std::string const & body_a, std::string const & request_id_a, std::function<void(std::string const &)> const & response_a, futurehead::rpc_handler_interface & rpc_handler_interface_a, futurehead::logger_mt & logger) :
body (body_a),
request_id (request_id_a),
response (response_a),
rpc_config (rpc_config),
rpc_handler_interface (rpc_handler_interface_a),
logger (logger)
{
}

void futurehead::rpc_handler::process_request (futurehead::rpc_handler_request_params const & request_params)
{
	try
	{
		auto max_depth_exceeded (false);
		auto max_depth_possible (0u);
		for (auto ch : body)
		{
			if (ch == '[' || ch == '{')
			{
				if (max_depth_possible >= rpc_config.max_json_depth)
				{
					max_depth_exceeded = true;
					break;
				}
				++max_depth_possible;
			}
		}
		if (max_depth_exceeded)
		{
			json_error_response (response, "Max JSON depth exceeded");
		}
		else
		{
			if (request_params.rpc_version == 1)
			{
				boost::property_tree::ptree request;
				{
					std::stringstream ss;
					ss << body;
					boost::property_tree::read_json (ss, request);
				}

				auto action = request.get<std::string> ("action");
				if (rpc_config.rpc_logging.log_rpc)
				{
					// Creating same string via stringstream as using it directly is generating a TSAN warning
					std::stringstream ss;
					ss << request_id;
					logger.always_log (ss.str (), " ", filter_request (request));
				}

				// Check if this is a RPC command which requires RPC enabled control
				std::error_code rpc_control_disabled_ec = futurehead::error_rpc::rpc_control_disabled;

				bool error = false;
				auto found = rpc_control_impl_set.find (action);
				if (found != rpc_control_impl_set.cend () && !rpc_config.enable_control)
				{
					json_error_response (response, rpc_control_disabled_ec.message ());
					error = true;
				}
				else
				{
					// Special case with stats, type -> objects
					if (action == "stats" && !rpc_config.enable_control)
					{
						if (request.get<std::string> ("type") == "objects")
						{
							json_error_response (response, rpc_control_disabled_ec.message ());
							error = true;
						}
					}
					else if (action == "process")
					{
						auto force = request.get_optional<bool> ("force").value_or (false);
						auto watch_work = request.get_optional<bool> ("watch_work").value_or (true);
						if ((force || watch_work) && !rpc_config.enable_control)
						{
							json_error_response (response, rpc_control_disabled_ec.message ());
							error = true;
						}
					}
				}

				if (!error)
				{
					rpc_handler_interface.process_request (action, body, this->response);
				}
			}
			else if (request_params.rpc_version == 2)
			{
				rpc_handler_interface.process_request_v2 (request_params, body, [response = response](std::shared_ptr<std::string> body) {
					std::string body_l = *body;
					response (body_l);
				});
			}
			else
			{
				debug_assert (false);
				json_error_response (response, "Invalid RPC version");
			}
		}
	}
	catch (std::runtime_error const &)
	{
		json_error_response (response, "Unable to parse JSON");
	}
	catch (...)
	{
		json_error_response (response, "Internal server error in RPC");
	}
}

namespace
{
std::unordered_set<std::string> create_rpc_control_impls ()
{
	std::unordered_set<std::string> set;
	set.emplace ("account_create");
	set.emplace ("account_move");
	set.emplace ("account_remove");
	set.emplace ("account_representative_set");
	set.emplace ("accounts_create");
	set.emplace ("block_create");
	set.emplace ("bootstrap_lazy");
	set.emplace ("confirmation_height_currently_processing");
	set.emplace ("database_txn_tracker");
	set.emplace ("epoch_upgrade");
	set.emplace ("keepalive");
	set.emplace ("ledger");
	set.emplace ("node_id");
	set.emplace ("password_change");
	set.emplace ("receive");
	set.emplace ("receive_minimum");
	set.emplace ("receive_minimum_set");
	set.emplace ("search_pending");
	set.emplace ("search_pending_all");
	set.emplace ("send");
	set.emplace ("stop");
	set.emplace ("unchecked_clear");
	set.emplace ("unopened");
	set.emplace ("wallet_add");
	set.emplace ("wallet_add_watch");
	set.emplace ("wallet_change_seed");
	set.emplace ("wallet_create");
	set.emplace ("wallet_destroy");
	set.emplace ("wallet_lock");
	set.emplace ("wallet_representative_set");
	set.emplace ("wallet_republish");
	set.emplace ("wallet_work_get");
	set.emplace ("work_generate");
	set.emplace ("work_cancel");
	set.emplace ("work_get");
	set.emplace ("work_set");
	set.emplace ("work_peer_add");
	set.emplace ("work_peers");
	set.emplace ("work_peers_clear");
	set.emplace ("wallet_seed");
	return set;
}

std::string filter_request (boost::property_tree::ptree tree_a)
{
	// Replace password
	boost::optional<std::string> password_text (tree_a.get_optional<std::string> ("password"));
	if (password_text.is_initialized ())
	{
		tree_a.put ("password", "password");
	}
	// Save first 2 symbols of wallet, key, seed
	boost::optional<std::string> wallet_text (tree_a.get_optional<std::string> ("wallet"));
	if (wallet_text.is_initialized () && wallet_text.get ().length () > 2)
	{
		tree_a.put ("wallet", wallet_text.get ().replace (wallet_text.get ().begin () + 2, wallet_text.get ().end (), wallet_text.get ().length () - 2, 'X'));
	}
	boost::optional<std::string> key_text (tree_a.get_optional<std::string> ("key"));
	if (key_text.is_initialized () && key_text.get ().length () > 2)
	{
		tree_a.put ("key", key_text.get ().replace (key_text.get ().begin () + 2, key_text.get ().end (), key_text.get ().length () - 2, 'X'));
	}
	boost::optional<std::string> seed_text (tree_a.get_optional<std::string> ("seed"));
	if (seed_text.is_initialized () && seed_text.get ().length () > 2)
	{
		tree_a.put ("seed", seed_text.get ().replace (seed_text.get ().begin () + 2, seed_text.get ().end (), seed_text.get ().length () - 2, 'X'));
	}
	std::string result;
	std::stringstream stream;
	boost::property_tree::write_json (stream, tree_a, false);
	result = stream.str ();
	// removing std::endl
	if (result.length () > 1)
	{
		result.pop_back ();
	}
	return result;
}
}
