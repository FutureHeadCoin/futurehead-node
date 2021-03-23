#pragma once

#include <futurehead/lib/errors.hpp>
#include <futurehead/lib/numbers.hpp>

#include <string>

namespace futurehead
{
class tomlconfig;

/** Configuration options for the Qt wallet */
class wallet_config final
{
public:
	wallet_config ();
	/** Update this instance by parsing the given wallet and account */
	futurehead::error parse (std::string const & wallet_a, std::string const & account_a);
	futurehead::error serialize_toml (futurehead::tomlconfig & toml_a) const;
	futurehead::error deserialize_toml (futurehead::tomlconfig & toml_a);
	futurehead::wallet_id wallet;
	futurehead::account account{ 0 };
};
}
