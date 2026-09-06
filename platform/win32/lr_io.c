/* Positional I/O, Win32.                                         SESSION 9

   POSIX pread/pwrite do two things at once: they transfer at an absolute
   offset, and they leave the descriptor's file position untouched. Windows
   offers the first directly and breaks the second.

   ReadFile/WriteFile take the offset in an OVERLAPPED structure, which is the
   positioned part. But on a handle opened WITHOUT FILE_FLAG_OVERLAPPED -- and
   every handle behind a CRT descriptor is such a handle -- the call is
   synchronous and updates the file pointer as a side effect. Measured, not
   assumed: with the pointer at 4, an OVERLAPPED read of 4 bytes at offset 12
   left it at 16, and the CRT's _lseek agreed.

   That side effect is not tolerable here. runzip.c preads from fd_hist while
   other code reads the same descriptor sequentially and fdopen()s it, and
   pwrites to fd_out while lseek(fd_out, 0, SEEK_CUR) is used to learn where
   the stream is. Silently moving those positions would corrupt output in a way
   that surfaces far from the cause.

   So each call saves the file pointer and restores it afterwards. The save and
   restore are not atomic against another thread using the same descriptor, but
   neither is the POSIX version's guarantee useful in that case: lrzip uses
   these on descriptors owned by one thread at a time. */

#include "lr_platform.h"
#include "lr_win32.h"
#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>

/* Windows counts a transfer in DWORD, so a single call cannot exceed 4 GiB-1.
   Callers pass at most a stream buffer, far below that, but clamping keeps a
   large count from truncating silently on the cast. */
#define LR_IO_MAX 0x7ffff000u          /* same ceiling Linux read(2) uses */

static HANDLE lr_handle_of(int fd)
{
	intptr_t h = _get_osfhandle(fd);

	if (h == -1) {
		errno = EBADF;
		return INVALID_HANDLE_VALUE;
	}
	return (HANDLE)h;
}

/* Map a Win32 error onto the errno a POSIX caller would expect. Only the cases
   these two calls can actually produce are distinguished. */
static void lr_set_errno(DWORD err)
{
	switch (err) {
	case ERROR_ACCESS_DENIED:	errno = EACCES; break;
	case ERROR_INVALID_HANDLE:	errno = EBADF;  break;
	case ERROR_NOT_ENOUGH_MEMORY:	errno = ENOMEM; break;
	case ERROR_DISK_FULL:		errno = ENOSPC; break;
	case ERROR_INVALID_PARAMETER:	errno = EINVAL; break;
	case ERROR_BROKEN_PIPE:		errno = EPIPE;  break;
	default:			errno = EIO;    break;
	}
}

/* Shared body: transfer at an absolute offset with the file position put back
   the way it was found. `writing` picks WriteFile over ReadFile. */
static ssize_t lr_pio(int fd, void *buf, size_t count, lr_i64 offset, bool writing)
{
	HANDLE h;
	OVERLAPPED ov;
	__int64 saved;
	DWORD moved = 0, want;
	BOOL ok;

	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}

	h = lr_handle_of(fd);
	if (h == INVALID_HANDLE_VALUE)
		return -1;			/* errno set by lr_handle_of */

	if (count > LR_IO_MAX)
		count = LR_IO_MAX;
	want = (DWORD)count;

	/* Remember where the descriptor was pointing. Going through the CRT
	   rather than SetFilePointerEx keeps both layers consistent in one call:
	   the raw ReadFile/WriteFile below bypasses the CRT, so its cached
	   position has to be restored too, and _lseeki64 sets both. Fails with
	   ESPIPE on a pipe, which has neither a position to preserve nor a
	   meaningful absolute offset. */
	saved = _lseeki64(fd, 0, SEEK_CUR);
	if (saved == -1)
		return -1;			/* errno set by the CRT */

	memset(&ov, 0, sizeof(ov));
	ov.Offset     = (DWORD)((uint64_t)offset & 0xffffffffu);
	ov.OffsetHigh = (DWORD)((uint64_t)offset >> 32);

	if (writing)
		ok = WriteFile(h, buf, want, &moved, &ov);
	else
		ok = ReadFile(h, buf, want, &moved, &ov);

	if (!ok) {
		DWORD err = GetLastError();

		/* Reading at or past end of file is not an error in POSIX; it
		   is a short read. Windows reports it as one. */
		if (!writing && err == ERROR_HANDLE_EOF) {
			moved = 0;
			ok = TRUE;
		} else {
			/* Put the position back even on failure, so a caller that
			   ignores the error is not left with a moved descriptor. */
			_lseeki64(fd, saved, SEEK_SET);
			lr_set_errno(err);
			return -1;
		}
	}

	/* Undo the side effect described in the header comment. */
	if (_lseeki64(fd, saved, SEEK_SET) == -1)
		return -1;			/* errno set by the CRT */

	return (ssize_t)moved;
}

ssize_t lr_pread(int fd, void *buf, size_t count, lr_i64 offset)
{
	return lr_pio(fd, buf, count, offset, false);
}

ssize_t lr_pwrite(int fd, const void *buf, size_t count, lr_i64 offset)
{
	/* Casting away const is safe: WriteFile does not modify the buffer, and
	   the const is dropped only to share one body with the read path. */
	return lr_pio(fd, (void *)buf, count, offset, true);
}

/* See lr_platform.h for the measurements behind this. */
void lr_platform_init(void)
{
	_set_fmode(_O_BINARY);

	/* _set_fmode only governs files opened afterwards; the three standard
	   streams already exist. stdin and stdout carry archive bytes whenever
	   lrzip is used in a pipeline, so both must be binary. stderr is left
	   in text mode: it carries only human-readable messages, which are
	   better off with the line endings the console expects. */
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
}
