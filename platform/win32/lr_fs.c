/* Filesystem queries, Win32.                   SESSION 6, SESSION 7, SESSION 9 */

#include "lr_platform.h"
#include "lr_win32.h"

/* SESSION 6
   Windows has no fstatvfs. GetDiskFreeSpaceEx is path based, so the path has
   to be recovered from the descriptor:

       fd -> HANDLE            _get_osfhandle
       HANDLE -> full path     GetFinalPathNameByHandleW
       path -> mount root      GetVolumePathNameW
       root -> free bytes      GetDiskFreeSpaceExW

   GetVolumePathNameW rather than just taking the drive letter, because a
   volume can be mounted on a directory instead of a letter, and free space
   belongs to the volume, not to the leading "C:".

   Paths are wide throughout and sized past MAX_PATH: GetFinalPathNameByHandleW
   returns the \\?\ form, which is not bounded by MAX_PATH. */
lr_i64 lr_free_space(int fd)
{
	wchar_t path[4096], root[4096];
	const DWORD path_cch = (DWORD)(sizeof(path) / sizeof(path[0]));
	const DWORD root_cch = (DWORD)(sizeof(root) / sizeof(root[0]));
	ULARGE_INTEGER avail;
	HANDLE h;
	DWORD len;

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE)
		return -1;

	len = GetFinalPathNameByHandleW(h, path, path_cch,
					FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	if (len == 0 || len >= path_cch)
		return -1;

	if (!GetVolumePathNameW(path, root, root_cch))
		return -1;

	if (!GetDiskFreeSpaceExW(root, &avail, NULL, NULL))
		return -1;

	/* Caller compares against a signed i64 file size; saturate rather than
	   wrap on a volume larger than 8 EiB. */
	if (avail.QuadPart > (ULONGLONG)INT64_MAX)
		return INT64_MAX;

	return (lr_i64)avail.QuadPart;
}

/* SESSION 7
   _commit is the CRT's fsync: it flushes the descriptor's buffers through to
   the disk. Like fsync it returns 0 on success. */
bool lr_fsync(int fd)
{
	return _commit(fd) == 0;
}

/* SESSION 7
   Windows has no POSIX permission bits, and _chmod would need a path we do not
   hold. The one honest mapping is the read-only attribute, set through the
   handle we already have.

   Only the owner-write bit survives the translation. Group and other bits have
   no Windows representation; expressing them would mean writing ACLs, which is
   a different security model, not a translation of this one.

   Requires fd to be open for writing: SetFileInformationByHandle needs
   FILE_WRITE_ATTRIBUTES, and a descriptor opened read-only fails here with
   ERROR_ACCESS_DENIED (measured, not assumed). The only caller passes the
   output file, which is opened O_WRONLY or O_RDWR. */
bool lr_set_mode(int fd, unsigned int mode)
{
	FILE_BASIC_INFO fbi;
	HANDLE h;

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE)
		return false;

	if (!GetFileInformationByHandleEx(h, FileBasicInfo, &fbi, sizeof(fbi)))
		return false;

	if (mode & 0200)	/* S_IWUSR */
		fbi.FileAttributes &= ~(DWORD)FILE_ATTRIBUTE_READONLY;
	else
		fbi.FileAttributes |= (DWORD)FILE_ATTRIBUTE_READONLY;

	/* Clearing every attribute bit is not a legal request. */
	if (fbi.FileAttributes == 0)
		fbi.FileAttributes = FILE_ATTRIBUTE_NORMAL;

	/* Zero in a time field means "leave it alone". The timestamps were just
	   read back from the file, so writing them again would be a no-op, but
	   setting LastWriteTime explicitly also stops Windows updating it
	   automatically for this handle -- and this file is still being written.
	   Preserving times is Session 9's job and belongs in lr_set_file_time. */
	fbi.CreationTime.QuadPart = 0;
	fbi.LastAccessTime.QuadPart = 0;
	fbi.LastWriteTime.QuadPart = 0;
	fbi.ChangeTime.QuadPart = 0;

	return SetFileInformationByHandle(h, FileBasicInfo, &fbi, sizeof(fbi)) != 0;
}

/* SESSION 7
   See lr_platform.h for why this reports success. Windows has no uid/gid, and
   the output file is already owned by the user running lrzip, so the caller's
   intent is satisfied and there is nothing to warn about. */
bool lr_set_owner(int fd, unsigned int uid, unsigned int gid)
{
	(void)fd;
	(void)uid;
	(void)gid;
	return true;
}

/* SESSION 9 */
bool lr_set_file_time(const char *path, time_t mtime)
{
	(void)path;
	(void)mtime;
	return false;
}
