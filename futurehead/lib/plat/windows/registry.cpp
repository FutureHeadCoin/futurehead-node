#include <windows.h>

namespace futurehead
{
bool event_log_reg_entry_exists ()
{
	HKEY h_key;
	auto res = RegOpenKeyExW (HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Futurehead\\Futurehead", 0, KEY_READ, &h_key);
	auto found_key = (res == ERROR_SUCCESS);
	if (found_key)
	{
		RegCloseKey (h_key);
	}
	return found_key;
}
}
