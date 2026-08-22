/* System information queries, Win32.                             SESSION 7 */

#include "lr_platform.h"
#include "lr_win32.h"

lr_i64 lr_physical_ram(void)
{
	MEMORYSTATUSEX ms;

	/* GlobalMemoryStatusEx requires the caller to declare the struct size;
	   the field distinguishes this from the older MEMORYSTATUS. */
	ms.dwLength = sizeof(ms);

	if (!GlobalMemoryStatusEx(&ms))
		return -1;

	/* ullTotalPhys, not ullAvailPhys: the POSIX side reports _SC_PHYS_PAGES,
	   which is installed RAM rather than free RAM. lrzip's window heuristics
	   expect the total and apply their own headroom. */

	/* ullTotalPhys is unsigned 64-bit and the return type is signed, so a
	   machine with more than 8 EiB would wrap into the -1 error sentinel. */
	if (ms.ullTotalPhys > (ULONGLONG)INT64_MAX)
		return INT64_MAX;

	return (lr_i64)ms.ullTotalPhys;
}

int lr_cpu_count(void)
{
	DWORD count;
	SYSTEM_INFO si;

	/* GetActiveProcessorCount over GetSystemInfo: dwNumberOfProcessors only
	   reports the calling thread's processor group, so it saturates at 64 on
	   machines with more. lrzip scales its worker threads with this number,
	   so on a large server the difference is real throughput. */
	count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
	if (count > 0)
		return (int)count;

	GetSystemInfo(&si);
	if (si.dwNumberOfProcessors > 0)
		return (int)si.dwNumberOfProcessors;

	return 1;
}
