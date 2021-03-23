#include <futurehead/lib/config.hpp>
#include <futurehead/secure/utility.hpp>
#include <futurehead/secure/working.hpp>

#include <boost/filesystem.hpp>

static std::vector<boost::filesystem::path> all_unique_paths;

boost::filesystem::path futurehead::working_path (bool legacy)
{
	static futurehead::network_constants network_constants;
	auto result (futurehead::app_path ());
	switch (network_constants.network ())
	{
		case futurehead::futurehead_networks::futurehead_test_network:
			if (!legacy)
			{
				result /= "FutureheadTest";
			}
			else
			{
				result /= "RaiBlocksTest";
			}
			break;
		case futurehead::futurehead_networks::futurehead_beta_network:
			if (!legacy)
			{
				result /= "FutureheadBeta";
			}
			else
			{
				result /= "RaiBlocksBeta";
			}
			break;
		case futurehead::futurehead_networks::futurehead_live_network:
			if (!legacy)
			{
				result /= "Futurehead";
			}
			else
			{
				result /= "RaiBlocks";
			}
			break;
	}
	return result;
}

bool futurehead::migrate_working_path (std::string & error_string)
{
	bool result (true);
	auto old_path (futurehead::working_path (true));
	auto new_path (futurehead::working_path ());

	if (old_path != new_path)
	{
		boost::system::error_code status_error;

		auto old_path_status (boost::filesystem::status (old_path, status_error));
		if (status_error == boost::system::errc::success && boost::filesystem::exists (old_path_status) && boost::filesystem::is_directory (old_path_status))
		{
			auto new_path_status (boost::filesystem::status (new_path, status_error));
			if (!boost::filesystem::exists (new_path_status))
			{
				boost::system::error_code rename_error;

				boost::filesystem::rename (old_path, new_path, rename_error);
				if (rename_error != boost::system::errc::success)
				{
					std::stringstream error_string_stream;

					error_string_stream << "Unable to migrate data from " << old_path << " to " << new_path;

					error_string = error_string_stream.str ();

					result = false;
				}
			}
		}
	}

	return result;
}

boost::filesystem::path futurehead::unique_path ()
{
	auto result (working_path () / boost::filesystem::unique_path ());
	all_unique_paths.push_back (result);
	return result;
}

void futurehead::remove_temporary_directories ()
{
	for (auto & path : all_unique_paths)
	{
		boost::system::error_code ec;
		boost::filesystem::remove_all (path, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary directory: " << ec.message () << std::endl;
		}

		// lmdb creates a -lock suffixed file for its MDB_NOSUBDIR databases
		auto lockfile = path;
		lockfile += "-lock";
		boost::filesystem::remove (lockfile, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary lock file: " << ec.message () << std::endl;
		}
	}
}

namespace futurehead
{
/** A wrapper for handling signals */
std::function<void()> signal_handler_impl;
void signal_handler (int sig)
{
	if (signal_handler_impl != nullptr)
	{
		signal_handler_impl ();
	}
}
}
