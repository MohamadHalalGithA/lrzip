/*
   Platform abstraction layer for lrzip.

   This is the only platform header application code includes. It is pulled in
   first from lrzip_private.h, which every application translation unit reaches
   directly or transitively, so it is the single point where the OS is decided.

   Two jobs:

     1. Include the system headers the application needs, per platform. This is
        why application code no longer includes <termios.h>, <sys/statvfs.h> and
        friends itself -- those includes moved here.

     2. Declare the lr_* interface. Interfaces are shaped by what callers need,
        not by what POSIX happens to expose: lr_free_space() returns one integer
        rather than reproducing struct statvfs, and lr_map_resize() is named for
        the caller's intent because Windows cannot offer mremap's mechanism.

   One implementation of each function exists per platform, in platform/posix/
   and platform/win32/. The filenames are parallel, so the build selects a
   directory rather than a file list.
*/

#ifndef LR_PLATFORM_H
#define LR_PLATFORM_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <sys/types.h>

/* The application's own i64 lives in lrzip_private.h, which includes this
   header before defining it. The interface therefore needs its own name. */
typedef int64_t lr_i64;

#if defined(_WIN32)
# define LR_PLATFORM_WIN32 1
# include "lr_platform_win32.h"
#else
# define LR_PLATFORM_POSIX 1
# include "lr_platform_posix.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ console
   Toggle terminal echo around the passphrase prompt. Both return false if
   stdin is not a terminal, which callers may ignore -- there is no echo to
   suppress when input is a pipe or a file.                      SESSION 6 */
bool lr_echo_disable(void);
bool lr_echo_enable(void);

/* --------------------------------------------------------------- filesystem
   Bytes free to this user on the filesystem holding fd; -1 on error.

   Takes a descriptor rather than a path because both call sites hold an
   already-open output file and used fstatvfs(). Windows has no fstatvfs and
   its free-space call is path based, so the win32 side recovers a path from
   the handle -- the awkwardness belongs here, not at the call sites.
                                                                  SESSION 6 */
lr_i64 lr_free_space(int fd);

/* No lr_set_file_time() here, though Session 5 planned one and Session 9 was
   to implement it. UCRT64 supplies <utime.h> and a working utime(), so
   lrzip.c's preserve_times() needs no help: the call compiles and behaves as
   it does on Linux. An lr_* wrapper would have been indirection with one
   implementation. Same reasoning retired the planned lr_time_ms() below.
   Under the settled MinGW-only scope there is no second implementation to
   abstract over; if that scope ever widens, these come back.     SESSION 9 */

/* Flush a descriptor's written data to the storage device.       SESSION 7 */
bool lr_fsync(int fd);

/* Apply the permission bits of a POSIX mode to an open file.

   Windows has no POSIX permission model. The only bit it can honour is the
   read-only attribute, which the owner-write bit maps onto; the group and
   other bits have no representation and are discarded.

   fd must be open for writing. The win32 side sets the attribute through the
   handle, which needs FILE_WRITE_ATTRIBUTES access; a read-only descriptor
   fails with ERROR_ACCESS_DENIED. Both call sites pass the output file, which
   is opened O_WRONLY or O_RDWR, so this costs nothing -- but it is a real
   precondition and callers should not assume otherwise.           SESSION 7 */
bool lr_set_mode(int fd, unsigned int mode);

/* Apply a POSIX owner to an open file.

   Returns true on Windows without doing anything, and that is the honest
   answer rather than a papered-over failure: Windows has no uid/gid, and a
   file created by this process is already owned by the user running it, so
   the caller's goal -- output owned like the input -- already holds. Returning
   false would print a warning on every single run for a non-problem.
                                                                  SESSION 7 */
bool lr_set_owner(int fd, unsigned int uid, unsigned int gid);

/* -------------------------------------------------------- system information
   Total physical RAM in bytes, or -1 if it cannot be determined.

   This is not a convenience query. lrzip sizes its compression window from
   available RAM -- handling files larger than memory is the reason the program
   exists -- so a wrong answer here silently degrades every compression, and a
   too-large answer drives the machine into swap.                 SESSION 7 */
lr_i64 lr_physical_ram(void);

/* Online CPUs usable by this process. Never returns less than 1, so callers
   can use the result directly as a thread count.                  SESSION 7 */
int lr_cpu_count(void);

/* No timing section: UCRT64 supplies <sys/time.h> and gettimeofday(), which is
   what main.c, rzip.c and runzip.c call and all they need. See the note beside
   lr_fsync above.                                                 SESSION 9 */

/* -------------------------------------------------------------- pseudorandom
   A full-width 32-bit value for seeding rzip's rolling-hash index table.

   Deliberately 32 bits rather than a random() clone. lrzip never seeds the
   generator, so on glibc hash_index[] is a fixed table, identical on every run
   and every machine, and compressed output is reproducible. Two properties
   must therefore survive the port: every one of the 32 bits must vary, and the
   sequence must stay deterministic. Windows' rand() satisfies neither -- UCRT
   caps RAND_MAX at 32767, so the upstream expression built from two rand()
   calls would leave bits 15 and 31 permanently zero and weaken the hash that
   drives match-finding.                                           SESSION 7 */
uint32_t lr_random32(void);

/* --------------------------------------------------------------- scheduling
   Priority expressed as a POSIX nice value: LR_PRIO_MIN is the most
   favourable, LR_PRIO_MAX the least. lrzip's -N option is documented in these
   terms and defaults to 19, so the range is part of the user interface and
   cannot be replaced with a Windows priority class without changing what -N
   means.

   The scope is the CALLING THREAD, not the process. That is what the call
   sites need -- stream.c renices each worker as it starts, expecting to affect
   only that worker -- and it is what Linux already does, where nice is a
   per-thread attribute despite PRIO_PROCESS being the name of the flag.

   Windows has no nice values. The backend maps the range onto the seven thread
   priority levels, which is lossy in one direction only: distinct nice values
   can land on the same level, so lr_get_priority() returns a representative
   value for the current level rather than necessarily the one last set. Only
   lrzip's warning message reads it back, so that is enough.

   lr_set_priority() returns false if the level could not be applied; callers
   fall back to leaving priority alone.                           SESSION 9 */
#define LR_PRIO_MIN (-20)
#define LR_PRIO_MAX 19

int  lr_get_priority(void);
bool lr_set_priority(int nice_val);

/* ----------------------------------------------------------- positional I/O
   Read/write at an absolute offset without moving the file pointer.
                                                                  SESSION 9 */
ssize_t lr_pread(int fd, void *buf, size_t count, lr_i64 offset);
ssize_t lr_pwrite(int fd, const void *buf, size_t count, lr_i64 offset);

/* --------------------------------------------------------------- interrupts
   Install a handler for interrupt-style events: Ctrl+C and a termination
   request on both platforms, plus console-close, logoff and shutdown on
   Windows, which has no SIGTERM.

   The handler takes no argument because the caller never used the signal
   number for anything but re-arming, and re-arming is done here instead --
   which signals exist is platform knowledge. SIGTTIN and SIGTTOU have no
   Windows counterpart at all.

   Semantic difference that must not be papered over: on POSIX the handler runs
   on the interrupted thread in signal context, so only async-signal-safe calls
   are legal. On Windows it runs on a separate OS-created thread, so it must be
   thread-safe instead, and repeat events arrive on further new threads rather
   than being held off by a signal mask. The win32 backend refuses re-entry
   explicitly for that reason.

   HAZARD, inherited rather than introduced: lrzip's handler does not set a
   flag and return. It unlinks files, prints, flushes and calls exit(). On
   POSIX that is already outside what a signal handler may safely do; on
   Windows it additionally races the main thread, which keeps running. Making
   this genuinely safe means a flag-and-poll shutdown across lrzip's worker
   loops, which is a redesign rather than a port.        SESSION 8 */
bool lr_install_interrupt_handler(void (*handler)(void));

/* ----------------------------------------------------------- memory mapping
   lr_mapping is opaque. This is the decision most expensive to get wrong: the
   win32 implementation must retain a HANDLE and an alignment delta that POSIX
   has no use for, and a bare void * return would have nowhere to keep them.

   lr_map_resize() replaces mremap(). Callers MUST re-fetch the pointer with
   lr_map_ptr() afterwards -- on Windows the region is unmapped and remapped,
   so the address moves.                                         SESSION 10 */
typedef struct lr_mapping lr_mapping;

lr_mapping *lr_map_create(int fd, lr_i64 offset, size_t len, bool writable);
void       *lr_map_ptr(lr_mapping *m);
size_t      lr_map_len(lr_mapping *m);
bool        lr_map_resize(lr_mapping *m, size_t new_len);
void        lr_map_destroy(lr_mapping *m);

#ifdef __cplusplus
}
#endif

#endif /* LR_PLATFORM_H */
