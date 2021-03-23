#pragma once

#include <futurehead/ipc_flatbuffers_lib/generated/flatbuffers/futureheadapi_generated.h>
#include <futurehead/lib/errors.hpp>
#include <futurehead/lib/ipc.hpp>
#include <futurehead/node/ipc/ipc_access_config.hpp>
#include <futurehead/node/ipc/ipc_broker.hpp>
#include <futurehead/node/node_rpc_config.hpp>

#include <atomic>
#include <mutex>

namespace flatbuffers
{
class Parser;
}
namespace futurehead
{
class node;
class error;
namespace ipc
{
	class access;
	/** The IPC server accepts connections on one or more configured transports */
	class ipc_server final
	{
	public:
		ipc_server (futurehead::node & node, futurehead::node_rpc_config const & node_rpc_config);
		~ipc_server ();
		void stop ();

		futurehead::node & node;
		futurehead::node_rpc_config const & node_rpc_config;

		/** Unique counter/id shared across sessions */
		std::atomic<uint64_t> id_dispenser{ 1 };
		futurehead::ipc::broker & get_broker ();
		futurehead::ipc::access & get_access ();
		futurehead::error reload_access_config ();

	private:
		void setup_callbacks ();
		futurehead::ipc::broker broker;
		futurehead::ipc::access access;
		std::unique_ptr<dsock_file_remover> file_remover;
		std::vector<std::shared_ptr<futurehead::ipc::transport>> transports;
	};
}
}
