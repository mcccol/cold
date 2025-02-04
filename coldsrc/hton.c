#include <ansidecl.h>
#include <netinet/in.h>

#undef ntohl
#undef ntohs
#undef htonl
#undef htons
#undef __ntohl
#undef __ntohs
#undef __htonl
#undef __htons

extern unsigned long int        __htonl(unsigned long int);
extern unsigned short int       __htons(unsigned short int);

unsigned long int
__htonl(unsigned long int x)
{
  return __ntohl (x);
}

unsigned short int
__htons(unsigned short int x)
{
  return __ntohs (x);
}

#include <gnu-stabs.h>
#ifdef elf_alias
elf_alias (__htonl, __ntohl);
elf_alias (__htons, __ntohs);
#endif
#ifdef weak_alias
weak_alias (__htonl, ntohl);
weak_alias (__htons, ntohs);
weak_alias (__htonl, htonl);
weak_alias (__htons, htons);
#endif
