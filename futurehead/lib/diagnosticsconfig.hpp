#pragma once

#include <futurehead/lib/errors.hpp>

#include <chrono>

namespace futurehead
{
class jsonconfig;
class tomlconfig;
class txn_tracking_config final
{
public:
	/** If true, enable tracking for transaction read/writes held open longer than the min time variables */
	bool enable{ false };
	std::chrono::milliseconds min_read_txn_time{ 5000 };
	std::chrono::milliseconds min_write_txn_time{ 500 };
	bool ignore_writes_below_block_processor_max_time{ true };
};

/** Configuration options for diagnostics information */
class diagnostics_config final
{
public:
	futurehead::error serialize_json (futurehead::jsonconfig &) const;
	futurehead::error deserialize_json (futurehead::jsonconfig &);
	futurehead::error serialize_toml (futurehead::tomlconfig &) const;
	futurehead::error deserialize_toml (futurehead::tomlconfig &);

	txn_tracking_config txn_tracking;
};
}
