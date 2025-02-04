/* profiling - some simple event timers for Linux */
#include <sys/time.h>

#ifdef NPROFILE
void startTimer()
{
}

double stopTimer()
{
  return 0;
}

void pauseTimer()
{
}

void unpauseTimer()
{
}
#else
/* time in seconds of last interval */
double timer;

/* values used by interval timer */
static struct itimerval start = {{0,0}, {10000, 0}}; /* some large value - we don't want signals */
static struct itimerval zero = {{0,0},{0,0}};
static struct itimerval current = {{0,0},{0,0}};;

void startTimer()
{
  struct itimerval start;
  setitimer(ITIMER_PROF, &start, (void*)0);	/* start the timer with a large value */
}

double stopTimer()
{
  setitimer(ITIMER_PROF, &zero, &current);	/* stop the timer */
  timer = (double)(start.it_value.tv_sec - current.it_value.tv_sec) + (double)(1000000 - current.it_value.tv_usec) / 1000000;
  return timer;
}

void pauseTimer()
{
  setitimer(ITIMER_PROF, &zero, &current);	/* pause the timer */
}

void unpauseTimer()
{
  setitimer(ITIMER_PROF, &current, (void*)0);	/* restart the timer */
}
#endif
