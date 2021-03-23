#include <futurehead/secure/working.hpp>

#include <boost/filesystem/path.hpp>

#include <shlobj.h>

namespace futurehead
{
boost::filesystem::path app_path ()
{
	boost::filesystem::path result;
	WCHAR path[MAX_PATH];
	if (SUCCEEDED (SHGetFolderPathW (NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path)))
	{
		result = boost::filesystem::path (path);
	}
	else
	{
		debug_assert (false);
	}
	return result;
}
}