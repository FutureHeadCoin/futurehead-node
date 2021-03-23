#pragma once

#include <futurehead/lib/rpcconfig.hpp>

#include <string>

namespace boost
{
namespace filesystem
{
	class path;
}
}

namespace futurehead
{
class tomlconfig;
class rpc_child_process_config final
{
public:
	bool enable{ false };
	std::string rpc_path{ get_default_rpc_filepath () };
};

class node_rpc_config final
{
public:
	futurehead::error serialize_json (futurehead::jsonconfig &) const;
	futurehead::error deserialize_json (bool & upgraded_a, futurehead::jsonconfig &, boost::filesystem::path const & data_path);
	futurehead::error serialize_toml (futurehead::tomlconfig & toml) const;
	futurehead::error deserialize_toml (futurehead::tomlconfig & toml);

	bool enable_sign_hash{ false };
	futurehead::rpc_child_process_config child_process;
	static unsigned json_version ()
	{
		return 1;
	}

private:
	void migrate (futurehead::jsonconfig & json, boost::filesystem::path const & data_path);
};
}
