/* dbpack.h: Declarations for packing objects in the database. */

#ifndef DBPACK_H
#define DBPACK_H
#include <stdio.h>
#include "object.h"

typedef struct
{
  int isFile;	/* 0 implies buffer */
  int cnt;	/* used for reading buffers */
  Dict *ref;	/* used to build/hold dbref->name mapping */
  union {
    FILE *fp;
    Buffer *bp;
  } ptr;
} bFILE;

void pack_object(Object *obj, FILE *fp);
void unpack_object(Object *obj, FILE *fp);
int size_object(Object *obj);

void pack_dict(Dict *dict, bFILE *fp);
Dict *unpack_dict(bFILE *fp);

#endif

