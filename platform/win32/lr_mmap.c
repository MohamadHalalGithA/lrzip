/* Memory mapping, Win32.                                        SESSION 10

   Three differences from POSIX have to be absorbed here rather than at the
   call sites.

   ALIGNMENT. mmap needs a page-aligned offset (4 KiB). MapViewOfFile needs a
   multiple of dwAllocationGranularity, normally 64 KiB. rzip.c rounds its
   offsets to PAGE_SIZE, which is not enough, so the offset is rounded DOWN
   here and the caller is handed a pointer moved FORWARD by the difference.
   That difference is `delta`, and it must be remembered because UnmapViewOfFile
   wants the base the system returned, not the pointer the caller holds.

   TWO OBJECTS, NOT ONE. A POSIX mapping is just an address. Windows splits it
   into a section object (CreateFileMapping) and a view of it (MapViewOfFile),
   with separate lifetimes. The section is kept across lr_map_move() so sliding
   the window costs one view swap rather than tearing down both.

   NO ANONYMOUS FILE MAPPING. MAP_ANONYMOUS is not a file mapping at all; it is
   plain private memory, so LR_MAP_ANON uses VirtualAlloc. That also makes
   lr_map_shrink() straightforward: MEM_DECOMMIT releases the tail while the
   address stays put, which is exactly mremap's shrink behaviour and what
   mmap_stdin() needs. */

#include "lr_platform.h"
#include "lr_win32.h"
#include <stdlib.h>
#include <errno.h>
#include <io.h>

struct lr_mapping {
	HANDLE mapping;		/* section object; NULL for LR_MAP_ANON */
	void *base;		/* what Windows returned */
	void *addr;		/* base + delta -- what the caller sees */
	size_t len;		/* what the caller asked for */
	size_t delta;		/* alignment slack ahead of addr */
	int fd;
	lr_i64 offset;
	lr_map_mode mode;
};

static size_t lr_granularity(void)
{
	static size_t g;
	SYSTEM_INFO si;

	if (!g) {
		GetSystemInfo(&si);
		g = si.dwAllocationGranularity ? si.dwAllocationGranularity : 65536;
	}
	return g;
}

static size_t lr_page(void)
{
	static size_t p;
	SYSTEM_INFO si;

	if (!p) {
		GetSystemInfo(&si);
		p = si.dwPageSize ? si.dwPageSize : 4096;
	}
	return p;
}

/* rzip_fd() retries at 90% of the size when errno is ENOMEM and treats
   anything else as fatal, so resource exhaustion must be reported as ENOMEM
   for the window to shrink instead of the run aborting. */
static void lr_map_errno(DWORD err)
{
	switch (err) {
	case ERROR_NOT_ENOUGH_MEMORY:
	case ERROR_OUTOFMEMORY:
	case ERROR_COMMITMENT_LIMIT:
	case ERROR_NO_SYSTEM_RESOURCES:
		errno = ENOMEM;
		break;
	case ERROR_INVALID_HANDLE:
		errno = EBADF;
		break;
	case ERROR_ACCESS_DENIED:
		errno = EACCES;
		break;
	default:
		errno = EINVAL;
		break;
	}
}

/* Map a view of an existing section at an arbitrary byte offset, handling the
   granularity rounding. Returns the caller-visible address, or NULL. */
static void *lr_map_view(HANDLE mapping, lr_i64 offset, size_t len,
			 lr_map_mode mode, void **base_out, size_t *delta_out)
{
	size_t gran = lr_granularity();
	lr_i64 base_off;
	size_t delta;
	DWORD access;
	void *view;

	delta = (size_t)(offset % (lr_i64)gran);
	base_off = offset - (lr_i64)delta;

	access = (mode == LR_MAP_COPY) ? FILE_MAP_COPY : FILE_MAP_READ;

	view = MapViewOfFile(mapping, access,
			     (DWORD)((uint64_t)base_off >> 32),
			     (DWORD)((uint64_t)base_off & 0xffffffffu),
			     len + delta);
	if (!view) {
		lr_map_errno(GetLastError());
		return NULL;
	}

	*base_out = view;
	*delta_out = delta;
	return (char *)view + delta;
}

lr_mapping *lr_map_create(int fd, lr_i64 offset, size_t len, lr_map_mode mode)
{
	lr_mapping *m;

	if (!len || offset < 0) {
		errno = EINVAL;
		return NULL;
	}

	m = calloc(1, sizeof(*m));
	if (!m) {
		errno = ENOMEM;
		return NULL;
	}

	m->fd = fd;
	m->offset = offset;
	m->len = len;
	m->mode = mode;

	if (mode == LR_MAP_ANON) {
		m->base = VirtualAlloc(NULL, len, MEM_RESERVE | MEM_COMMIT,
				       PAGE_READWRITE);
		if (!m->base) {
			lr_map_errno(GetLastError());
			free(m);
			return NULL;
		}
		m->addr = m->base;
		return m;
	}

	{
		intptr_t raw = _get_osfhandle(fd);
		HANDLE fh;
		DWORD protect;

		if (raw == -1) {
			errno = EBADF;
			free(m);
			return NULL;
		}
		fh = (HANDLE)raw;

		protect = (mode == LR_MAP_COPY) ? PAGE_WRITECOPY : PAGE_READONLY;

		/* Maximum size 0 means "as large as the file". The section costs
		   no address space; only the view below does. */
		m->mapping = CreateFileMapping(fh, NULL, protect, 0, 0, NULL);
		if (!m->mapping) {
			lr_map_errno(GetLastError());
			free(m);
			return NULL;
		}

		m->addr = lr_map_view(m->mapping, offset, len, mode,
				      &m->base, &m->delta);
		if (!m->addr) {
			CloseHandle(m->mapping);
			free(m);
			return NULL;	/* errno set by lr_map_view */
		}
	}

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
	void *new_base = NULL, *new_addr;
	size_t new_delta = 0;

	if (!m || m->mode == LR_MAP_ANON || !len || offset < 0) {
		errno = EINVAL;
		return false;
	}

	/* Map the new view before releasing the old one, so a failure leaves
	   the caller's pointer still valid. Holding both briefly costs address
	   space, which lrzip has: it requires a 64-bit build. */
	new_addr = lr_map_view(m->mapping, offset, len, m->mode,
			       &new_base, &new_delta);
	if (!new_addr)
		return false;		/* errno set by lr_map_view */

	UnmapViewOfFile(m->base);

	m->base = new_base;
	m->addr = new_addr;
	m->delta = new_delta;
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

	page = lr_page();
	kept = (new_len + page - 1) / page * page;

	/* Decommit the tail; the reservation and the address are untouched, so
	   the caller's pointer stays valid. */
	if (kept < m->len &&
	    !VirtualFree((char *)m->base + kept, m->len - kept, MEM_DECOMMIT)) {
		lr_map_errno(GetLastError());
		return false;
	}

	m->len = new_len;
	return true;
}

void lr_map_destroy(lr_mapping *m)
{
	if (!m)
		return;

	if (m->mode == LR_MAP_ANON) {
		/* MEM_RELEASE frees the whole reservation and requires a zero
		   length, decommitted tail included. */
		VirtualFree(m->base, 0, MEM_RELEASE);
	} else {
		UnmapViewOfFile(m->base);
		CloseHandle(m->mapping);
	}
	free(m);
}

/* ------------------------------------------------------------ memory locking
   VirtualLock is the direct counterpart of mlock, with one difference worth
   naming: it charges the locked pages against the process working-set minimum
   rather than against a separate limit, so it fails with
   ERROR_WORKING_SET_QUOTA once the default minimum is used up. lrzip locks
   only a handful of small buffers -- a passphrase, a key, an IV, an AES
   context -- so the default is ample, and the call sites treat failure as
   advisory anyway. Growing the working set to guarantee success would mean
   changing a process-wide setting to protect a few hundred bytes. */

bool lr_mem_lock(void *addr, size_t len)
{
	return VirtualLock(addr, len) != 0;
}

bool lr_mem_unlock(void *addr, size_t len)
{
	return VirtualUnlock(addr, len) != 0;
}
