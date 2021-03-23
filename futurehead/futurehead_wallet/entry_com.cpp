#include <futurehead/lib/errors.hpp>
#include <futurehead/lib/utility.hpp>
#include <futurehead/node/cli.hpp>
#include <futurehead/rpc/rpc.hpp>
#include <futurehead/secure/utility.hpp>
#include <futurehead/secure/working.hpp>

#include <boost/format.hpp>
#include <boost/make_shared.hpp>
#include <boost/program_options.hpp>

int main (int argc, char * const * argv)
{
	futurehead::set_umask ();
	try
	{
		boost::program_options::options_description description ("Command line options");
		description.add_options () ("help", "Print out options");
		futurehead::add_node_options (description);
		boost::program_options::variables_map vm;
		boost::program_options::store (boost::program_options::command_line_parser (argc, argv).options (description).allow_unregistered ().run (), vm);
		boost::program_options::notify (vm);
		int result (0);

		if (!vm.count ("data_path"))
		{
			std::string error_string;
			if (!futurehead::migrate_working_path (error_string))
			{
				throw std::runtime_error (error_string);
			}
		}

		auto ec = futurehead::handle_node_options (vm);
		if (ec == futurehead::error_cli::unknown_command && vm.count ("help") != 0)
		{
			std::cout << description << std::endl;
		}
		return result;
	}
	catch (std::exception const & e)
	{
		std::cerr << boost::str (boost::format ("Exception while initializing %1%") % e.what ());
	}
	catch (...)
	{
		std::cerr << boost::str (boost::format ("Unknown exception while initializing"));
	}
	return 1;
}
