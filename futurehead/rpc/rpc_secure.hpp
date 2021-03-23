#pragma once
#include <futurehead/rpc/rpc.hpp>

namespace boost
{
namespace asio
{
	class io_context;
}
}

namespace futurehead
{
/**
 * Specialization of futurehead::rpc with TLS support
 */
class rpc_secure : public rpc
{
public:
	rpc_secure (boost::asio::io_context & context_a, futurehead::rpc_config const & config_a, futurehead::rpc_handler_interface & rpc_handler_interface_a);

	/** Starts accepting connections */
	void accept () override;
};
}
