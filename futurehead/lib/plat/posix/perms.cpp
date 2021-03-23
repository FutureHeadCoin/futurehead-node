#include <futurehead/lib/utility.hpp>

#include <boost/filesystem.hpp>

#include <sys/stat.h>
#include <sys/types.h>

void futurehead::set_umask ()
{
	umask (077);
}

void futurehead::set_secure_perm_directory (boost::filesystem::path const & path)
{
	boost::filesystem::permissions (path, boost::filesystem::owner_all);
}

void futurehead::set_secure_perm_directory (boost::filesystem::path const & path, boost::system::error_code & ec)
{
	boost::filesystem::permissions (path, boost::filesystem::owner_all, ec);
}

void futurehead::set_secure_perm_file (boost::filesystem::path const & path)
{
	boost::filesystem::permissions (path, boost::filesystem::perms::owner_read | boost::filesystem::perms::owner_write);
}

void futurehead::set_secure_perm_file (boost::filesystem::path const & path, boost::system::error_code & ec)
{
	boost::filesystem::permissions (path, boost::filesystem::perms::owner_read | boost::filesystem::perms::owner_write, ec);
}
