/* Positional I/O, POSIX.                                         SESSION 9 */

#include "lr_platform.h"
#include <unistd.h>

/* Thin wrappers: pread/pwrite are exactly this interface. off_t is 64-bit
   because configure.ac calls AC_SYS_LARGEFILE, which defines
   _FILE_OFFSET_BITS 64 in config.h -- included from lr_platform.h above, so
   the definition is in scope before <unistd.h> is read. */

ssize_t lr_pread(int fd, void *buf, size_t count, lr_i64 offset)
{
	return pread(fd, buf, count, (off_t)offset);
}

ssize_t lr_pwrite(int fd, const void *buf, size_t count, lr_i64 offset)
{
	return pwrite(fd, buf, count, (off_t)offset);
}

/* Nothing to do: POSIX has no text mode, so a file is already what it says.
   The function exists so main() can call it unconditionally. */
void lr_platform_init(void)
{
}
