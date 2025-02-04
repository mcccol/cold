/* config.h: Compile-time values administrator may want to change. */

#ifndef CONFIG_H
#define CONFIG_H

/* Uses vfork() instead of fork() in adminop.c. */
/* #define BSD_FEATURES */

/* The version number. */
#define VERSION_MAJOR	0
#define VERSION_MINOR	12
#define VERSION_BUGFIX	0

/* Number of ticks a method gets before dying with an E_TICKS. */
#define METHOD_TICKS		20000

/* Number of ticks a paused method gets before dying with an E_TICKS */
#define PAUSED_METHOD_TICKS     5000

/* Maximum depth of method calls. */
#define MAX_CALL_DEPTH		128

/* Width and depth of object cache. (7 and 23 are defaults) */
#define CACHE_WIDTH	15
#define CACHE_DEPTH	30

/* Default indent for decompiled code. */
#define DEFAULT_INDENT	4

/* Maximum number of characters of a data value to display using format(). */
#define MAX_DATA_DISPLAY 15

#define SYSTEM_DBREF	0
#define ROOT_DBREF	1


void startTimer();	/* start an interval timer */
double stopTimer();	/* stop itimer returning elapsed */
void pauseTimer();	/* pause the timer until unpause */
void unpauseTimer();	/* pause the timer until unpause */

extern int debugging;
#define DEB_PROFILE 1
#define DEB_CALL 2
#define DEB_ERROR 4
#define DEB_MESSAGE 8
#define DEB_OPCODE 16
#define DEB_VAR 32
#define DEB_STACK 64

#ifdef MEMSPY
#include "memspy/memspy.h"
#endif

#ifdef DMALLOC
#include <dmalloc.h>
#endif

#endif

