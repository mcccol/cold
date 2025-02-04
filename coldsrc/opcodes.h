/* opcodes.h: Declarations for the opcodes table. */

#ifndef OPCODES_H
#define OPCODES_H

typedef struct op_info Op_info;

#include "ident.h"

struct op_info {
  long opcode;
  char *name;
  void (*func)(void);
  int arg1;
  int arg2;
  Ident symbol;
  unsigned long opcount;	/* for profiling purposes */
};

extern Op_info op_table[LAST_TOKEN], op_info[];

void init_op_table(void);
int find_function(char *name);

#endif

