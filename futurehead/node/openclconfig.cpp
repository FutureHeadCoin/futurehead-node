#include <futurehead/lib/jsonconfig.hpp>
#include <futurehead/lib/tomlconfig.hpp>
#include <futurehead/node/openclconfig.hpp>

futurehead::opencl_config::opencl_config (unsigned platform_a, unsigned device_a, unsigned threads_a) :
platform (platform_a),
device (device_a),
threads (threads_a)
{
}

futurehead::error futurehead::opencl_config::serialize_json (futurehead::jsonconfig & json) const
{
	json.put ("platform", platform);
	json.put ("device", device);
	json.put ("threads", threads);
	return json.get_error ();
}

futurehead::error futurehead::opencl_config::deserialize_json (futurehead::jsonconfig & json)
{
	json.get_optional<unsigned> ("platform", platform);
	json.get_optional<unsigned> ("device", device);
	json.get_optional<unsigned> ("threads", threads);
	return json.get_error ();
}

futurehead::error futurehead::opencl_config::serialize_toml (futurehead::tomlconfig & toml) const
{
	toml.put ("platform", platform);
	toml.put ("device", device);
	toml.put ("threads", threads);

	// Add documentation
	toml.doc ("platform", "OpenCL platform identifier");
	toml.doc ("device", "OpenCL device identifier");
	toml.doc ("threads", "OpenCL thread count");

	return toml.get_error ();
}

futurehead::error futurehead::opencl_config::deserialize_toml (futurehead::tomlconfig & toml)
{
	toml.get_optional<unsigned> ("platform", platform);
	toml.get_optional<unsigned> ("device", device);
	toml.get_optional<unsigned> ("threads", threads);
	return toml.get_error ();
}
