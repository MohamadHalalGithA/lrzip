/* Memory mapping, POSIX.                                        SESSION 10 */

#include "lr_platform.h"
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

struct lr_mapping {
	void *addr;
	size_t len;
	int fd;
	lr_i64 offset;
	lr_map_mode mode;
};

/* mmap arguments for a mode. Kept in one place so the two platforms can be
   read side by side against the same three cases. */
static void *lr_map_raw(int fd, lr_i64 offset, size_t len, lr_map_mode mode)
{
	switch (mode) {
	case LR_MAP_ANON:
		return mmap(NULL, len, PROT_READ | PROT_WRITE,
			    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	case LR_MAP_COPY:
		return mmap(NULL, len, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE, fd, (off_t)offset);
	case LR_MAP_READ:
	default:
		return mmap(NULL, len, PROT_READ, MAP_SHARED, fd, (off_t)offset);
	}
}

lr_mapping *lr_map_create(int fd, lr_i64 offset, size_t len, lr_map_mode mode)
{
	lr_mapping *m;
	void *addr;

	if (!len) {
		errno = EINVAL;
		return NULL;
	}

	addr = lr_map_raw(fd, offset, len, mode);
	if (addr == MAP_FAILED)
		return NULL;			/* errno set by mmap */

	m = calloc(1, sizeof(*m));
	if (!m) {
		munmap(addr, len);
		errno = ENOMEM;
		return NULL;
	}

	m->addr = addr;
	m->len = len;
	m->fd = fd;
	m->offset = offset;
	m->mode = mode;
	return m;
}

void *lr_map_ptr(lr_mapping *m)
{
	return m ? m->addr : NULL;
}

size_t lr_map_len(lr_mapping *m)
{
	return m ? m->len : 0;
}

bool lr_map_move(lr_mapping *m, lr_i64 offset, size_t len)
{
	void *addr;

	if (!m || m->mode == LR_MAP_ANON || !len) {
		errno = EINVAL;
		return false;
	}

	/* Map the new region before dropping the old one, so a failure leaves
	   the mapping usable rather than half torn down. The win32 side does
	   the same, which is why the interface can promise it. */
	addr = lr_map_raw(m->fd, offset, len, m->mode);
	if (addr == MAP_FAILED)
		return false;			/* errno set by mmap */

	munmap(m->addr, m->len);
	m->addr = addr;
	m->len = len;
	m->offset = offset;
	return true;
}

bool lr_map_shrink(lr_mapping *m, size_t new_len)
{
	size_t page, kept;

	if (!m || m->mode != LR_MAP_ANON || new_len > m->len) {
		errno = EINVAL;
		return false;
	}

	/* Round the kept part up to a whole number of pages and release what
	   is past it. mremap(MREMAP_MAYMOVE) would also work on Linux, but
	   unmapping the tail keeps the address stable, which is what the
	   interface promises and what mmap_stdin() relies on. */
	page = (size_t)sysconf(_SC_PAGE_SIZE);
	if (!page)
		page = 4096;
	kept = (new_len + page - 1) / page * page;

	if (kept < m->len &&
	    munmap((char *)m->addr + kept, m->len - kept))
		return false;			/* errno set by munmap */

	m->len = new_len;
	return true;
}

void lr_map_destroy(lr_mapping *m)
{
	if (!m)
		return;
	munmap(m->addr, m->len);
	free(m);
}

/* ------------------------------------------------------------ memory locking
   See lr_platform.h for why the result is advisory. */

bool lr_mem_lock(void *addr, size_t len)
{
	return mlock(addr, len) == 0;
}

bool lr_mem_unlock(void *addr, size_t len)
{
	return munlock(addr, len) == 0;
}
