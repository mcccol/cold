/* memory.h: Function prototypes for memory management. */

#ifndef MEMORY_H
#define MEMORY_H

typedef struct pile Pile;

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

void *emalloc(size_t size);
void *erealloc(void *ptr, size_t size);
void *tmalloc(size_t size);
void tfree(void *ptr, size_t size);
void *trealloc(void *ptr, size_t oldsize, size_t newsize);
char *tstrdup(char *s);
char *tstrndup(char *s, int len);
void tfree_chars(char *s);
Pile *new_pile(void);
void *pmalloc(Pile *pile, size_t size);
void pfree(Pile *pile);

#define EMALLOC(type, num)	 ((type *) emalloc((num) * sizeof(type)))
#define EREALLOC(ptr, type, num) ((type *) erealloc(ptr, (num) * sizeof(type)))
#define TMALLOC(type, num)	 ((type *) tmalloc((num) * sizeof(type)))
#define TFREE(ptr, num)		 tfree(ptr, (num) * sizeof(*(ptr)))
#define TREALLOC(ptr, type, old, new) \
    ((type *) trealloc(ptr, (old) * sizeof(*(ptr)), (new) * sizeof(type)))
#define PMALLOC(pile, type, num) ((type *) pmalloc(pile, (num) * sizeof(type)))

#define MEMCPY(a, b, l)		memcpy(a, b, (l) * sizeof(*(a)))

#ifdef SUNOS
#define MEMMOVE(a, b, l)	bcopy(b, a, (l) * sizeof(*(a)))
#define MEMCMP(a, b, l)		bcmp(a, b, (l) * sizeof(*(a)))
#else
#define MEMMOVE(a, b, l)	memmove(a, b, (l) * sizeof(*(a)))
#define MEMCMP(a, b, l)		memcmp(a, b, (l) * sizeof(*(a)))
#endif

#endif

