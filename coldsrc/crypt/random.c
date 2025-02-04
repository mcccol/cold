#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <signal.h>	/* For SIGINT */
#include <time.h>

void
trueRandFlush(void)
{}

void
trueRandConsume(unsigned count)
{}

int
trueRandByte(void)
{
  return rand();
}

void trueRandAccum(unsigned count)	/* Get this many random bits ready */
{
  srand(time((time_t *)0));
}

