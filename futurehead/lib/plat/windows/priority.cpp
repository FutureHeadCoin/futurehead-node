#include <windows.h>
namespace futurehead
{
void work_thread_reprioritize ()
{
	SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);
}
}
