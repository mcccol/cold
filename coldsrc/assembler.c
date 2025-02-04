/* assemble.c: Disassembler. */

#define _POSIX_SOURCE

#include <ctype.h>
#include "config.h"
#include "operator.h"
#include "x.tab.h"
#include "object.h"
#include "data.h"
#include "memory.h"
#include "opcodes.h"
#include "cache.h"
#include "io.h"
#include "ident.h"
#include "cmstring.h"
#include "decode.h"
#include "util.h"
#include "log.h"
#include "execute.h"

List *disassemble(Method *method, Object *object)
{
  List *output = list_new(0);

  int pc = 0;

  while (pc < method->num_opcodes) {
    int opcode;
    Op_info *info;
    int arg;
    List *op = list_new(0);
    Data d;

    /* PC */
    d.type = INTEGER;
    d.u.val = pc;
    op = list_add(op, &d);

    /* opcode */
    opcode = method->opcodes[pc++];

    d.type = STRING;
    d.u.str =
      string_from_chars(op_table[opcode].name,
			strlen(op_table[opcode].name)
	);
    op = list_add(op, &d);
    string_discard(d.u.str);

    /* arg(s) */
    info = &op_table[opcode];
    for (arg = 0; arg < 2; arg++) {
      Data d;
      int arg_type = (arg == 0) ? info->arg1 : info->arg2;
      if (arg_type) {
	switch (arg_type) {
	case INTEGER:
	  d.type = INTEGER;
	  d.u.val = method->opcodes[pc++];
	  break;

	case STRING:
	  d.type = STRING;
	  d.u.str = object_get_string(object, method->opcodes[pc++]);
	  break;

	case ERROR:
	  d.type = ERROR;
	  d.u.error = object_get_ident(object, method->opcodes[pc++]);
	  break;

	case VAR:
	case IDENT:
	  d.type = SYMBOL;
	  d.u.symbol = object_get_ident(object, method->opcodes[pc++]);
	  break;

	case JUMP:
	  d.type = INTEGER;
	  d.u.val = method->opcodes[pc++];
	  break;
	}
	op = list_add(op, &d);
      } else {
	d.type = LIST;
	d.u.list = op;
	output = list_add(output, &d);
	list_discard(op);
	break;
      }
    }
  }

  return output;
}

static List *disassemble_method(Object *object, long name)
{
  Method *method;

  method = object_find_method(object->dbref, name);
  return (method) ? disassemble(method, method->object) : NULL;
}

void op_disassemble(void)
{
  Data *args;
  List *code;

  /* Accept a symbol for the method name */
  if (!func_init_1(&args, SYMBOL))
    return;

  code = disassemble_method(cur_frame->object, args[0].u.symbol);

  if (code) {
    pop(1);
    push_list(code);
    list_discard(code);
  } else {
    cthrow(methodnf_id,
	   "Method %s not found.",
	   ident_name(args[0].u.symbol));
  }
}

extern int debugging;
void op_debug(void)
{
  Data *args;

  /* Accept a symbol for the method name */
  if (!func_init_1(&args, INTEGER))
    return;

  if (!check_perms()) {
    debugging = args[0].u.val;
    /*    push_int(debugging); */
  }
}
