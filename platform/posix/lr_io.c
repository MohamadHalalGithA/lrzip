/* Timing and process priority, POSIX.                            SESSION 9 */

#include "lr_platform.h"
#include <sys/resource.h>
#include <errno.h>

/* PRIO_PROCESS is a misnomer on Linux, where nice is a per-thread attribute
   and this therefore affects only the calling thread -- which is what the
   header promises and what stream.c relies on. */

int lr_get_priority(void)
{
	int prio;

	/* getpriority legitimately returns -1, so errno is the only way to tell
	   a real failure from a valid nice value of -1. */
	errno = 0;
	prio = getpriority(PRIO_PROCESS, 0);
	if (prio == -1 && errno != 0)
		return 0;			/* unknown; report as neutral */

	return prio;
}

bool lr_set_priority(int nice_val)
{
	if (nice_val < LR_PRIO_MIN || nice_val > LR_PRIO_MAX)
		return false;

	return setpriority(PRIO_PROCESS, 0, nice_val) == 0;
}
