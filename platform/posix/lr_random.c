/* Pseudorandom source, POSIX.                                    SESSION 7 */

#include "lr_platform.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

uint32_t lr_random32(void)
{
	/* The expression this replaced in rzip.c, reproduced exactly. random()
	   returns 31 bits, so one call cannot fill 32; the shift-and-xor pair is
	   upstream's construction and is kept verbatim so that Linux output
	   remains bit-identical to every previous release.

	   No seeding, also as upstream: lrzip never calls srandom(), so this is
	   glibc's fixed default sequence and hash_index[] is the same table on
	   every run. That reproducibility is a property callers rely on, not an
	   oversight to be corrected here. */
	return ((uint32_t)random() << 16) ^ (uint32_t)random();
}

/* Reads /dev/urandom, which is what util.c did inline before the port. Kept
   as a read of the device rather than getrandom(2) so the behaviour on older
   kernels is unchanged by this port. */
bool lr_secure_random(void *buf, size_t len)
{
	int fd;
	ssize_t got;

	fd = open("/dev/urandom", O_RDONLY);
	if (fd == -1)
		return false;

	got = read(fd, buf, len);
	close(fd);

	return got == (ssize_t)len;
}
