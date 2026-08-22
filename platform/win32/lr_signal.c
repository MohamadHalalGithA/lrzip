/* Interrupt handling, Win32.                                     SESSION 8

   POSIX delivers SIGINT to the interrupted thread, which stops what it was
   doing and runs the handler. Windows does something structurally different:
   it creates a NEW thread inside the process and runs the handler there,
   while the main thread carries on working. Two consequences follow, and
   neither is a naming difference that can be papered over.

   First, the handler must be thread-safe rather than async-signal-safe, and a
   second Ctrl+C arrives on a second new thread rather than being blocked by a
   signal mask -- so re-entry has to be refused explicitly. The interlocked
   flag below does that.

   Second, cleanup now races the main thread instead of suspending it. See the
   hazard note on lr_install_interrupt_handler in lr_platform.h.

   Returning TRUE means "handled, do not run the next handler"; FALSE passes
   the event along, which for Ctrl+C ends at the default handler that kills
   the process without cleanup. */

#include "lr_platform.h"
#include "lr_win32.h"

static void (*lr_int_handler)(void);
static volatile LONG lr_int_active;

static BOOL WINAPI lr_console_ctrl_handler(DWORD type)
{
	switch (type) {
	case CTRL_C_EVENT:		/* Ctrl+C */
	case CTRL_BREAK_EVENT:		/* Ctrl+Break */
	case CTRL_CLOSE_EVENT:		/* console window closed */
	case CTRL_LOGOFF_EVENT:		/* user logging off */
	case CTRL_SHUTDOWN_EVENT:	/* system shutting down */
		break;
	default:
		return FALSE;
	}

	/* Refuse re-entry. A second Ctrl+C during cleanup arrives on another
	   thread; swallow it so the first pass can finish rather than running
	   two concurrent unlink-and-exit sequences over the same files. */
	if (InterlockedCompareExchange(&lr_int_active, 1, 0) != 0)
		return TRUE;

	if (lr_int_handler)
		lr_int_handler();

	return TRUE;
}

bool lr_install_interrupt_handler(void (*handler)(void))
{
	lr_int_handler = handler;

	/* Added on top of the CRT's own handler rather than replacing it.
	   Windows calls control handlers in reverse registration order, so this
	   one runs first and the CRT's SIGINT plumbing never sees the event. */
	return SetConsoleCtrlHandler(lr_console_ctrl_handler, TRUE) != 0;
}
