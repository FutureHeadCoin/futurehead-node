#include <futurehead/lib/memory.hpp>

namespace
{
#ifdef __APPLE__
/** TSAN on mac is generating some warnings. They need further investigating before using memory pools can be used, so disable them for now */
bool use_memory_pools{ false };
#else
bool use_memory_pools{ true };
#endif
}

bool futurehead::get_use_memory_pools ()
{
	return use_memory_pools;
}

/** This has no effect on Mac */
void futurehead::set_use_memory_pools (bool use_memory_pools_a)
{
#ifndef __APPLE__
	use_memory_pools = use_memory_pools_a;
#endif
}

futurehead::cleanup_guard::cleanup_guard (std::vector<std::function<void()>> const & cleanup_funcs_a) :
cleanup_funcs (cleanup_funcs_a)
{
}

futurehead::cleanup_guard::~cleanup_guard ()
{
	for (auto & func : cleanup_funcs)
	{
		func ();
	}
}
