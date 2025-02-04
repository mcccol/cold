#include "usuals.h"

int cryptRandOpen(void);
void cryptRandWash(byte const key[16]);
byte cryptRandByte(void);
void cryptRandSave(byte const key[16], byte const iv[8]);
void cryptRandCreate(void);

unsigned trueRandEvent(int event);
void trueRandFlush(void);
void trueRandConsume(unsigned count);
void trueRandAccumLater(unsigned bitcount);
void trueRandAccum(unsigned count);
int trueRandByte(void);

int getstring(char *strbuf, unsigned maxlen, int echo);
