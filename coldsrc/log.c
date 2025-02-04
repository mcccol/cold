/* log.c: Procedures to handle logging and fatal errors. */

#define _POSIX_SOURCE

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>
#include "config.h"
#include "log.h"
#include "dump.h"
#include "cmstring.h"
#include "util.h"

void panic(char *s)
{
    static int panic_state = 0;
    char timestr[50];
    time_t timeval;

    time(&timeval);
    strcpy(timestr, ctime(&timeval));
    timestr[strlen(timestr) - 1] = 0;

    fputs(timestr, stderr);
    fputs(panic_state ? ": RECURSIVE PANIC: " : ": PANIC: ", stderr);
    fputs(s, stderr);
    fputc('\n', stderr);

    if (!panic_state) {
	panic_state = 1;
	binary_dump();
    }
    exit(1);
}

void panic_output(void);

void abort()
{
  panic_output();
  panic("Aborted");
  exit(1);
}

void fail_to_start(char *s)
{
    char timestr[50];
    time_t timeval;

    time(&timeval);
    strcpy(timestr, ctime(&timeval));
    timestr[strlen(timestr) - 1] = 0;

    fputs(timestr, stderr);
    fputs(": FAILED TO START: ", stderr);
    fputs(s, stderr);
    fputc('\n', stderr);

    exit(1);
}

void write_log(char *fmt, ...)
{
    char timestr[50];
    time_t timeval;
    va_list arg;
    String *str;

    va_start(arg, fmt);

    pauseTimer();	/* stop logging from accumulating profile */

    time(&timeval);
    strcpy(timestr, ctime(&timeval));
    timestr[strlen(timestr) - 1] = 0;

    str = vformat(fmt, arg);

    fputs(timestr, stderr);
    fputs(": ", stderr);
    fputs(string_chars(str), stderr);
    fputc('\n', stderr);
    fflush(stderr);
    string_discard(str);

    unpauseTimer();	/* stop logging from accumulating profile */

    va_end(arg);
}

