/* Interrupt handling, POSIX.                                     SESSION 8 */

#include "lr_platform.h"
#include <signal.h>

static void (*lr_int_handler)(void);

/* Suppressing further signals is done here rather than in the application's
   handler because which signals exist is platform knowledge: SIGTTIN and
   SIGTTOU are the terminal stop signals for a backgrounded process reading or
   writing the tty, and they have no Windows counterpart.

   The set below reproduces exactly what lrzip's handler did inline before the
   port -- the delivered signal, SIGTERM, SIGTTIN and SIGTTOU. Note that SIGINT
   is not in that list, so a SIGTERM followed by a SIGINT can still re-enter
   during cleanup. That gap is upstream's and is left as found rather than
   quietly changed by a Windows port. */
static void lr_signal_trampoline(int sig)
{
	signal(sig, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);

	if (lr_int_handler)
		lr_int_handler();
}

bool lr_install_interrupt_handler(void (*handler)(void))
{
	struct sigaction sa;

	lr_int_handler = handler;

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = lr_signal_trampoline;

	if (sigaction(SIGTERM, &sa, NULL))
		return false;
	if (sigaction(SIGINT, &sa, NULL))
		return false;

	return true;
}
