/* Timing and process priority, Win32.                            SESSION 9

   Windows has no nice values. It has a process priority class and, within
   that, seven relative thread levels. The mapping below is between the nice
   range lrzip's -N option publishes and those levels.

   Thread levels rather than the process class, for two reasons. The call sites
   want per-thread scope -- stream.c renices each compression worker as it
   starts -- and SetPriorityClass would move every thread at once. And Linux's
   nice is itself per-thread, so this keeps the two platforms behaving the same
   way rather than merely compiling the same way.

   The mapping is deliberately coarse at the ends and fine in the middle, where
   lrzip actually operates: its default is nice 19 and main.c runs the rzip
   stage at half that, so 19 and 9 must land on DIFFERENT levels or the comment
   in main.c about the main thread outranking the backend threads stops being
   true. They land on IDLE and LOWEST respectively. */

#include "lr_platform.h"
#include "lr_win32.h"

/* Ordered from most to least favourable. Each entry claims nice values from
   `floor` up to the next entry's floor, so the table is searched top-down. */
static const struct {
	int level;		/* THREAD_PRIORITY_* */
	int floor;		/* lowest nice value that maps here */
	int report;		/* nice value reported back for this level */
} lr_prio_map[] = {
	{ THREAD_PRIORITY_HIGHEST,      -20, -20 },
	{ THREAD_PRIORITY_ABOVE_NORMAL, -10, -10 },
	{ THREAD_PRIORITY_NORMAL,         0,   0 },
	{ THREAD_PRIORITY_BELOW_NORMAL,   1,   5 },
	{ THREAD_PRIORITY_LOWEST,         7,  10 },
	{ THREAD_PRIORITY_IDLE,          15,  19 },
};

#define LR_PRIO_LEVELS ((int)(sizeof(lr_prio_map) / sizeof(lr_prio_map[0])))

int lr_get_priority(void)
{
	int level = GetThreadPriority(GetCurrentThread());
	int i;

	if (level == THREAD_PRIORITY_ERROR_RETURN)
		return 0;			/* unknown; report as neutral */

	for (i = 0; i < LR_PRIO_LEVELS; i++)
		if (lr_prio_map[i].level == level)
			return lr_prio_map[i].report;

	/* TIME_CRITICAL, or a level set by something other than this code. */
	return level > THREAD_PRIORITY_NORMAL ? LR_PRIO_MIN : LR_PRIO_MAX;
}

bool lr_set_priority(int nice_val)
{
	int i, level = lr_prio_map[0].level;

	if (nice_val < LR_PRIO_MIN || nice_val > LR_PRIO_MAX)
		return false;

	/* Last entry whose floor the value has reached wins. */
	for (i = 0; i < LR_PRIO_LEVELS; i++)
		if (nice_val >= lr_prio_map[i].floor)
			level = lr_prio_map[i].level;

	return SetThreadPriority(GetCurrentThread(), level) != 0;
}
