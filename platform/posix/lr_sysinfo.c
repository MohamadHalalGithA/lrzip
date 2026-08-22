/* System information queries, POSIX.                             SESSION 7 */

#include "lr_platform.h"

lr_i64 lr_physical_ram(void)
{
	long pages = sysconf(_SC_PHYS_PAGES);
	long page_size = sysconf(_SC_PAGE_SIZE);

	if (pages <= 0 || page_size <= 0)
		return -1;

	/* Cast before multiplying: both are long, which is 32-bit on some
	   targets, and the product is a byte count that overflows it. */
	return (lr_i64)pages * (lr_i64)page_size;
}

int lr_cpu_count(void)
{
	long cpus = sysconf(_SC_NPROCESSORS_ONLN);

	if (cpus < 1)
		return 1;

	return (int)cpus;
}
