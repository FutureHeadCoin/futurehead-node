#define IGNORE_GTEST_INCL
#include <futurehead/core_test/testutil.hpp>
#include <futurehead/lib/threading.hpp>
#include <futurehead/lib/tomlconfig.hpp>
#include <futurehead/lib/utility.hpp>
#include <futurehead/node/common.hpp>
#include <futurehead/node/daemonconfig.hpp>
#include <futurehead/node/node.hpp>
#include <futurehead/node/telemetry.hpp>
#include <futurehead/node/websocket.hpp>
#include <futurehead/rpc/rpc.hpp>
#include <futurehead/secure/buffer.hpp>

#if FUTUREHEAD_ROCKSDB
#include <futurehead/node/rocksdb/rocksdb.hpp>
#endif

#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <cstdlib>
#include <future>
#include <sstream>

double constexpr futurehead::node::price_max;
double constexpr futurehead::node::free_cutoff;
size_t constexpr futurehead::block_arrival::arrival_size_min;
std::chrono::seconds constexpr futurehead::block_arrival::arrival_time_min;

namespace futurehead
{
extern unsigned char futurehead_bootstrap_weights_live[];
extern size_t futurehead_bootstrap_weights_live_size;
extern unsigned char futurehead_bootstrap_weights_beta[];
extern size_t futurehead_bootstrap_weights_beta_size;
}

void futurehead::node::keepalive (std::string const & address_a, uint16_t port_a)
{
	auto node_l (shared_from_this ());
	network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (address_a, std::to_string (port_a)), [node_l, address_a, port_a](boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a) {
		if (!ec)
		{
			for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator{}); i != n; ++i)
			{
				auto endpoint (futurehead::transport::map_endpoint_to_v6 (i->endpoint ()));
				std::weak_ptr<futurehead::node> node_w (node_l);
				auto channel (node_l->network.find_channel (endpoint));
				if (!channel)
				{
					node_l->network.tcp_channels.start_tcp (endpoint, [node_w](std::shared_ptr<futurehead::transport::channel> channel_a) {
						if (auto node_l = node_w.lock ())
						{
							node_l->network.send_keepalive (channel_a);
						}
					});
				}
				else
				{
					node_l->network.send_keepalive (channel);
				}
			}
		}
		else
		{
			node_l->logger.try_log (boost::str (boost::format ("Error resolving address: %1%:%2%: %3%") % address_a % port_a % ec.message ()));
		}
	});
}

std::unique_ptr<futurehead::container_info_component> futurehead::collect_container_info (rep_crawler & rep_crawler, const std::string & name)
{
	size_t count;
	{
		futurehead::lock_guard<std::mutex> guard (rep_crawler.active_mutex);
		count = rep_crawler.active.size ();
	}

	auto sizeof_element = sizeof (decltype (rep_crawler.active)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "active", count, sizeof_element }));
	return composite;
}

futurehead::node::node (boost::asio::io_context & io_ctx_a, uint16_t peering_port_a, boost::filesystem::path const & application_path_a, futurehead::alarm & alarm_a, futurehead::logging const & logging_a, futurehead::work_pool & work_a, futurehead::node_flags flags_a, unsigned seq) :
node (io_ctx_a, application_path_a, alarm_a, futurehead::node_config (peering_port_a, logging_a), work_a, flags_a, seq)
{
}

futurehead::node::node (boost::asio::io_context & io_ctx_a, boost::filesystem::path const & application_path_a, futurehead::alarm & alarm_a, futurehead::node_config const & config_a, futurehead::work_pool & work_a, futurehead::node_flags flags_a, unsigned seq) :
io_ctx (io_ctx_a),
node_initialized_latch (1),
config (config_a),
stats (config.stat_config),
flags (flags_a),
alarm (alarm_a),
work (work_a),
distributed_work (*this),
logger (config_a.logging.min_time_between_log_output),
store_impl (futurehead::make_store (logger, application_path_a, flags.read_only, true, config_a.rocksdb_config, config_a.diagnostics_config.txn_tracking, config_a.block_processor_batch_max_time, config_a.lmdb_config, flags.sideband_batch_size, config_a.backup_before_upgrade, config_a.rocksdb_config.enable)),
store (*store_impl),
wallets_store_impl (std::make_unique<futurehead::mdb_wallets_store> (application_path_a / "wallets.ldb", config_a.lmdb_config)),
wallets_store (*wallets_store_impl),
gap_cache (*this),
ledger (store, stats, flags_a.generate_cache, [this]() { this->network.erase_below_version (network_params.protocol.protocol_version_min (true)); }),
checker (config.signature_checker_threads),
network (*this, config.peering_port),
telemetry (std::make_shared<futurehead::telemetry> (network, alarm, worker, observers.telemetry, stats, network_params, flags.disable_ongoing_telemetry_requests)),
bootstrap_initiator (*this),
bootstrap (config.peering_port, *this),
application_path (application_path_a),
port_mapping (*this),
vote_processor (checker, active, observers, stats, config, flags, logger, online_reps, ledger, network_params),
rep_crawler (*this),
warmed_up (0),
block_processor (*this, write_database_queue),
// clang-format off
block_processor_thread ([this]() {
	futurehead::thread_role::set (futurehead::thread_role::name::block_processing);
	this->block_processor.process_blocks ();
}),
// clang-format on
online_reps (ledger, network_params, config.online_weight_minimum.number ()),
votes_cache (wallets),
vote_uniquer (block_uniquer),
confirmation_height_processor (ledger, write_database_queue, config.conf_height_processor_batch_min_time, logger, node_initialized_latch, flags.confirmation_height_processor_mode),
active (*this, confirmation_height_processor),
aggregator (network_params.network, config, stats, votes_cache, ledger, wallets, active),
payment_observer_processor (observers.blocks),
wallets (wallets_store.init_error (), *this),
startup_time (std::chrono::steady_clock::now ()),
node_seq (seq)
{
	if (!init_error ())
	{
		telemetry->start ();

		if (config.websocket_config.enabled)
		{
			auto endpoint_l (futurehead::tcp_endpoint (boost::asio::ip::make_address_v6 (config.websocket_config.address), config.websocket_config.port));
			websocket_server = std::make_shared<futurehead::websocket::listener> (config.websocket_config.tls_config, logger, wallets, io_ctx, endpoint_l);
			this->websocket_server->run ();
		}

		wallets.observer = [this](bool active) {
			observers.wallet.notify (active);
		};
		network.channel_observer = [this](std::shared_ptr<futurehead::transport::channel> channel_a) {
			debug_assert (channel_a != nullptr);
			observers.endpoint.notify (channel_a);
		};
		network.disconnect_observer = [this]() {
			observers.disconnect.notify ();
		};
		if (!config.callback_address.empty ())
		{
			observers.blocks.add ([this](futurehead::election_status const & status_a, futurehead::account const & account_a, futurehead::amount const & amount_a, bool is_state_send_a) {
				auto block_a (status_a.winner);
				if ((status_a.type == futurehead::election_status_type::active_confirmed_quorum || status_a.type == futurehead::election_status_type::active_confirmation_height) && this->block_arrival.recent (block_a->hash ()))
				{
					auto node_l (shared_from_this ());
					background ([node_l, block_a, account_a, amount_a, is_state_send_a]() {
						boost::property_tree::ptree event;
						event.add ("account", account_a.to_account ());
						event.add ("hash", block_a->hash ().to_string ());
						std::string block_text;
						block_a->serialize_json (block_text);
						event.add ("block", block_text);
						event.add ("amount", amount_a.to_string_dec ());
						if (is_state_send_a)
						{
							event.add ("is_send", is_state_send_a);
							event.add ("subtype", "send");
						}
						// Subtype field
						else if (block_a->type () == futurehead::block_type::state)
						{
							if (block_a->link ().is_zero ())
							{
								event.add ("subtype", "change");
							}
							else if (amount_a == 0 && node_l->ledger.is_epoch_link (block_a->link ()))
							{
								event.add ("subtype", "epoch");
							}
							else
							{
								event.add ("subtype", "receive");
							}
						}
						std::stringstream ostream;
						boost::property_tree::write_json (ostream, event);
						ostream.flush ();
						auto body (std::make_shared<std::string> (ostream.str ()));
						auto address (node_l->config.callback_address);
						auto port (node_l->config.callback_port);
						auto target (std::make_shared<std::string> (node_l->config.callback_target));
						auto resolver (std::make_shared<boost::asio::ip::tcp::resolver> (node_l->io_ctx));
						resolver->async_resolve (boost::asio::ip::tcp::resolver::query (address, std::to_string (port)), [node_l, address, port, target, body, resolver](boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::iterator i_a) {
							if (!ec)
							{
								node_l->do_rpc_callback (i_a, address, port, target, body, resolver);
							}
							else
							{
								if (node_l->config.logging.callback_logging ())
								{
									node_l->logger.always_log (boost::str (boost::format ("Error resolving callback: %1%:%2%: %3%") % address % port % ec.message ()));
								}
								node_l->stats.inc (futurehead::stat::type::error, futurehead::stat::detail::http_callback, futurehead::stat::dir::out);
							}
						});
					});
				}
			});
		}
		if (websocket_server)
		{
			observers.blocks.add ([this](futurehead::election_status const & status_a, futurehead::account const & account_a, futurehead::amount const & amount_a, bool is_state_send_a) {
				debug_assert (status_a.type != futurehead::election_status_type::ongoing);

				if (this->websocket_server->any_subscriber (futurehead::websocket::topic::confirmation))
				{
					auto block_a (status_a.winner);
					std::string subtype;
					if (is_state_send_a)
					{
						subtype = "send";
					}
					else if (block_a->type () == futurehead::block_type::state)
					{
						if (block_a->link ().is_zero ())
						{
							subtype = "change";
						}
						else if (amount_a == 0 && this->ledger.is_epoch_link (block_a->link ()))
						{
							subtype = "epoch";
						}
						else
						{
							subtype = "receive";
						}
					}

					this->websocket_server->broadcast_confirmation (block_a, account_a, amount_a, subtype, status_a);
				}
			});

			observers.active_stopped.add ([this](futurehead::block_hash const & hash_a) {
				if (this->websocket_server->any_subscriber (futurehead::websocket::topic::stopped_election))
				{
					futurehead::websocket::message_builder builder;
					this->websocket_server->broadcast (builder.stopped_election (hash_a));
				}
			});

			observers.difficulty.add ([this](uint64_t active_difficulty) {
				if (this->websocket_server->any_subscriber (futurehead::websocket::topic::active_difficulty))
				{
					futurehead::websocket::message_builder builder;
					auto msg (builder.difficulty_changed (this->default_difficulty (futurehead::work_version::work_1), active_difficulty));
					this->websocket_server->broadcast (msg);
				}
			});

			observers.telemetry.add ([this](futurehead::telemetry_data const & telemetry_data, futurehead::endpoint const & endpoint) {
				if (this->websocket_server->any_subscriber (futurehead::websocket::topic::telemetry))
				{
					futurehead::websocket::message_builder builder;
					this->websocket_server->broadcast (builder.telemetry_received (telemetry_data, endpoint));
				}
			});
		}
		// Add block confirmation type stats regardless of http-callback and websocket subscriptions
		observers.blocks.add ([this](futurehead::election_status const & status_a, futurehead::account const & account_a, futurehead::amount const & amount_a, bool is_state_send_a) {
			debug_assert (status_a.type != futurehead::election_status_type::ongoing);
			switch (status_a.type)
			{
				case futurehead::election_status_type::active_confirmed_quorum:
					this->stats.inc (futurehead::stat::type::confirmation_observer, futurehead::stat::detail::active_quorum, futurehead::stat::dir::out);
					break;
				case futurehead::election_status_type::active_confirmation_height:
					this->stats.inc (futurehead::stat::type::confirmation_observer, futurehead::stat::detail::active_conf_height, futurehead::stat::dir::out);
					break;
				case futurehead::election_status_type::inactive_confirmation_height:
					this->stats.inc (futurehead::stat::type::confirmation_observer, futurehead::stat::detail::inactive_conf_height, futurehead::stat::dir::out);
					break;
				default:
					break;
			}
		});
		observers.endpoint.add ([this](std::shared_ptr<futurehead::transport::channel> channel_a) {
			if (channel_a->get_type () == futurehead::transport::transport_type::udp)
			{
				this->network.send_keepalive (channel_a);
			}
			else
			{
				this->network.send_keepalive_self (channel_a);
			}
		});
		observers.vote.add ([this](std::shared_ptr<futurehead::vote> vote_a, std::shared_ptr<futurehead::transport::channel> channel_a, futurehead::vote_code code_a) {
			debug_assert (code_a != futurehead::vote_code::invalid);
			if (code_a != futurehead::vote_code::replay)
			{
				auto active_in_rep_crawler (!this->rep_crawler.response (channel_a, vote_a));
				if (active_in_rep_crawler || code_a == futurehead::vote_code::vote)
				{
					// Representative is defined as online if replying to live votes or rep_crawler queries
					this->online_reps.observe (vote_a->account);
				}
			}
			if (code_a == futurehead::vote_code::indeterminate)
			{
				this->gap_cache.vote (vote_a);
			}
		});
		if (websocket_server)
		{
			observers.vote.add ([this](std::shared_ptr<futurehead::vote> vote_a, std::shared_ptr<futurehead::transport::channel> channel_a, futurehead::vote_code code_a) {
				if (this->websocket_server->any_subscriber (futurehead::websocket::topic::vote))
				{
					futurehead::websocket::message_builder builder;
					auto msg (builder.vote_received (vote_a, code_a));
					this->websocket_server->broadcast (msg);
				}
			});
		}
		// Cancelling local work generation
		observers.work_cancel.add ([this](futurehead::root const & root_a) {
			this->work.cancel (root_a);
			this->distributed_work.cancel (root_a);
		});

		logger.always_log ("Node starting, version: ", FUTUREHEAD_VERSION_STRING);
		logger.always_log ("Build information: ", BUILD_INFO);
		logger.always_log ("Database backend: ", store.vendor_get ());

		auto network_label = network_params.network.get_current_network_as_string ();
		logger.always_log ("Active network: ", network_label);

		logger.always_log (boost::str (boost::format ("Work pool running %1% threads %2%") % work.threads.size () % (work.opencl ? "(1 for OpenCL)" : "")));
		logger.always_log (boost::str (boost::format ("%1% work peers configured") % config.work_peers.size ()));
		if (!work_generation_enabled ())
		{
			logger.always_log ("Work generation is disabled");
		}

		if (config.logging.node_lifetime_tracing ())
		{
			logger.always_log ("Constructing node");
		}

		logger.always_log (boost::str (boost::format ("Outbound Voting Bandwidth limited to %1% bytes per second, burst ratio %2%") % config.bandwidth_limit % config.bandwidth_limit_burst_ratio));

		// First do a pass with a read to see if any writing needs doing, this saves needing to open a write lock (and potentially blocking)
		auto is_initialized (false);
		{
			auto transaction (store.tx_begin_read ());
			is_initialized = (store.latest_begin (transaction) != store.latest_end ());
		}

		futurehead::genesis genesis;
		if (!is_initialized)
		{
			release_assert (!flags.read_only);
			auto transaction (store.tx_begin_write ({ tables::accounts, tables::cached_counts, tables::confirmation_height, tables::frontiers, tables::open_blocks }));
			// Store was empty meaning we just created it, add the genesis block
			store.initialize (transaction, genesis, ledger.cache);
		}

		if (!ledger.block_exists (genesis.hash ()))
		{
			std::stringstream ss;
			ss << "Genesis block not found. Make sure the node network ID is correct.";
			if (network_params.network.is_beta_network ())
			{
				ss << " Beta network may have reset, try clearing database files";
			}
			auto str = ss.str ();

			logger.always_log (str);
			std::cerr << str << std::endl;
			std::exit (1);
		}

		if (config.enable_voting)
		{
			std::ostringstream stream;
			stream << "Voting is enabled, more system resources will be used";
			auto voting (wallets.reps ().voting);
			if (voting > 0)
			{
				stream << ". " << voting << " representative(s) are configured";
				if (voting > 1)
				{
					stream << ". Voting with more than one representative can limit performance";
				}
			}
			logger.always_log (stream.str ());
		}

		node_id = futurehead::keypair ();
		logger.always_log ("Node ID: ", node_id.pub.to_node_id ());

		if ((network_params.network.is_live_network () || network_params.network.is_beta_network ()) && !flags.inactive_node)
		{
			auto bootstrap_weights = get_bootstrap_weights ();
			// Use bootstrap weights if initial bootstrap is not completed
			bool use_bootstrap_weight = ledger.cache.block_count < bootstrap_weights.first;
			if (use_bootstrap_weight)
			{
				ledger.bootstrap_weight_max_blocks = bootstrap_weights.first;
				ledger.bootstrap_weights = bootstrap_weights.second;
				for (auto const & rep : ledger.bootstrap_weights)
				{
					logger.always_log ("Using bootstrap rep weight: ", rep.first.to_account (), " -> ", futurehead::uint128_union (rep.second).format_balance (Mxrb_ratio, 0, true), " XRB");
				}
			}

			// Drop unchecked blocks if initial bootstrap is completed
			if (!flags.disable_unchecked_drop && !use_bootstrap_weight && !flags.read_only)
			{
				auto transaction (store.tx_begin_write ({ tables::unchecked }));
				store.unchecked_clear (transaction);
				ledger.cache.unchecked_count = 0;
				logger.always_log ("Dropping unchecked blocks");
			}
		}
	}
	node_initialized_latch.count_down ();
}

futurehead::node::~node ()
{
	if (config.logging.node_lifetime_tracing ())
	{
		logger.always_log ("Destructing node");
	}
	stop ();
}

void futurehead::node::do_rpc_callback (boost::asio::ip::tcp::resolver::iterator i_a, std::string const & address, uint16_t port, std::shared_ptr<std::string> target, std::shared_ptr<std::string> body, std::shared_ptr<boost::asio::ip::tcp::resolver> resolver)
{
	if (i_a != boost::asio::ip::tcp::resolver::iterator{})
	{
		auto node_l (shared_from_this ());
		auto sock (std::make_shared<boost::asio::ip::tcp::socket> (node_l->io_ctx));
		sock->async_connect (i_a->endpoint (), [node_l, target, body, sock, address, port, i_a, resolver](boost::system::error_code const & ec) mutable {
			if (!ec)
			{
				auto req (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
				req->method (boost::beast::http::verb::post);
				req->target (*target);
				req->version (11);
				req->insert (boost::beast::http::field::host, address);
				req->insert (boost::beast::http::field::content_type, "application/json");
				req->body () = *body;
				req->prepare_payload ();
				boost::beast::http::async_write (*sock, *req, [node_l, sock, address, port, req, i_a, target, body, resolver](boost::system::error_code const & ec, size_t bytes_transferred) mutable {
					if (!ec)
					{
						auto sb (std::make_shared<boost::beast::flat_buffer> ());
						auto resp (std::make_shared<boost::beast::http::response<boost::beast::http::string_body>> ());
						boost::beast::http::async_read (*sock, *sb, *resp, [node_l, sb, resp, sock, address, port, i_a, target, body, resolver](boost::system::error_code const & ec, size_t bytes_transferred) mutable {
							if (!ec)
							{
								if (boost::beast::http::to_status_class (resp->result ()) == boost::beast::http::status_class::successful)
								{
									node_l->stats.inc (futurehead::stat::type::http_callback, futurehead::stat::detail::initiate, futurehead::stat::dir::out);
								}
								else
								{
									if (node_l->config.logging.callback_logging ())
									{
										node_l->logger.try_log (boost::str (boost::format ("Callback to %1%:%2% failed with status: %3%") % address % port % resp->result ()));
									}
									node_l->stats.inc (futurehead::stat::type::error, futurehead::stat::detail::http_callback, futurehead::stat::dir::out);
								}
							}
							else
							{
								if (node_l->config.logging.callback_logging ())
								{
									node_l->logger.try_log (boost::str (boost::format ("Unable complete callback: %1%:%2%: %3%") % address % port % ec.message ()));
								}
								node_l->stats.inc (futurehead::stat::type::error, futurehead::stat::detail::http_callback, futurehead::stat::dir::out);
							};
						});
					}
					else
					{
						if (node_l->config.logging.callback_logging ())
						{
							node_l->logger.try_log (boost::str (boost::format ("Unable to send callback: %1%:%2%: %3%") % address % port % ec.message ()));
						}
						node_l->stats.inc (futurehead::stat::type::error, futurehead::stat::detail::http_callback, futurehead::stat::dir::out);
					}
				});
			}
			else
			{
				if (node_l->config.logging.callback_logging ())
				{
					node_l->logger.try_log (boost::str (boost::format ("Unable to connect to callback address: %1%:%2%: %3%") % address % port % ec.message ()));
				}
				node_l->stats.inc (futurehead::stat::type::error, futurehead::stat::detail::http_callback, futurehead::stat::dir::out);
				++i_a;
				node_l->do_rpc_callback (i_a, address, port, target, body, resolver);
			}
		});
	}
}

bool futurehead::node::copy_with_compaction (boost::filesystem::path const & destination)
{
	return store.copy_db (destination);
}

void futurehead::node::process_fork (futurehead::transaction const & transaction_a, std::shared_ptr<futurehead::block> block_a, uint64_t modified_a)
{
	auto root (block_a->root ());
	if (!store.block_exists (transaction_a, block_a->type (), block_a->hash ()) && store.root_exists (transaction_a, block_a->root ()))
	{
		std::shared_ptr<futurehead::block> ledger_block (ledger.forked_block (transaction_a, *block_a));
		if (ledger_block && !block_confirmed_or_being_confirmed (transaction_a, ledger_block->hash ()) && (ledger.can_vote (transaction_a, *ledger_block) || modified_a < futurehead::seconds_since_epoch () - 300 || !block_arrival.recent (block_a->hash ())))
		{
			std::weak_ptr<futurehead::node> this_w (shared_from_this ());
			auto election = active.insert (ledger_block, boost::none, [this_w, root](std::shared_ptr<futurehead::block>) {
				if (auto this_l = this_w.lock ())
				{
					auto attempt (this_l->bootstrap_initiator.current_attempt ());
					if (attempt && attempt->mode == futurehead::bootstrap_mode::legacy)
					{
						auto transaction (this_l->store.tx_begin_read ());
						auto account (this_l->ledger.store.frontier_get (transaction, root));
						if (!account.is_zero ())
						{
							this_l->bootstrap_initiator.connections->requeue_pull (futurehead::pull_info (account, root, root, attempt->incremental_id));
						}
						else if (this_l->ledger.store.account_exists (transaction, root))
						{
							this_l->bootstrap_initiator.connections->requeue_pull (futurehead::pull_info (root, futurehead::block_hash (0), futurehead::block_hash (0), attempt->incremental_id));
						}
					}
				}
			});
			if (election.inserted)
			{
				logger.always_log (boost::str (boost::format ("Resolving fork between our block: %1% and block %2% both with root %3%") % ledger_block->hash ().to_string () % block_a->hash ().to_string () % block_a->root ().to_string ()));
				election.election->transition_active ();
			}
		}
		active.publish (block_a);
	}
}

std::unique_ptr<futurehead::container_info_component> futurehead::collect_container_info (node & node, const std::string & name)
{
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (collect_container_info (node.alarm, "alarm"));
	composite->add_component (collect_container_info (node.work, "work"));
	composite->add_component (collect_container_info (node.gap_cache, "gap_cache"));
	composite->add_component (collect_container_info (node.ledger, "ledger"));
	composite->add_component (collect_container_info (node.active, "active"));
	composite->add_component (collect_container_info (node.bootstrap_initiator, "bootstrap_initiator"));
	composite->add_component (collect_container_info (node.bootstrap, "bootstrap"));
	composite->add_component (collect_container_info (node.network, "network"));
	if (node.telemetry)
	{
		composite->add_component (collect_container_info (*node.telemetry, "telemetry"));
	}
	composite->add_component (collect_container_info (node.observers, "observers"));
	composite->add_component (collect_container_info (node.wallets, "wallets"));
	composite->add_component (collect_container_info (node.vote_processor, "vote_processor"));
	composite->add_component (collect_container_info (node.rep_crawler, "rep_crawler"));
	composite->add_component (collect_container_info (node.block_processor, "block_processor"));
	composite->add_component (collect_container_info (node.block_arrival, "block_arrival"));
	composite->add_component (collect_container_info (node.online_reps, "online_reps"));
	composite->add_component (collect_container_info (node.votes_cache, "votes_cache"));
	composite->add_component (collect_container_info (node.block_uniquer, "block_uniquer"));
	composite->add_component (collect_container_info (node.vote_uniquer, "vote_uniquer"));
	composite->add_component (collect_container_info (node.confirmation_height_processor, "confirmation_height_processor"));
	composite->add_component (collect_container_info (node.worker, "worker"));
	composite->add_component (collect_container_info (node.distributed_work, "distributed_work"));
	composite->add_component (collect_container_info (node.aggregator, "request_aggregator"));
	return composite;
}

void futurehead::node::process_active (std::shared_ptr<futurehead::block> incoming)
{
	block_arrival.add (incoming->hash ());
	block_processor.add (incoming, futurehead::seconds_since_epoch ());
}

futurehead::process_return futurehead::node::process (futurehead::block & block_a)
{
	auto transaction (store.tx_begin_write ({ tables::accounts, tables::cached_counts, tables::change_blocks, tables::frontiers, tables::open_blocks, tables::pending, tables::receive_blocks, tables::representation, tables::send_blocks, tables::state_blocks }, { tables::confirmation_height }));
	auto result (ledger.process (transaction, block_a));
	return result;
}

futurehead::process_return futurehead::node::process_local (std::shared_ptr<futurehead::block> block_a, bool const work_watcher_a)
{
	// Add block hash as recently arrived to trigger automatic rebroadcast and election
	block_arrival.add (block_a->hash ());
	// Set current time to trigger automatic rebroadcast and election
	futurehead::unchecked_info info (block_a, block_a->account (), futurehead::seconds_since_epoch (), futurehead::signature_verification::unknown);
	// Notify block processor to release write lock
	block_processor.wait_write ();
	// Process block
	block_post_events events;
	auto transaction (store.tx_begin_write ({ tables::accounts, tables::cached_counts, tables::change_blocks, tables::frontiers, tables::open_blocks, tables::pending, tables::receive_blocks, tables::representation, tables::send_blocks, tables::state_blocks }, { tables::confirmation_height }));
	return block_processor.process_one (transaction, events, info, work_watcher_a, futurehead::block_origin::local);
}

void futurehead::node::start ()
{
	long_inactivity_cleanup ();
	network.start ();
	add_initial_peers ();
	if (!flags.disable_legacy_bootstrap)
	{
		ongoing_bootstrap ();
	}
	if (!flags.disable_unchecked_cleanup)
	{
		auto this_l (shared ());
		worker.push_task ([this_l]() {
			this_l->ongoing_unchecked_cleanup ();
		});
	}
	ongoing_store_flush ();
	if (!flags.disable_rep_crawler)
	{
		rep_crawler.start ();
	}
	ongoing_rep_calculation ();
	ongoing_peer_store ();
	ongoing_online_weight_calculation_queue ();
	bool tcp_enabled (false);
	if (config.tcp_incoming_connections_max > 0 && !(flags.disable_bootstrap_listener && flags.disable_tcp_realtime))
	{
		bootstrap.start ();
		tcp_enabled = true;
	}
	if (!flags.disable_backup)
	{
		backup_wallet ();
	}
	search_pending ();
	if (!flags.disable_wallet_bootstrap)
	{
		// Delay to start wallet lazy bootstrap
		auto this_l (shared ());
		alarm.add (std::chrono::steady_clock::now () + std::chrono::minutes (1), [this_l]() {
			this_l->bootstrap_wallet ();
		});
	}
	// Start port mapping if external address is not defined and TCP or UDP ports are enabled
	if (config.external_address == boost::asio::ip::address_v6{}.any ().to_string () && (tcp_enabled || !flags.disable_udp))
	{
		port_mapping.start ();
	}
}

void futurehead::node::stop ()
{
	if (!stopped.exchange (true))
	{
		logger.always_log ("Node stopping");
		// Cancels ongoing work generation tasks, which may be blocking other threads
		// No tasks may wait for work generation in I/O threads, or termination signal capturing will be unable to call node::stop()
		distributed_work.stop ();
		block_processor.stop ();
		if (block_processor_thread.joinable ())
		{
			block_processor_thread.join ();
		}
		aggregator.stop ();
		vote_processor.stop ();
		active.stop ();
		confirmation_height_processor.stop ();
		network.stop ();
		if (telemetry)
		{
			telemetry->stop ();
			telemetry = nullptr;
		}
		if (websocket_server)
		{
			websocket_server->stop ();
		}
		bootstrap_initiator.stop ();
		bootstrap.stop ();
		port_mapping.stop ();
		checker.stop ();
		wallets.stop ();
		stats.stop ();
		worker.stop ();
		auto epoch_upgrade = epoch_upgrading.lock ();
		if (epoch_upgrade->valid ())
		{
			epoch_upgrade->wait ();
		}
		// work pool is not stopped on purpose due to testing setup
	}
}

void futurehead::node::keepalive_preconfigured (std::vector<std::string> const & peers_a)
{
	for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
	{
		keepalive (*i, network_params.network.default_node_port);
	}
}

futurehead::block_hash futurehead::node::latest (futurehead::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.latest (transaction, account_a);
}

futurehead::uint128_t futurehead::node::balance (futurehead::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.account_balance (transaction, account_a);
}

std::shared_ptr<futurehead::block> futurehead::node::block (futurehead::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	return store.block_get (transaction, hash_a);
}

std::pair<futurehead::uint128_t, futurehead::uint128_t> futurehead::node::balance_pending (futurehead::account const & account_a)
{
	std::pair<futurehead::uint128_t, futurehead::uint128_t> result;
	auto transaction (store.tx_begin_read ());
	result.first = ledger.account_balance (transaction, account_a);
	result.second = ledger.account_pending (transaction, account_a);
	return result;
}

futurehead::uint128_t futurehead::node::weight (futurehead::account const & account_a)
{
	return ledger.weight (account_a);
}

futurehead::block_hash futurehead::node::rep_block (futurehead::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	futurehead::account_info info;
	futurehead::block_hash result (0);
	if (!store.account_get (transaction, account_a, info))
	{
		result = ledger.representative (transaction, info.head);
	}
	return result;
}

futurehead::uint128_t futurehead::node::minimum_principal_weight ()
{
	return minimum_principal_weight (online_reps.online_stake ());
}

futurehead::uint128_t futurehead::node::minimum_principal_weight (futurehead::uint128_t const & online_stake)
{
	return online_stake / network_params.network.principal_weight_factor;
}

void futurehead::node::long_inactivity_cleanup ()
{
	bool perform_cleanup = false;
	auto transaction (store.tx_begin_write ({ tables::online_weight, tables::peers }));
	if (store.online_weight_count (transaction) > 0)
	{
		auto i (store.online_weight_begin (transaction));
		auto sample (store.online_weight_begin (transaction));
		auto n (store.online_weight_end ());
		while (++i != n)
		{
			++sample;
		}
		debug_assert (sample != n);
		auto const one_week_ago = (std::chrono::system_clock::now () - std::chrono::hours (7 * 24)).time_since_epoch ().count ();
		perform_cleanup = sample->first < one_week_ago;
	}
	if (perform_cleanup)
	{
		store.online_weight_clear (transaction);
		store.peer_clear (transaction);
		logger.always_log ("Removed records of peers and online weight after a long period of inactivity");
	}
}

void futurehead::node::ongoing_rep_calculation ()
{
	auto now (std::chrono::steady_clock::now ());
	vote_processor.calculate_weights ();
	std::weak_ptr<futurehead::node> node_w (shared_from_this ());
	alarm.add (now + std::chrono::minutes (10), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_rep_calculation ();
		}
	});
}

void futurehead::node::ongoing_bootstrap ()
{
	auto next_wakeup (300);
	if (warmed_up < 3)
	{
		// Re-attempt bootstrapping more aggressively on startup
		next_wakeup = 5;
		if (!bootstrap_initiator.in_progress () && !network.empty ())
		{
			++warmed_up;
		}
	}
	bootstrap_initiator.bootstrap ();
	std::weak_ptr<futurehead::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (next_wakeup), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_bootstrap ();
		}
	});
}

void futurehead::node::ongoing_store_flush ()
{
	{
		auto transaction (store.tx_begin_write ({ tables::vote }));
		store.flush (transaction);
	}
	std::weak_ptr<futurehead::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->worker.push_task ([node_l]() {
				node_l->ongoing_store_flush ();
			});
		}
	});
}

void futurehead::node::ongoing_peer_store ()
{
	bool stored (network.tcp_channels.store_all (true));
	network.udp_channels.store_all (!stored);
	std::weak_ptr<futurehead::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + network_params.node.peer_interval, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->worker.push_task ([node_l]() {
				node_l->ongoing_peer_store ();
			});
		}
	});
}

void futurehead::node::backup_wallet ()
{
	auto transaction (wallets.tx_begin_read ());
	for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n; ++i)
	{
		boost::system::error_code error_chmod;
		auto backup_path (application_path / "backup");

		boost::filesystem::create_directories (backup_path);
		futurehead::set_secure_perm_directory (backup_path, error_chmod);
		i->second->store.write_backup (transaction, backup_path / (i->first.to_string () + ".json"));
	}
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + network_params.node.backup_interval, [this_l]() {
		this_l->backup_wallet ();
	});
}

void futurehead::node::search_pending ()
{
	// Reload wallets from disk
	wallets.reload ();
	// Search pending
	wallets.search_pending_all ();
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + network_params.node.search_pending_interval, [this_l]() {
		this_l->worker.push_task ([this_l]() {
			this_l->search_pending ();
		});
	});
}

void futurehead::node::bootstrap_wallet ()
{
	std::deque<futurehead::account> accounts;
	{
		futurehead::lock_guard<std::mutex> lock (wallets.mutex);
		auto transaction (wallets.tx_begin_read ());
		for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n && accounts.size () < 128; ++i)
		{
			auto & wallet (*i->second);
			futurehead::lock_guard<std::recursive_mutex> wallet_lock (wallet.store.mutex);
			for (auto j (wallet.store.begin (transaction)), m (wallet.store.end ()); j != m && accounts.size () < 128; ++j)
			{
				futurehead::account account (j->first);
				accounts.push_back (account);
			}
		}
	}
	if (!accounts.empty ())
	{
		bootstrap_initiator.bootstrap_wallet (accounts);
	}
}

void futurehead::node::unchecked_cleanup ()
{
	std::vector<futurehead::uint128_t> digests;
	std::deque<futurehead::unchecked_key> cleaning_list;
	auto attempt (bootstrap_initiator.current_attempt ());
	bool long_attempt (attempt != nullptr && std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - attempt->attempt_start).count () > config.unchecked_cutoff_time.count ());
	// Collect old unchecked keys
	if (!flags.disable_unchecked_cleanup && ledger.cache.block_count >= ledger.bootstrap_weight_max_blocks && !long_attempt)
	{
		auto now (futurehead::seconds_since_epoch ());
		auto transaction (store.tx_begin_read ());
		// Max 1M records to clean, max 2 minutes reading to prevent slow i/o systems issues
		for (auto i (store.unchecked_begin (transaction)), n (store.unchecked_end ()); i != n && cleaning_list.size () < 1024 * 1024 && futurehead::seconds_since_epoch () - now < 120; ++i)
		{
			futurehead::unchecked_key const & key (i->first);
			futurehead::unchecked_info const & info (i->second);
			if ((now - info.modified) > static_cast<uint64_t> (config.unchecked_cutoff_time.count ()))
			{
				digests.push_back (network.publish_filter.hash (info.block));
				cleaning_list.push_back (key);
			}
		}
	}
	if (!cleaning_list.empty ())
	{
		logger.always_log (boost::str (boost::format ("Deleting %1% old unchecked blocks") % cleaning_list.size ()));
	}
	// Delete old unchecked keys in batches
	while (!cleaning_list.empty ())
	{
		size_t deleted_count (0);
		auto transaction (store.tx_begin_write ({ tables::unchecked }));
		while (deleted_count++ < 2 * 1024 && !cleaning_list.empty ())
		{
			auto key (cleaning_list.front ());
			cleaning_list.pop_front ();
			if (store.unchecked_exists (transaction, key))
			{
				store.unchecked_del (transaction, key);
				debug_assert (ledger.cache.unchecked_count > 0);
				--ledger.cache.unchecked_count;
			}
		}
	}
	// Delete from the duplicate filter
	network.publish_filter.clear (digests);
}

void futurehead::node::ongoing_unchecked_cleanup ()
{
	unchecked_cleanup ();
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + network_params.node.unchecked_cleaning_interval, [this_l]() {
		this_l->worker.push_task ([this_l]() {
			this_l->ongoing_unchecked_cleanup ();
		});
	});
}

int futurehead::node::price (futurehead::uint128_t const & balance_a, int amount_a)
{
	debug_assert (balance_a >= amount_a * futurehead::Gxrb_ratio);
	auto balance_l (balance_a);
	double result (0.0);
	for (auto i (0); i < amount_a; ++i)
	{
		balance_l -= futurehead::Gxrb_ratio;
		auto balance_scaled ((balance_l / futurehead::Mxrb_ratio).convert_to<double> ());
		auto units (balance_scaled / 1000.0);
		auto unit_price (((free_cutoff - units) / free_cutoff) * price_max);
		result += std::min (std::max (0.0, unit_price), price_max);
	}
	return static_cast<int> (result * 100.0);
}

uint64_t futurehead::node::default_difficulty (futurehead::work_version const version_a) const
{
	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	switch (version_a)
	{
		case futurehead::work_version::work_1:
			result = ledger.cache.epoch_2_started ? futurehead::work_threshold_base (version_a) : network_params.network.publish_thresholds.epoch_1;
			break;
		default:
			debug_assert (false && "Invalid version specified to default_difficulty");
	}
	return result;
}

uint64_t futurehead::node::max_work_generate_difficulty (futurehead::work_version const version_a) const
{
	return futurehead::difficulty::from_multiplier (config.max_work_generate_multiplier, default_difficulty (version_a));
}

bool futurehead::node::local_work_generation_enabled () const
{
	return config.work_threads > 0 || work.opencl;
}

bool futurehead::node::work_generation_enabled () const
{
	return work_generation_enabled (config.work_peers);
}

bool futurehead::node::work_generation_enabled (std::vector<std::pair<std::string, uint16_t>> const & peers_a) const
{
	return !peers_a.empty () || local_work_generation_enabled ();
}

boost::optional<uint64_t> futurehead::node::work_generate_blocking (futurehead::block & block_a, uint64_t difficulty_a)
{
	auto opt_work_l (work_generate_blocking (block_a.work_version (), block_a.root (), difficulty_a, block_a.account ()));
	if (opt_work_l.is_initialized ())
	{
		block_a.block_work_set (*opt_work_l);
	}
	return opt_work_l;
}

void futurehead::node::work_generate (futurehead::work_version const version_a, futurehead::root const & root_a, uint64_t difficulty_a, std::function<void(boost::optional<uint64_t>)> callback_a, boost::optional<futurehead::account> const & account_a, bool secondary_work_peers_a)
{
	auto const & peers_l (secondary_work_peers_a ? config.secondary_work_peers : config.work_peers);
	if (distributed_work.make (version_a, root_a, peers_l, difficulty_a, callback_a, account_a))
	{
		// Error in creating the job (either stopped or work generation is not possible)
		callback_a (boost::none);
	}
}

boost::optional<uint64_t> futurehead::node::work_generate_blocking (futurehead::work_version const version_a, futurehead::root const & root_a, uint64_t difficulty_a, boost::optional<futurehead::account> const & account_a)
{
	std::promise<boost::optional<uint64_t>> promise;
	work_generate (
	version_a, root_a, difficulty_a, [&promise](boost::optional<uint64_t> opt_work_a) {
		promise.set_value (opt_work_a);
	},
	account_a);
	return promise.get_future ().get ();
}

boost::optional<uint64_t> futurehead::node::work_generate_blocking (futurehead::block & block_a)
{
	debug_assert (network_params.network.is_test_network ());
	return work_generate_blocking (block_a, default_difficulty (futurehead::work_version::work_1));
}

boost::optional<uint64_t> futurehead::node::work_generate_blocking (futurehead::root const & root_a)
{
	debug_assert (network_params.network.is_test_network ());
	return work_generate_blocking (root_a, default_difficulty (futurehead::work_version::work_1));
}

boost::optional<uint64_t> futurehead::node::work_generate_blocking (futurehead::root const & root_a, uint64_t difficulty_a)
{
	debug_assert (network_params.network.is_test_network ());
	return work_generate_blocking (futurehead::work_version::work_1, root_a, difficulty_a);
}

void futurehead::node::add_initial_peers ()
{
	auto transaction (store.tx_begin_read ());
	for (auto i (store.peers_begin (transaction)), n (store.peers_end ()); i != n; ++i)
	{
		futurehead::endpoint endpoint (boost::asio::ip::address_v6 (i->first.address_bytes ()), i->first.port ());
		if (!network.reachout (endpoint, config.allow_local_peers))
		{
			std::weak_ptr<futurehead::node> node_w (shared_from_this ());
			network.tcp_channels.start_tcp (endpoint, [node_w](std::shared_ptr<futurehead::transport::channel> channel_a) {
				if (auto node_l = node_w.lock ())
				{
					node_l->network.send_keepalive (channel_a);
					if (!node_l->flags.disable_rep_crawler)
					{
						node_l->rep_crawler.query (channel_a);
					}
				}
			});
		}
	}
}

void futurehead::node::block_confirm (std::shared_ptr<futurehead::block> block_a)
{
	auto election = active.insert (block_a);
	if (election.inserted)
	{
		election.election->transition_active ();
	}
}

bool futurehead::node::block_confirmed (futurehead::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	return store.block_exists (transaction, hash_a) && ledger.block_confirmed (transaction, hash_a);
}

bool futurehead::node::block_confirmed_or_being_confirmed (futurehead::transaction const & transaction_a, futurehead::block_hash const & hash_a)
{
	return confirmation_height_processor.is_processing_block (hash_a) || ledger.block_confirmed (transaction_a, hash_a);
}

futurehead::uint128_t futurehead::node::delta () const
{
	auto result ((online_reps.online_stake () / 100) * config.online_weight_quorum);
	return result;
}

void futurehead::node::ongoing_online_weight_calculation_queue ()
{
	std::weak_ptr<futurehead::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + (std::chrono::seconds (network_params.node.weight_period)), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->worker.push_task ([node_l]() {
				node_l->ongoing_online_weight_calculation ();
			});
		}
	});
}

bool futurehead::node::online () const
{
	return rep_crawler.total_weight () > (std::max (config.online_weight_minimum.number (), delta ()));
}

void futurehead::node::ongoing_online_weight_calculation ()
{
	online_reps.sample ();
	ongoing_online_weight_calculation_queue ();
}

namespace
{
class confirmed_visitor : public futurehead::block_visitor
{
public:
	confirmed_visitor (futurehead::transaction const & transaction_a, futurehead::node & node_a, std::shared_ptr<futurehead::block> const & block_a, futurehead::block_hash const & hash_a) :
	transaction (transaction_a),
	node (node_a),
	block (block_a),
	hash (hash_a)
	{
	}
	virtual ~confirmed_visitor () = default;
	void scan_receivable (futurehead::account const & account_a)
	{
		for (auto i (node.wallets.items.begin ()), n (node.wallets.items.end ()); i != n; ++i)
		{
			auto const & wallet (i->second);
			auto transaction_l (node.wallets.tx_begin_read ());
			if (wallet->store.exists (transaction_l, account_a))
			{
				futurehead::account representative;
				futurehead::pending_info pending;
				representative = wallet->store.representative (transaction_l);
				auto error (node.store.pending_get (transaction, futurehead::pending_key (account_a, hash), pending));
				if (!error)
				{
					auto node_l (node.shared ());
					auto amount (pending.amount.number ());
					wallet->receive_async (block, representative, amount, [](std::shared_ptr<futurehead::block>) {});
				}
				else
				{
					if (!node.store.block_exists (transaction, hash))
					{
						node.logger.try_log (boost::str (boost::format ("Confirmed block is missing:  %1%") % hash.to_string ()));
						debug_assert (false && "Confirmed block is missing");
					}
					else
					{
						node.logger.try_log (boost::str (boost::format ("Block %1% has already been received") % hash.to_string ()));
					}
				}
			}
		}
	}
	void state_block (futurehead::state_block const & block_a) override
	{
		scan_receivable (block_a.hashables.link);
	}
	void send_block (futurehead::send_block const & block_a) override
	{
		scan_receivable (block_a.hashables.destination);
	}
	void receive_block (futurehead::receive_block const &) override
	{
	}
	void open_block (futurehead::open_block const &) override
	{
	}
	void change_block (futurehead::change_block const &) override
	{
	}
	futurehead::transaction const & transaction;
	futurehead::node & node;
	std::shared_ptr<futurehead::block> block;
	futurehead::block_hash const & hash;
};
}

void futurehead::node::receive_confirmed (futurehead::transaction const & transaction_a, std::shared_ptr<futurehead::block> block_a, futurehead::block_hash const & hash_a)
{
	confirmed_visitor visitor (transaction_a, *this, block_a, hash_a);
	block_a->visit (visitor);
}

void futurehead::node::process_confirmed_data (futurehead::transaction const & transaction_a, std::shared_ptr<futurehead::block> block_a, futurehead::block_hash const & hash_a, futurehead::account & account_a, futurehead::uint128_t & amount_a, bool & is_state_send_a, futurehead::account & pending_account_a)
{
	// Faster account calculation
	account_a = block_a->account ();
	if (account_a.is_zero ())
	{
		account_a = block_a->sideband ().account;
	}
	// Faster amount calculation
	auto previous (block_a->previous ());
	auto previous_balance (ledger.balance (transaction_a, previous));
	auto block_balance (store.block_balance_calculated (block_a));
	if (hash_a != ledger.network_params.ledger.genesis_account)
	{
		amount_a = block_balance > previous_balance ? block_balance - previous_balance : previous_balance - block_balance;
	}
	else
	{
		amount_a = ledger.network_params.ledger.genesis_amount;
	}
	if (auto state = dynamic_cast<futurehead::state_block *> (block_a.get ()))
	{
		if (state->hashables.balance < previous_balance)
		{
			is_state_send_a = true;
		}
		pending_account_a = state->hashables.link;
	}
	if (auto send = dynamic_cast<futurehead::send_block *> (block_a.get ()))
	{
		pending_account_a = send->hashables.destination;
	}
}

void futurehead::node::process_confirmed (futurehead::election_status const & status_a, uint64_t iteration_a)
{
	auto block_a (status_a.winner);
	auto hash (block_a->hash ());
	const auto num_iters = (config.block_processor_batch_max_time / network_params.node.process_confirmed_interval) * 4;
	if (ledger.block_exists (block_a->type (), hash))
	{
		confirmation_height_processor.add (hash);
	}
	else if (iteration_a < num_iters)
	{
		iteration_a++;
		std::weak_ptr<futurehead::node> node_w (shared ());
		alarm.add (std::chrono::steady_clock::now () + network_params.node.process_confirmed_interval, [node_w, status_a, iteration_a]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->process_confirmed (status_a, iteration_a);
			}
		});
	}
	else
	{
		// Do some cleanup due to this block never being processed by confirmation height processor
		active.remove_election_winner_details (hash);
	}
}

bool futurehead::block_arrival::add (futurehead::block_hash const & hash_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	auto inserted (arrival.get<tag_sequence> ().emplace_back (futurehead::block_arrival_info{ now, hash_a }));
	auto result (!inserted.second);
	return result;
}

bool futurehead::block_arrival::recent (futurehead::block_hash const & hash_a)
{
	futurehead::lock_guard<std::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	while (arrival.size () > arrival_size_min && arrival.get<tag_sequence> ().front ().arrival + arrival_time_min < now)
	{
		arrival.get<tag_sequence> ().pop_front ();
	}
	return arrival.get<tag_hash> ().find (hash_a) != arrival.get<tag_hash> ().end ();
}

std::unique_ptr<futurehead::container_info_component> futurehead::collect_container_info (block_arrival & block_arrival, const std::string & name)
{
	size_t count = 0;
	{
		futurehead::lock_guard<std::mutex> guard (block_arrival.mutex);
		count = block_arrival.arrival.size ();
	}

	auto sizeof_element = sizeof (decltype (block_arrival.arrival)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "arrival", count, sizeof_element }));
	return composite;
}

std::shared_ptr<futurehead::node> futurehead::node::shared ()
{
	return shared_from_this ();
}

int futurehead::node::store_version ()
{
	auto transaction (store.tx_begin_read ());
	return store.version_get (transaction);
}

bool futurehead::node::init_error () const
{
	return store.init_error () || wallets_store.init_error ();
}

bool futurehead::node::epoch_upgrader (futurehead::private_key const & prv_a, futurehead::epoch epoch_a, uint64_t count_limit, uint64_t threads)
{
	bool error = stopped.load ();
	if (!error)
	{
		auto epoch_upgrade = epoch_upgrading.lock ();
		error = epoch_upgrade->valid () && epoch_upgrade->wait_for (std::chrono::seconds (0)) == std::future_status::timeout;
		if (!error)
		{
			*epoch_upgrade = std::async (std::launch::async, &futurehead::node::epoch_upgrader_impl, this, prv_a, epoch_a, count_limit, threads);
		}
	}
	return error;
}

void futurehead::node::epoch_upgrader_impl (futurehead::private_key const & prv_a, futurehead::epoch epoch_a, uint64_t count_limit, uint64_t threads)
{
	futurehead::thread_role::set (futurehead::thread_role::name::epoch_upgrader);
	auto upgrader_process = [](futurehead::node & node_a, std::atomic<uint64_t> & counter, std::shared_ptr<futurehead::block> epoch, uint64_t difficulty, futurehead::public_key const & signer_a, futurehead::root const & root_a, futurehead::account const & account_a) {
		epoch->block_work_set (node_a.work_generate_blocking (futurehead::work_version::work_1, root_a, difficulty).value_or (0));
		bool valid_signature (!futurehead::validate_message (signer_a, epoch->hash (), epoch->block_signature ()));
		bool valid_work (epoch->difficulty () >= difficulty);
		futurehead::process_result result (futurehead::process_result::old);
		if (valid_signature && valid_work)
		{
			result = node_a.process_local (epoch).code;
		}
		if (result == futurehead::process_result::progress)
		{
			++counter;
		}
		else
		{
			bool fork (result == futurehead::process_result::fork);
			node_a.logger.always_log (boost::str (boost::format ("Failed to upgrade account %1%. Valid signature: %2%. Valid work: %3%. Block processor fork: %4%") % account_a.to_account () % valid_signature % valid_work % fork));
		}
	};

	uint64_t const upgrade_batch_size = 1000;
	futurehead::block_builder builder;
	auto link (ledger.epoch_link (epoch_a));
	futurehead::raw_key raw_key;
	raw_key.data = prv_a;
	auto signer (futurehead::pub_key (prv_a));
	debug_assert (signer == ledger.epoch_signer (link));

	std::mutex upgrader_mutex;
	futurehead::condition_variable upgrader_condition;

	class account_upgrade_item final
	{
	public:
		futurehead::account account{ 0 };
		uint64_t modified{ 0 };
	};
	class account_tag
	{
	};
	class modified_tag
	{
	};
	// clang-format off
	boost::multi_index_container<account_upgrade_item,
	boost::multi_index::indexed_by<
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<modified_tag>,
			boost::multi_index::member<account_upgrade_item, uint64_t, &account_upgrade_item::modified>,
			std::greater<uint64_t>>,
		boost::multi_index::hashed_unique<boost::multi_index::tag<account_tag>,
			boost::multi_index::member<account_upgrade_item, futurehead::account, &account_upgrade_item::account>>>>
	accounts_list;
	// clang-format on

	bool finished_upgrade (false);

	while (!finished_upgrade && !stopped)
	{
		bool finished_accounts (false);
		uint64_t total_upgraded_accounts (0);
		while (!finished_accounts && count_limit != 0 && !stopped)
		{
			{
				auto transaction (store.tx_begin_read ());
				// Collect accounts to upgrade
				for (auto i (store.latest_begin (transaction)), n (store.latest_end ()); i != n && accounts_list.size () < count_limit; ++i)
				{
					futurehead::account const & account (i->first);
					futurehead::account_info const & info (i->second);
					if (info.epoch () < epoch_a)
					{
						release_assert (futurehead::epochs::is_sequential (info.epoch (), epoch_a));
						accounts_list.emplace (account_upgrade_item{ account, info.modified });
					}
				}
			}

			/* Upgrade accounts
			Repeat until accounts with previous epoch exist in latest table */
			std::atomic<uint64_t> upgraded_accounts (0);
			uint64_t workers (0);
			uint64_t attempts (0);
			for (auto i (accounts_list.get<modified_tag> ().begin ()), n (accounts_list.get<modified_tag> ().end ()); i != n && attempts < upgrade_batch_size && attempts < count_limit && !stopped; ++i)
			{
				auto transaction (store.tx_begin_read ());
				futurehead::account_info info;
				futurehead::account const & account (i->account);
				if (!store.account_get (transaction, account, info) && info.epoch () < epoch_a)
				{
					++attempts;
					auto difficulty (futurehead::work_threshold (futurehead::work_version::work_1, futurehead::block_details (epoch_a, false, false, true)));
					futurehead::root const & root (info.head);
					std::shared_ptr<futurehead::block> epoch = builder.state ()
					                                     .account (account)
					                                     .previous (info.head)
					                                     .representative (info.representative)
					                                     .balance (info.balance)
					                                     .link (link)
					                                     .sign (raw_key, signer)
					                                     .work (0)
					                                     .build ();
					if (threads != 0)
					{
						{
							futurehead::unique_lock<std::mutex> lock (upgrader_mutex);
							++workers;
							while (workers > threads)
							{
								upgrader_condition.wait (lock);
							}
						}
						worker.push_task ([node_l = shared_from_this (), &upgrader_process, &upgrader_mutex, &upgrader_condition, &upgraded_accounts, &workers, epoch, difficulty, signer, root, account]() {
							upgrader_process (*node_l, upgraded_accounts, epoch, difficulty, signer, root, account);
							{
								futurehead::lock_guard<std::mutex> lock (upgrader_mutex);
								--workers;
							}
							upgrader_condition.notify_all ();
						});
					}
					else
					{
						upgrader_process (*this, upgraded_accounts, epoch, difficulty, signer, root, account);
					}
				}
			}
			{
				futurehead::unique_lock<std::mutex> lock (upgrader_mutex);
				while (workers > 0)
				{
					upgrader_condition.wait (lock);
				}
			}
			total_upgraded_accounts += upgraded_accounts;
			count_limit -= upgraded_accounts;

			if (!accounts_list.empty ())
			{
				logger.always_log (boost::str (boost::format ("%1% accounts were upgraded to new epoch, %2% remain...") % total_upgraded_accounts % (accounts_list.size () - upgraded_accounts)));
				accounts_list.clear ();
			}
			else
			{
				logger.always_log (boost::str (boost::format ("%1% total accounts were upgraded to new epoch") % total_upgraded_accounts));
				finished_accounts = true;
			}
		}

		// Pending blocks upgrade
		bool finished_pending (false);
		uint64_t total_upgraded_pending (0);
		while (!finished_pending && count_limit != 0 && !stopped)
		{
			std::atomic<uint64_t> upgraded_pending (0);
			uint64_t workers (0);
			uint64_t attempts (0);
			auto transaction (store.tx_begin_read ());
			for (auto i (store.pending_begin (transaction, futurehead::pending_key (1, 0))), n (store.pending_end ()); i != n && attempts < upgrade_batch_size && attempts < count_limit && !stopped;)
			{
				bool to_next_account (false);
				futurehead::pending_key const & key (i->first);
				if (!store.account_exists (transaction, key.account))
				{
					futurehead::pending_info const & info (i->second);
					if (info.epoch < epoch_a)
					{
						++attempts;
						release_assert (futurehead::epochs::is_sequential (info.epoch, epoch_a));
						auto difficulty (futurehead::work_threshold (futurehead::work_version::work_1, futurehead::block_details (epoch_a, false, false, true)));
						futurehead::root const & root (key.account);
						futurehead::account const & account (key.account);
						std::shared_ptr<futurehead::block> epoch = builder.state ()
						                                     .account (key.account)
						                                     .previous (0)
						                                     .representative (0)
						                                     .balance (0)
						                                     .link (link)
						                                     .sign (raw_key, signer)
						                                     .work (0)
						                                     .build ();
						if (threads != 0)
						{
							{
								futurehead::unique_lock<std::mutex> lock (upgrader_mutex);
								++workers;
								while (workers > threads)
								{
									upgrader_condition.wait (lock);
								}
							}
							worker.push_task ([node_l = shared_from_this (), &upgrader_process, &upgrader_mutex, &upgrader_condition, &upgraded_pending, &workers, epoch, difficulty, signer, root, account]() {
								upgrader_process (*node_l, upgraded_pending, epoch, difficulty, signer, root, account);
								{
									futurehead::lock_guard<std::mutex> lock (upgrader_mutex);
									--workers;
								}
								upgrader_condition.notify_all ();
							});
						}
						else
						{
							upgrader_process (*this, upgraded_pending, epoch, difficulty, signer, root, account);
						}
					}
				}
				else
				{
					to_next_account = true;
				}
				if (to_next_account)
				{
					// Move to next account if pending account exists or was upgraded
					if (key.account.number () == std::numeric_limits<futurehead::uint256_t>::max ())
					{
						break;
					}
					else
					{
						i = store.pending_begin (transaction, futurehead::pending_key (key.account.number () + 1, 0));
					}
				}
				else
				{
					// Move to next pending item
					++i;
				}
			}
			{
				futurehead::unique_lock<std::mutex> lock (upgrader_mutex);
				while (workers > 0)
				{
					upgrader_condition.wait (lock);
				}
			}

			total_upgraded_pending += upgraded_pending;
			count_limit -= upgraded_pending;

			// Repeat if some pending accounts were upgraded
			if (upgraded_pending != 0)
			{
				logger.always_log (boost::str (boost::format ("%1% unopened accounts with pending blocks were upgraded to new epoch...") % total_upgraded_pending));
			}
			else
			{
				logger.always_log (boost::str (boost::format ("%1% total unopened accounts with pending blocks were upgraded to new epoch") % total_upgraded_pending));
				finished_pending = true;
			}
		}

		finished_upgrade = (total_upgraded_accounts == 0) && (total_upgraded_pending == 0);
	}

	logger.always_log ("Epoch upgrade is completed");
}

std::pair<uint64_t, decltype (futurehead::ledger::bootstrap_weights)> futurehead::node::get_bootstrap_weights () const
{
	std::unordered_map<futurehead::account, futurehead::uint128_t> weights;
	const uint8_t * weight_buffer = network_params.network.is_live_network () ? futurehead_bootstrap_weights_live : futurehead_bootstrap_weights_beta;
	size_t weight_size = network_params.network.is_live_network () ? futurehead_bootstrap_weights_live_size : futurehead_bootstrap_weights_beta_size;
	futurehead::bufferstream weight_stream ((const uint8_t *)weight_buffer, weight_size);
	futurehead::uint128_union block_height;
	uint64_t max_blocks = 0;
	if (!futurehead::try_read (weight_stream, block_height))
	{
		max_blocks = futurehead::narrow_cast<uint64_t> (block_height.number ());
		while (true)
		{
			futurehead::account account;
			if (futurehead::try_read (weight_stream, account.bytes))
			{
				break;
			}
			futurehead::amount weight;
			if (futurehead::try_read (weight_stream, weight.bytes))
			{
				break;
			}
			weights[account] = weight.number ();
		}
	}
	return { max_blocks, weights };
}

futurehead::inactive_node::inactive_node (boost::filesystem::path const & path_a, futurehead::node_flags const & node_flags_a) :
io_context (std::make_shared<boost::asio::io_context> ()),
alarm (*io_context),
work (1)
{
	boost::system::error_code error_chmod;

	/*
	 * @warning May throw a filesystem exception
	 */
	boost::filesystem::create_directories (path_a);
	futurehead::set_secure_perm_directory (path_a, error_chmod);
	futurehead::daemon_config daemon_config (path_a);
	auto error = futurehead::read_node_config_toml (path_a, daemon_config, node_flags_a.config_overrides);
	if (error)
	{
		std::cerr << "Error deserializing config file";
		if (!node_flags_a.config_overrides.empty ())
		{
			std::cerr << " or --config option";
		}
		std::cerr << "\n"
		          << error.get_message () << std::endl;
		std::exit (1);
	}

	auto & node_config = daemon_config.node;
	node_config.peering_port = futurehead::get_available_port ();
	node_config.logging.max_size = std::numeric_limits<std::uintmax_t>::max ();
	node_config.logging.init (path_a);

	node = std::make_shared<futurehead::node> (*io_context, path_a, alarm, node_config, work, node_flags_a);
	node->active.stop ();
}

futurehead::inactive_node::~inactive_node ()
{
	node->stop ();
}

futurehead::node_flags const & futurehead::inactive_node_flag_defaults ()
{
	static futurehead::node_flags node_flags;
	node_flags.inactive_node = true;
	node_flags.read_only = true;
	node_flags.generate_cache.reps = false;
	node_flags.generate_cache.cemented_count = false;
	node_flags.generate_cache.unchecked_count = false;
	node_flags.generate_cache.account_count = false;
	node_flags.generate_cache.epoch_2 = false;
	node_flags.disable_bootstrap_listener = true;
	node_flags.disable_tcp_realtime = true;
	return node_flags;
}

std::unique_ptr<futurehead::block_store> futurehead::make_store (futurehead::logger_mt & logger, boost::filesystem::path const & path, bool read_only, bool add_db_postfix, futurehead::rocksdb_config const & rocksdb_config, futurehead::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a, futurehead::lmdb_config const & lmdb_config_a, size_t batch_size, bool backup_before_upgrade, bool use_rocksdb_backend)
{
#if FUTUREHEAD_ROCKSDB
	auto make_rocksdb = [&logger, add_db_postfix, &path, &rocksdb_config, read_only]() {
		return std::make_unique<futurehead::rocksdb_store> (logger, add_db_postfix ? path / "rocksdb" : path, rocksdb_config, read_only);
	};
#endif

	if (use_rocksdb_backend)
	{
#if FUTUREHEAD_ROCKSDB
		return make_rocksdb ();
#else
		logger.always_log (std::error_code (futurehead::error_config::rocksdb_enabled_but_not_supported).message ());
		release_assert (false);
		return nullptr;
#endif
	}
	else
	{
#if FUTUREHEAD_ROCKSDB
		/** To use RocksDB in tests make sure the node is built with the cmake variable -DFUTUREHEAD_ROCKSDB=ON and the environment variable TEST_USE_ROCKSDB=1 is set */
		static futurehead::network_constants network_constants;
		auto use_rocksdb_str = std::getenv ("TEST_USE_ROCKSDB");
		if (use_rocksdb_str && (boost::lexical_cast<int> (use_rocksdb_str) == 1) && network_constants.is_test_network ())
		{
			return make_rocksdb ();
		}
#endif
	}

	return std::make_unique<futurehead::mdb_store> (logger, add_db_postfix ? path / "data.ldb" : path, txn_tracking_config_a, block_processor_batch_max_time_a, lmdb_config_a, batch_size, backup_before_upgrade);
}
