#pragma once

#include <futurehead/lib/config.hpp>
#include <futurehead/lib/errors.hpp>
#include <memory>

namespace futurehead
{
class jsonconfig;
class tomlconfig;
class tls_config;
namespace websocket
{
	/** websocket configuration */
	class config final
	{
	public:
		config ();
		futurehead::error deserialize_json (futurehead::jsonconfig & json_a);
		futurehead::error serialize_json (futurehead::jsonconfig & json) const;
		futurehead::error deserialize_toml (futurehead::tomlconfig & toml_a);
		futurehead::error serialize_toml (futurehead::tomlconfig & toml) const;
		futurehead::network_constants network_constants;
		bool enabled{ false };
		uint16_t port;
		std::string address;
		/** Optional TLS config */
		std::shared_ptr<futurehead::tls_config> tls_config;
	};
}
}
