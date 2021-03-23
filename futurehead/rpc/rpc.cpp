#include <futurehead/boost/asio/bind_executor.hpp>
#include <futurehead/lib/rpc_handler_interface.hpp>
#include <futurehead/lib/tlsconfig.hpp>
#include <futurehead/rpc/rpc.hpp>
#include <futurehead/rpc/rpc_connection.hpp>

#include <boost/format.hpp>

#include <iostream>

#ifdef FUTUREHEAD_SECURE_RPC
#include <futurehead/rpc/rpc_secure.hpp>
#endif

futurehead::rpc::rpc (boost::asio::io_context & io_ctx_a, futurehead::rpc_config const & config_a, futurehead::rpc_handler_interface & rpc_handler_interface_a) :
config (config_a),
acceptor (io_ctx_a),
logger (std::chrono::milliseconds (0)),
io_ctx (io_ctx_a),
rpc_handler_interface (rpc_handler_interface_a)
{
	rpc_handler_interface.rpc_instance (*this);
}

futurehead::rpc::~rpc ()
{
	if (!stopped)
	{
		stop ();
	}
}

void futurehead::rpc::start ()
{
	auto endpoint (boost::asio::ip::tcp::endpoint (boost::asio::ip::make_address_v6 (config.address), config.port));
	if (!endpoint.address ().is_loopback () && config.enable_control)
	{
		auto warning = boost::str (boost::format ("WARNING: control-level RPCs are enabled on non-local address %1%, potentially allowing wallet access outside local computer") % endpoint.address ().to_string ());
		std::cout << warning << std::endl;
		logger.always_log (warning);
	}
	acceptor.open (endpoint.protocol ());
	acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));

	boost::system::error_code ec;
	acceptor.bind (endpoint, ec);
	if (ec)
	{
		logger.always_log (boost::str (boost::format ("Error while binding for RPC on port %1%: %2%") % endpoint.port () % ec.message ()));
		throw std::runtime_error (ec.message ());
	}
	acceptor.listen ();
	accept ();
}

void futurehead::rpc::accept ()
{
	auto connection (std::make_shared<futurehead::rpc_connection> (config, io_ctx, logger, rpc_handler_interface));
	acceptor.async_accept (connection->socket, boost::asio::bind_executor (connection->strand, [this, connection](boost::system::error_code const & ec) {
		if (ec != boost::asio::error::operation_aborted && acceptor.is_open ())
		{
			accept ();
		}
		if (!ec)
		{
			connection->parse_connection ();
		}
		else
		{
			logger.always_log (boost::str (boost::format ("Error accepting RPC connections: %1% (%2%)") % ec.message () % ec.value ()));
		}
	}));
}

void futurehead::rpc::stop ()
{
	stopped = true;
	acceptor.close ();
}

std::unique_ptr<futurehead::rpc> futurehead::get_rpc (boost::asio::io_context & io_ctx_a, futurehead::rpc_config const & config_a, futurehead::rpc_handler_interface & rpc_handler_interface_a)
{
	std::unique_ptr<rpc> impl;

	if (config_a.tls_config && config_a.tls_config->enable_https)
	{
#ifdef FUTUREHEAD_SECURE_RPC
		impl = std::make_unique<rpc_secure> (io_ctx_a, config_a, rpc_handler_interface_a);
#endif
	}
	else
	{
		impl = std::make_unique<rpc> (io_ctx_a, config_a, rpc_handler_interface_a);
	}

	return impl;
}
