#pragma once

#include <futurehead/boost/asio/ip/tcp.hpp>
#include <futurehead/lib/logger_mt.hpp>
#include <futurehead/lib/rpc_handler_interface.hpp>
#include <futurehead/lib/rpcconfig.hpp>

namespace boost
{
namespace asio
{
	class io_context;
}
}

namespace futurehead
{
class rpc_handler_interface;

class rpc
{
public:
	rpc (boost::asio::io_context & io_ctx_a, futurehead::rpc_config const & config_a, futurehead::rpc_handler_interface & rpc_handler_interface_a);
	virtual ~rpc ();
	void start ();
	virtual void accept ();
	void stop ();

	futurehead::rpc_config config;
	boost::asio::ip::tcp::acceptor acceptor;
	futurehead::logger_mt logger;
	boost::asio::io_context & io_ctx;
	futurehead::rpc_handler_interface & rpc_handler_interface;
	bool stopped{ false };
};

/** Returns the correct RPC implementation based on TLS configuration */
std::unique_ptr<futurehead::rpc> get_rpc (boost::asio::io_context & io_ctx_a, futurehead::rpc_config const & config_a, futurehead::rpc_handler_interface & rpc_handler_interface_a);
}
