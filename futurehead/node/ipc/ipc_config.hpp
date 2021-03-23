#pragma once

#include <futurehead/lib/config.hpp>
#include <futurehead/lib/errors.hpp>

#include <string>

namespace futurehead
{
class jsonconfig;
class tomlconfig;
namespace ipc
{
	/** Base class for transport configurations */
	class ipc_config_transport
	{
	public:
		virtual ~ipc_config_transport () = default;
		bool enabled{ false };
		bool allow_unsafe{ false };
		size_t io_timeout{ 15 };
		long io_threads{ -1 };
	};

	/**
	 * Flatbuffers encoding config. See TOML serialization calls for details about each field.
	 */
	class ipc_config_flatbuffers final
	{
	public:
		bool skip_unexpected_fields_in_json{ true };
		bool verify_buffers{ true };
	};

	/** Domain socket specific transport config */
	class ipc_config_domain_socket : public ipc_config_transport
	{
	public:
		/**
		 * Default domain socket path for Unix systems. Once Boost supports Windows 10 usocks,
		 * this value will be conditional on OS.
		 */
		std::string path{ "/tmp/futurehead" };

		unsigned json_version () const
		{
			return 1;
		}
	};

	/** TCP specific transport config */
	class ipc_config_tcp_socket : public ipc_config_transport
	{
	public:
		ipc_config_tcp_socket () :
		port (network_constants.default_ipc_port)
		{
		}
		futurehead::network_constants network_constants;
		/** Listening port */
		uint16_t port;
	};

	/** IPC configuration */
	class ipc_config
	{
	public:
		futurehead::error deserialize_json (bool & upgraded_a, futurehead::jsonconfig & json_a);
		futurehead::error serialize_json (futurehead::jsonconfig & json) const;
		futurehead::error deserialize_toml (futurehead::tomlconfig & toml_a);
		futurehead::error serialize_toml (futurehead::tomlconfig & toml) const;
		ipc_config_domain_socket transport_domain;
		ipc_config_tcp_socket transport_tcp;
		ipc_config_flatbuffers flatbuffers;
	};
}
}
