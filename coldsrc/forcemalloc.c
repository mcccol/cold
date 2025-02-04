/* force all malloc calls through the appropriate debug routines. */
#include <stdlib.h>

typedef struct Fence {
  struct Fence *reflex;
  struct Fence *other;
  int size;
} Fence;

__ptr_t malloc(size_t __size)
{
  void *p = 
  malloc
  return GC_MALLOC(__size);
}

__ptr_t realloc(__ptr_t __ptr, size_t __size)
{
  return GC_REALLOC(__ptr, __size);
}

__ptr_t calloc(size_t __nmemb, size_t __size)
{
  return GC_MALLOC(__nmemb * __size);
}

void free(__ptr_t __ptr)
{
  return GC_FREE(__ptr);
}
