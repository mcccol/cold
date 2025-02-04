/* dataop.c: Function operators for miscellaneous data manipulation. */

#define _POSIX_SOURCE

#include <stdlib.h>
#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "data.h"
#include "ident.h"
#include "cache.h"
#include "util.h"

void op_type(void)
{
    Data *args;
    int type;

    /* Accept one argument of any type. */
    if (!func_init_1(&args, 0))
	return;

    /* Replace argument with symbol for type name. */
    type = args[0].type;
    pop(1);
    push_symbol(data_type_id(type));
}

void op_class(void)
{
    Data *args;
    int num_args;
    long cclass;

    /* Accept one argument of any type (only dbrefs and frobs are valid, though). */
    if (!func_init_0_or_1(&args, &num_args, 0))
	return;

    if (num_args == 0) {
      /* treat it as like a class(this()) */
      push_dbref(cur_frame->object->dbref);
    }

    if (args[0].type == DBREF) {
      cclass = args[0].u.dbref;
    } else if (args[0].type == FROB) {
      /* Replace argument with class. */
      cclass = args[0].u.frob->cclass;
    } else {
      type_error(args, dbref_id, "class can only apply to dbrefs or frobs");
      return;
    }
 
   pop(1);
   push_dbref(cclass);
}

void op_toint(void)
{
    Data *args;
    long val = 0;

    /* Accept a string or integer to convert into an integer. */
    if (!func_init_1(&args, 0))
	return;

    if (args[0].type == STRING) {
	val = atol(string_chars(args[0].u.str));
    } else if (args[0].type == DBREF) {
	val = args[0].u.dbref;
    } else {
	cthrow(type_id, "The first argument (%D) is not a dbref or string.", &args[0]);
    }
    pop(1);
    push_int(val);
}

void op_tostr(void)
{
    Data *args;
    String *str;

    /* Accept one argument of any type. */
    if (!func_init_1(&args, 0))
	return;

    /* Replace the argument with its text version. */
    str = data_tostr(&args[0]);
    pop(1);
    push_string(str);
    string_discard(str);
}

void op_toliteral(void)
{
    Data *args;
    String *str;

    /* Accept one argument of any type. */
    if (!func_init_1(&args, 0))
	return;

    /* Replace the argument with its unparsed version. */
    str = data_to_literal(&args[0]);
    pop(1);
    push_string(str);
    string_discard(str);
}

void op_todbref(void)
{
    Data *args;

    /* Accept an integer to convert into a dbref. */
    if (!func_init_1(&args, INTEGER))
	return;

    if (args[0].u.val < 0)
        cthrow(type_id, "dbrefs must be 0 or greater");

    args[0].u.dbref = args[0].u.val;
    args[0].type = DBREF;
}

void op_tosym(void)
{
    Data *args;
    long sym;

    /* Accept one string argument. */
    if (!func_init_1(&args, STRING))
	return;

    sym = ident_get(string_chars(args[0].u.str));
    pop(1);
    push_symbol(sym);
}

void op_toerr(void)
{
    Data *args;
    long error;

    /* Accept one string argument. */
    if (!func_init_1(&args, STRING))
	return;

    error = ident_get(string_chars(args[0].u.str));
    pop(1);
    push_error(error);
}

void op_valid(void)
{
    Data *args;
    int is_valid;

    /* Accept one argument of any type (only dbrefs and frobs can be valid, though). */
    if (!func_init_1(&args, 0))
	return;

    if (args[0].type == DBREF) {
      is_valid = cache_check(args[0].u.dbref);
    } else if (args[0].type == FROB) {
      is_valid = cache_check(args[0].u.frob->cclass);
    } else {
      is_valid = 0;
    }
    pop(1);
    push_int(is_valid);
}

