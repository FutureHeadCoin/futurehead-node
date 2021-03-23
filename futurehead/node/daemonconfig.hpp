#pragma once

#include <futurehead/lib/errors.hpp>
#include <futurehead/node/node_pow_server_config.hpp>
#include <futurehead/node/node_rpc_config.hpp>
#include <futurehead/node/nodeconfig.hpp>
#include <futurehead/node/openclconfig.hpp>

#include <vector>

namespace futurehead
{
class jsonconfig;
class tomlconfig;
class daemon_config
{
public:
	daemon_config () = default;
	daemon_config (boost::filesystem::path const & data_path);
	futurehead::error deserialize_json (bool &, futurehead::jsonconfig &);
	futurehead::error serialize_json (futurehead::jsonconfig &);
	futurehead::error deserialize_toml (futurehead::tomlconfig &);
	futurehead::error serialize_toml (futurehead::tomlconfig &);
	bool rpc_enable{ false };
	futurehead::node_rpc_config rpc;
	futurehead::node_config node;
	bool opencl_enable{ false };
	futurehead::opencl_config opencl;
	futurehead::node_pow_server_config pow_server;
	boost::filesystem::path data_path;
	unsigned json_version () const
	{
		return 2;
	}
};

futurehead::error read_node_config_toml (boost::filesystem::path const &, futurehead::daemon_config & config_a, std::vector<std::string> const & config_overrides = std::vector<std::string> ());
futurehead::error read_and_update_daemon_config (boost::filesystem::path const &, futurehead::daemon_config & config_a, futurehead::jsonconfig & json_a);
}
