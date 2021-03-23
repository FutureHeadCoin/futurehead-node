#pragma once

#include <futurehead/lib/errors.hpp>

namespace futurehead
{
class jsonconfig;
class tomlconfig;
class opencl_config
{
public:
	opencl_config () = default;
	opencl_config (unsigned, unsigned, unsigned);
	futurehead::error serialize_json (futurehead::jsonconfig &) const;
	futurehead::error deserialize_json (futurehead::jsonconfig &);
	futurehead::error serialize_toml (futurehead::tomlconfig &) const;
	futurehead::error deserialize_toml (futurehead::tomlconfig &);
	unsigned platform{ 0 };
	unsigned device{ 0 };
	unsigned threads{ 1024 * 1024 };
};
}
