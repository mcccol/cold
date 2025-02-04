/* methodop.c: Current method operations. */

#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "ident.h"
#include "data.h"
#include "memory.h"
#include "execute.h"

void op_this(void)
{
    /* Accept no arguments, and push the dbref of the current object. */
    if (!func_init_0())
	return;
    if (cur_frame->rep.type == NOT_AN_IDENT) {
      push_dbref(cur_frame->object->dbref);
    } else {
      Frob frob;
      Data d;

      /* push a frob <this(), rep> onto the stack
       */
      frob.cclass = cur_frame->object->dbref;
      frob.rep.type = cur_frame->rep.type;
      frob.rep.u = cur_frame->rep.u;
      d.type = FROB;
      d.u.frob = &frob;
      push_data(&d);
    }
}

void op_definer(void)
{
    /* Accept no arguments, and push the dbref of the method definer. */
    if (!func_init_0())
	return;
    push_dbref(cur_frame->method->object->dbref);
}

void op_sender(void)
{
    /* Accept no arguments, and push the dbref of the sending object. */
    if (!func_init_0())
	return;
    if (cur_frame->sender == NOT_AN_IDENT)
	push_int(0);
    else
	push_dbref(cur_frame->sender);
}

void op_rep(void)
{
    /* Accept no arguments, and push the dbref of the sending object. */
    if (!func_init_0())
	return;
    if (cur_frame->rep.type == NOT_AN_IDENT) {
	cthrow(frob_id, "Invocation has no representation.");
	return;
    } else {
      push_data(&cur_frame->rep);
    }
}

void op_caller(void)
{
    /* Accept no arguments, and push the dbref of the calling method's
     * definer. */
    if (!func_init_0())
	return;
    if (cur_frame->caller == NOT_AN_IDENT)
	push_int(0);
    else
	push_dbref(cur_frame->caller);
}

void op_task_id(void)
{
    /* Accept no arguments, and push the task ID. */
    if (!func_init_0())
	return;
    push_int(task_id);
}

