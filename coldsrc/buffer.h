/* buffer.h: Declarations for C-- buffers. */

#ifndef BUFFER_H
#define BUFFER_H

typedef struct buffer Buffer;

#include "list.h"

struct buffer {
    int len;
    int refs;
    unsigned char s[1];
};

Buffer *buffer_new(int len);
Buffer *buffer_dup(Buffer *buf);
void buffer_discard(Buffer *buf);
Buffer *buffer_append(Buffer *buf1, Buffer *buf2);
int buffer_retrieve(Buffer *buf, int pos);
Buffer *buffer_replace(Buffer *buf, int pos, unsigned int c);
Buffer *buffer_add(Buffer *buf, unsigned int c);
Buffer *buffer_add_chars(Buffer *buf, char *s, int len);
int buffer_len(Buffer *buf);
Buffer *buffer_truncate(Buffer *buf, int len);
List *buffer_to_strings(Buffer *buf, Buffer *sep);
Buffer *buffer_from_strings(List *string_list, Buffer *sep);

#define buffer_align(b) (void *)((&(b)->refs) + 1) /* buffer content with struct alignment */
#define buffer_align_len(size) ((size) + (int)buffer_align((Buffer *)0))
#define buffer_align_new(size) (buffer_new(buffer_align_len(size)))

#endif


