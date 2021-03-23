#include <futurehead/lib/tomlconfig.hpp>
#include <futurehead/node/node_pow_server_config.hpp>

futurehead::error futurehead::node_pow_server_config::serialize_toml (futurehead::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Value is currently not in use. Enable or disable starting Futurehead PoW Server as a child process.\ntype:bool");
	toml.put ("futurehead_pow_server_path", pow_server_path, "Value is currently not in use. Path to the futurehead_pow_server executable.\ntype:string,path");
	return toml.get_error ();
}

futurehead::error futurehead::node_pow_server_config::deserialize_toml (futurehead::tomlconfig & toml)
{
	toml.get_optional<bool> ("enable", enable);
	toml.get_optional<std::string> ("futurehead_pow_server_path", pow_server_path);

	return toml.get_error ();
}
