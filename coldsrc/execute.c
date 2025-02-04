/* execute.c: Routines for executing C-- tasks. */

#define _POSIX_SOURCE

#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>
#include "x.tab.h"
#include "execute.h"
#include "memory.h"
#include "config.h"
#include "ident.h"
#include "io.h"
#include "object.h"
#include "cache.h"
#include "util.h"
#include "opcodes.h"
#include "cmstring.h"
#include "log.h"
#include "decode.h"
#include "lookup.h"

#define STACK_STARTING_SIZE		(256 - STACK_MALLOC_DELTA)
#define ARG_STACK_STARTING_SIZE		(32 - ARG_STACK_MALLOC_DELTA)

extern int running;

static void execute(void);
static void out_of_ticks_error(void);

static Frame *frame_store = NULL;
static int frame_depth;
static int last_argpos;
String *numargs_str;

Frame *cur_frame, *suspend_frame;
Connection *cur_conn;
Data *stack;
int stack_pos, stack_size;
int *arg_starts, arg_pos, arg_size;
long task_id;
long tick;
int opcode_restart = 0;

VMState *tasks = NULL, *paused = NULL, *vmstore = NULL;
VMStack *stack_store = NULL, *holder_store = NULL; 

int debugging = 0;

void debug_output(void)
{
  int pc = cur_frame->pc;
  write_log("%f: object:$%d method:$%d.%I pc:%d/%d opcode:%s stack: %d arg: %d",
	    cur_frame->profile, 
	    cur_frame->object->dbref, 
	    cur_frame->method->object->dbref,
	    (cur_frame->method->name != NOT_AN_IDENT)
	      ? cur_frame->method->name
	      : opcode_id,
	    line_number(cur_frame->method, cur_frame->pc),
	    cur_frame->pc,
	    op_table[cur_frame->opcodes[pc]].name,
	    stack_pos,
	    arg_pos
    );
  assert(stack_pos >= cur_frame->stack_start);
  assert(arg_pos >= cur_frame->argpos_start);
}

void panic_output(void)
{
  cur_frame->pc--;
  debug_output();
}

void pop(int n)
{
  assert(n >= 0);
  while (n--) {
    if (debugging & DEB_STACK) {
      write_log("pop(%d) %d -> %D", n, stack_pos, &stack[stack_pos - 1]);
    }
    assert(data_refs(&stack[stack_pos-1]));
    assert(stack_pos > 0);
    data_discard(&stack[--stack_pos]);
  }
}

void check_stack(int n)
{
  Data *olds;
    while (stack_pos + n > stack_size) {
	stack_size = stack_size * 2 + STACK_MALLOC_DELTA;
	olds = stack;
	stack = EREALLOC(stack, Data, stack_size);
    }
}

void push_data(Data *d)
{
  if (debugging & DEB_STACK)
    write_log("push(%D) %d", d, stack_pos);

  check_stack(1);
  data_dup(stack + stack_pos++, d);
}

void push_int(long n)
{
  if (debugging & DEB_STACK)
    write_log("push(%d) %d", n, stack_pos);

  check_stack(1);
  stack[stack_pos].type = INTEGER;
  stack[stack_pos].u.val = n;
  stack_pos++;
}

void push_string(String *str)
{
  if (debugging & DEB_STACK)
    write_log("push(\"%S\") %d", str, stack_pos);

  check_stack(1);
  assert(str->refs > 0);
  stack[stack_pos].type = STRING;
  stack[stack_pos].u.str = string_dup(str);
  stack_pos++;
}

void push_dbref(Dbref dbref)
{
  if (debugging & DEB_STACK)
    write_log("push($%d) %d", dbref, stack_pos);

  check_stack(1);
  stack[stack_pos].type = DBREF;
  stack[stack_pos].u.dbref = dbref;
  stack_pos++;
}

void push_list(List *list)
{
  if (debugging & DEB_STACK) {
    String *str = string_new(0);
    str = data_add_list_literal_to_str(str, list);
    write_log("push(%S) %d", str, stack_pos);
    string_discard(str);
  }

  check_stack(1);
  assert(list->refs > 0);
  stack[stack_pos].type = LIST;
  stack[stack_pos].u.list = list_dup(list);
  assert(data_refs(&stack[stack_pos]));
  stack_pos++;
}

void push_dict(Dict *dict)
{
  if (debugging & DEB_STACK) {
    String *str = string_new(0);
    str = dict_add_literal_to_str(str, dict);
    write_log("push(%S) %d", str, stack_pos);
    string_discard(str);
  }

  check_stack(1);
  assert(dict->refs > 0);
  stack[stack_pos].type = DICT;
  stack[stack_pos].u.dict = dict_dup(dict);
  assert(data_refs(&stack[stack_pos]));
  stack_pos++;
}

void push_symbol(Ident id)
{
  if (debugging & DEB_STACK)
    write_log("push(\'%s) %d", ident_name(id), stack_pos);

  check_stack(1);
  stack[stack_pos].type = SYMBOL;
  stack[stack_pos].u.symbol = ident_dup(id);
  stack_pos++;
}

void push_error(Ident id)
{
  if (debugging & DEB_STACK)
    write_log("push(~%s) %d", ident_name(id), stack_pos);

  check_stack(1);
  stack[stack_pos].type = ERROR;
  stack[stack_pos].u.error = ident_dup(id);
  stack_pos++;
}

void push_buffer(Buffer *buf)
{
  if (debugging & DEB_STACK)
    write_log("push() buffer %d", stack_pos);

  check_stack(1);
  assert(buf->refs > 0);
  stack[stack_pos].type = BUFFER;
  stack[stack_pos].u.buffer = buffer_dup(buf);
  stack_pos++;
}

int pop_args()
{
#ifndef NDEBUG
  int n;
#endif
  if (debugging & DEB_STACK) {
    write_log("pop_args:  arg_starts[%d] -> %d",
	      arg_pos - 1, arg_starts[arg_pos-1]);
  }

  /* check the args' refcounts. */
#ifndef NDEBUG
  for (n = stack_pos - arg_starts[arg_pos - 1];n;n--) {
    assert(data_refs(&stack[stack_pos-n]));
  }
#endif

  return arg_starts[--arg_pos];
}


void store_stack(void) {
  VMStack *holder;

  if (holder_store) {
    holder = holder_store;
    holder_store = holder_store->next;
/*    write_log("store_stack:  reusing holder %d", holder); */
  } else {
    holder = EMALLOC(VMStack, 1);
/*    write_log("store_stack:  allocating holder %d", holder); */
  }
  
  holder->stack = stack;
  holder->stack_size = stack_size;

  holder->arg_starts = arg_starts;
  holder->arg_size = arg_size;

  holder->next = stack_store;
  stack_store = holder;
}

VMState *suspend_vm(void) {
  VMState *vm;

  if (vmstore) {
    vm = vmstore;
    vmstore = vmstore->next;
/*    write_log("suspend_vm:  reusing vm %d", vm); */
  } else {
    vm = EMALLOC(VMState, 1);
/*    write_log("suspend_vm:  allocating vm %d", vm); */
  }

  vm->paused = 0;
  vm->cur_frame = cur_frame;
  vm->cur_conn = cur_conn;
  vm->stack = stack;
  vm->stack_pos = stack_pos;
  vm->stack_size = stack_size;
  vm->arg_starts = arg_starts;
  vm->arg_pos = arg_pos;
  vm->arg_size = arg_size;
  vm->task_id = task_id;
  vm->next = NULL;

  return vm;
}

void restore_vm(VMState *vm) {
  task_id = vm->task_id;
  cur_frame = vm->cur_frame;
  cur_conn = vm->cur_conn;
  stack = vm->stack;
  stack_pos = vm->stack_pos;
  stack_size = vm->stack_size;
  arg_starts = vm->arg_starts;
  arg_pos = vm->arg_pos;
  arg_size = vm->arg_size;

  if (debugging & DEB_CALL) {
    write_log("restore_vm: tid %d", vm->task_id);
    debug_output();
  }
}

#define ADD_TO_LIST(the_list, the_value) \
   if (!the_list) { \
     the_list = the_value; \
     the_value->next = NULL; \
   } else { \
     the_value->next = the_list; \
     the_list = the_value; \
   }

#define REMOVE_FROM_LIST(the_list, the_value) {   \
   if (the_list == the_value) \
     the_list = the_list->next; \
   else task_delete(the_list, the_value); \
}

void task_delete(VMState *list, VMState *elem) {
  list = list->next;

  while (list && (list->next != elem))
    list = list->next;
  if (list)
    list->next = elem->next;
}

VMState *task_lookup(long tid) {
  VMState *vm;

  for (vm = tasks;  vm;  vm = vm->next)
    if (vm->task_id == tid)
      return vm;

  for (vm = paused;  vm;  vm = vm->next)
    if (vm->task_id == tid)
      return vm;

  return NULL;
}

/* we assume tid is a non-paused task */
void task_resume(long tid, Data *ret) {
  VMState *vm = task_lookup(tid), *old_vm;

  old_vm = suspend_vm();
  restore_vm(vm);
  REMOVE_FROM_LIST(tasks, vm);
  ADD_TO_LIST(vmstore, vm);
  cur_frame->ticks = METHOD_TICKS;

  if (ret) {
    push_data(ret);
  } else
    push_int(0);
  execute();
  store_stack();
  restore_vm(old_vm);
  ADD_TO_LIST(vmstore, old_vm);
}

void task_suspend(void) {
  VMState *vm = suspend_vm();

  ADD_TO_LIST(tasks, vm);
  init_execute();
  cur_frame = NULL;
}

void task_cancel(long tid) {
  VMState *vm = task_lookup(tid), *old_vm;

  old_vm = suspend_vm();
  restore_vm(vm);
  while (cur_frame)
    frame_return();
  if (vm->paused) {
    REMOVE_FROM_LIST(paused, vm);
  } else {
    REMOVE_FROM_LIST(tasks, vm);
  }
  store_stack();
  ADD_TO_LIST(vmstore, vm);
  restore_vm(old_vm);
  ADD_TO_LIST(vmstore, old_vm);
}

void task_pause(void) {
  VMState *vm = suspend_vm();

  vm->paused = 1;
  ADD_TO_LIST(paused, vm);
  init_execute();
  cur_frame = NULL;  
}

void run_paused_tasks(void) {
  VMState *vm = suspend_vm(), *task = paused, *last_task;

  /* don't want any task adding itself while we're running the paused tasks */
  paused = NULL;
  while (task) {
    restore_vm(task);
    cur_frame->ticks = PAUSED_METHOD_TICKS;
    last_task = task;
    task = task->next;
    ADD_TO_LIST(vmstore, last_task);
    execute();
    store_stack();
  }
  restore_vm(vm);
  ADD_TO_LIST(vmstore, vm);
/*  for (vm = paused; vm; vm = vm->next)
    write_log("paused task tid %d", vm->task_id);  */
}

List *task_list(void) {
  List *r;
  Data elem;
  VMState *vm = tasks;

  r = list_new(0);

  elem.type = INTEGER;
  for (; vm; vm = vm->next) {
    elem.u.val = vm->task_id;
    list_add(r, &elem); 
  }
  
  vm = paused;
  for (; vm; vm = vm->next) {
    elem.u.val = vm->task_id;
    list_add(r, &elem); 
  }

  return r;
}

List *task_callers(void) {
  List *r;
  Data elem, *d;
  Frame *f;

  r = list_new(0);
  elem.type = LIST;
  for (f = cur_frame; f; f = f->caller_frame) {
    elem.u.list = list_new(4);
    d = list_empty_spaces(elem.u.list, 4);
    d->type = DBREF;
    d->u.dbref = f->object->dbref;
    d++;
    d->type = DBREF;
    d->u.dbref = f->method->object->dbref;
    d++;
    d->type = SYMBOL;
    d->u.symbol = f->method->name;
    d++;
    d->type = INTEGER;
    d->u.val = f->pc; /* line_number(f->method, f->pc - 1); */
    r = list_add(r, &elem);
  }
  return r;
}

#undef ADD_TO_LIST
#undef REMOVE_FROM_LIST

void init_execute()
{
  if (stack_store) {
    VMStack *holder;
    
    stack = stack_store->stack;
    stack_size = stack_store->stack_size;

    arg_starts = stack_store->arg_starts;
    arg_size = stack_store->arg_size;

    holder = stack_store;
    stack_store = holder->next;
    holder->next = holder_store;
    holder_store = holder;
/*    write_log("resuing execution state"); */
  } else {
    stack = EMALLOC(Data, STACK_STARTING_SIZE);
    stack_size = STACK_STARTING_SIZE;

    arg_starts = EMALLOC(int, ARG_STACK_STARTING_SIZE);
    arg_size = ARG_STACK_STARTING_SIZE;
/*    write_log("allocating execution state"); */
  }
  stack_pos = 0;
  arg_pos = 0;
  opcode_restart = 0;
}

/* Execute a task by sending a message to an object. */
long task(Connection *conn, Dbref dbref, long message, int num_args, ...)
{
    va_list arg;
    Ident result;

    /* Don't execute if a shutdown() has occured. */
    if (!running) {
	va_end(arg);
	return disconnect_id;
    }

    /* Set global variables. */
    cur_conn = conn;
    frame_depth = 0;

    va_start(arg, num_args);
    check_stack(num_args);
    while (num_args--)
	push_data(va_arg(arg, Data *));
    va_end(arg);

    /* Send the message.  If this is succesful, start the task by calling
     * execute(). */
    ident_dup(message);
    result = send_message(dbref, message, (Data*)0, 0, 0);
    if (result == NOT_AN_IDENT) {

      execute();
      if (stack_pos != 0)
	panic("Stack not empty after interpretation.");
      task_id++;

    } else {
      pop(stack_pos);
    }
    ident_discard(message);
    return result;
}

/* Execute a task by evaluating a method on an object. */
void task_method(Connection *conn, Object *obj, Method *method)
{
    cur_conn = conn;
    frame_start(obj, method, NOT_AN_IDENT, (Data*)0, NOT_AN_IDENT, 0, 0, 0);
    execute();

    if (stack_pos != 0)
	panic("Stack not empty after interpretation.");
}

long frame_start(Object *obj, Method *method, Dbref sender, Data *rep, Dbref caller,
		 int stack_start, int arg_start, int arg_pos)
{
    Frame *frame;
    int i, num_args, num_rest_args;
    List *rest;
    Data *d;
    Number_buf nbuf1, nbuf2;

    if (debugging & DEB_CALL)
      debug_output();

    num_args = stack_pos - arg_start;
    if (num_args < method->num_args || (num_args > method->num_args &&
					method->rest == NOT_AN_IDENT)) {
	if (numargs_str)
	    string_discard(numargs_str);
	numargs_str = format("#%l.%s called with %s argument%s, requires %s%s",
			     obj->dbref, ident_name(method->name),
			     english_integer(num_args, nbuf1),
			     (num_args == 1) ? "" : "s",
			     (method->num_args == 0) ? "none" :
			     english_integer(method->num_args, nbuf2),
			     (method->rest == NOT_AN_IDENT) ? "." : " or more.");
	return numargs_id;
    }

    if (frame_depth > MAX_CALL_DEPTH)
	return maxdepth_id;
    frame_depth++;

    if (method->rest != NOT_AN_IDENT) {
	/* Make a list for the remaining arguments. */
	num_rest_args = stack_pos - (arg_start + method->num_args);
	rest = list_new(num_rest_args);

	/* Move aforementioned remaining arguments into the list. */
	d = list_empty_spaces(rest, num_rest_args);
	MEMCPY(d, &stack[stack_pos - num_rest_args], num_rest_args);
	stack_pos -= num_rest_args;

	/* Push the list onto the stack. */
	push_list(rest);
	list_discard(rest);
    }

    if (frame_store) {
	frame = frame_store;
	frame_store = frame_store->caller_frame;
    } else {
	frame = EMALLOC(Frame, 1);
    }

    frame->object = cache_grab(obj);
    frame->sender = sender;
    if (rep && (rep->type != NOT_AN_IDENT)) {
      data_dup(&frame->rep, rep);
    } else {
      frame->rep.type = NOT_AN_IDENT;
    }
    frame->caller = caller;
    frame->method = method_grab(method);
    cache_grab(method->object);
    frame->opcodes = method->opcodes;
    frame->pc = 0;
    frame->ticks = METHOD_TICKS;
    frame->profile = 0.0;

    frame->specifiers = NULL;
    frame->handler_info = NULL;

    /* Set up stack indices. */
    frame->stack_start = stack_start;
    frame->var_start = arg_start;
    frame->argpos_start = arg_pos;

    /* Initialize local variables to 0. */
    check_stack(method->num_vars);
    for (i = 0; i < method->num_vars; i++) {
	stack[stack_pos + i].type = INTEGER;
	stack[stack_pos + i].u.val = 0;
    }
    stack_pos += method->num_vars;

    frame->caller_frame = cur_frame;
    cur_frame = frame;
    opcode_restart = 0;

    return NOT_AN_IDENT;
}

void pop_error_action_specifier()
{ 
    Error_action_specifier *old;

    /* Pop the first error action specifier off that stack. */
    old = cur_frame->specifiers;
    if (old) {
      cur_frame->specifiers = old->next;
      free(old);
    }
    if (debugging & DEB_ERROR) {
      write_log("Top Error action handler: %d", cur_frame->specifiers);
    }
}

void pop_handler_info()
{
    Handler_info *old;

    /* Free the data in the first handler info specifier, and pop it off that
     * stack. */
    old = cur_frame->handler_info;
    if (old) {
      list_discard(old->traceback);
      ident_discard(old->error);
      cur_frame->handler_info = old->next;
      free(old);
    }
}

void frame_return(void)
{
    int i;
    Frame *caller_frame = cur_frame->caller_frame;

    if (debugging & DEB_CALL) {
      debug_output();
    }

    /* Free old data on stack. */
    for (i = cur_frame->stack_start; i < stack_pos; i++) {
      if (debugging & DEB_STACK) {
	write_log("Discarding stack[%d] %D", i, &stack[i]);
      }
      data_discard(&stack[i]);
    }
    stack_pos = cur_frame->stack_start;

    /* Let go of method and objects. */
    cache_discard(cur_frame->object);
    cache_discard(cur_frame->method->object);
    method_discard(cur_frame->method); /* CMC: move this to the end - some methods suicude! */
    if (cur_frame->rep.type != NOT_AN_IDENT) {
      data_discard(&cur_frame->rep);
    }

    /* Discard any error action specifiers. */
    while (cur_frame->specifiers)
	pop_error_action_specifier();

    /* Discard any handler information. */
    while (cur_frame->handler_info)
	pop_handler_info();

    /* Append frame to frame store for later reuse. */
    cur_frame->caller_frame = frame_store;
    frame_store = cur_frame;

    arg_pos = cur_frame->argpos_start;

    if (debugging & DEB_STACK) {
      write_log("frame_return: arg_pos=%d cur_frame->argpos_start=%d", 
		arg_pos, cur_frame->argpos_start);
    }
    /* Return to the caller frame. */
    cur_frame = caller_frame;

    frame_depth--;
}

static void execute(void)
{
    int opcode;

    while (cur_frame) {
       tick++;
	if (!--(cur_frame->ticks)) {
	    out_of_ticks_error();
	} else {
	    opcode = cur_frame->opcodes[cur_frame->pc];

	    if (debugging & DEB_OPCODE)
	      debug_output();
	    if (debugging & DEB_PROFILE)
	      startTimer();

	    last_argpos = arg_pos;	/* for opcode restart */
	    cur_frame->last_pc = cur_frame->pc;
	    cur_frame->pc++;
	    if (opcode_restart) {
	      (*op_table[opcode].func)();
	      opcode_restart = 0;
	    } else {
	      (*op_table[opcode].func)();
	    }
	    if (debugging & DEB_PROFILE)
	      cur_frame->profile += stopTimer();
	}
    }
}

/* Requires cur_frame->pc to be the current instruction.  Do NOT call this
 * function if there is any possibility of the assignment failing before the
 * current instruction finishes. */
void anticipate_assignment(void)
{
    int opcode, ind;
    long id;
    Data *dp, d;

    opcode = cur_frame->opcodes[cur_frame->pc];
    if (opcode == SET_LOCAL) {
	/* Zero out local variable value. */
	dp = &stack[cur_frame->var_start +
		    cur_frame->opcodes[cur_frame->pc + 1]];
	data_discard(dp);
	dp->type = INTEGER;
	dp->u.val = 0;
    } else if (opcode == SET_OBJ_VAR) {
	/* Zero out the object variable, if it exists. */
	ind = cur_frame->opcodes[cur_frame->pc + 1];
	id = object_get_ident(cur_frame->method->object, ind);
	d.type = INTEGER;
	d.u.val = 0;
	object_assign_var(cur_frame->object, cur_frame->method->object,
			  id, &d);
    }
}

Ident pass_message(int stack_start, int arg_start)
{
    Method *method;
    Ident result;

    if (cur_frame->method->name == NOT_AN_IDENT)
	return methodnf_id;

    /* Find the next method to handle the message. */
    method = object_find_next_method(cur_frame->object->dbref,
				     cur_frame->method->name,
				     cur_frame->method->object->dbref);
    if (!method)
	return methodnf_id;

    /* Start the new frame. */
    result = frame_start(cur_frame->object,
			 method,
			 cur_frame->sender,
			 &cur_frame->rep,
			 cur_frame->caller,
			 stack_start,
			 arg_start,
			 arg_pos);
    cache_discard(method->object);
    return result;
}

Ident send_message(Dbref dbref, Ident message, Data *rep, int stack_start, int arg_start)
{
    Object *obj;
    Method *method;
    Ident result;
    Dbref sender, caller;

    /* Get the target object from the cache. */
    obj = cache_retrieve(dbref);
    if (!obj)
	return objnf_id;

    /* Find the method to run. */
    method = object_find_method(obj->dbref, message);
    if (!method) {
	cache_discard(obj);
	return methodnf_id;
    }

    /* Start the new frame. */
    sender = (cur_frame) ? cur_frame->object->dbref : NOT_AN_IDENT;
    caller = (cur_frame) ? cur_frame->method->object->dbref : NOT_AN_IDENT;
    result = frame_start(obj, method, sender, rep, caller,
			 stack_start, arg_start, arg_pos);

    cache_discard(obj);
    cache_discard(method->object);

    return result;
}

static void fill_in_method_info(Data *d)
{
    Ident method_name;

    /* The method name, or 0 for eval. */
    method_name = cur_frame->method->name;
    if (method_name == NOT_AN_IDENT) {
	d->type = INTEGER;
	d->u.val = 0;
    } else {
	d->type = SYMBOL;
	d->u.val = ident_dup(method_name);
    }
    d++;

    /* The current object. */
    d->type = DBREF;
    d->u.dbref = cur_frame->object->dbref;
    d++;

    /* The defining object. */
    d->type = DBREF;
    d->u.dbref = cur_frame->method->object->dbref;
    d++;

    /* The PC. */
    d->type = INTEGER;
    d->u.val = cur_frame->pc;

    /* The line number. */
/*    d->u.val = line_number(cur_frame->method, cur_frame->pc); */
}

static List *traceback_add(List *traceback, Ident error)
{
    List *frame;
    Data *d, frame_data;

    /* Construct a list giving information about this stack frame. */
    frame = list_new(5);
    d = list_empty_spaces(frame, 5);

    /* First element is the error code. */
    d->type = ERROR;
    d->u.error = ident_dup(error);
    d++;

    /* Second through fifth elements are the current method info. */
    fill_in_method_info(d);

    /* Add the frame to the list. */
    frame_data.type = LIST;
    frame_data.u.list = frame;
    traceback = list_add(traceback, &frame_data);
    list_discard(frame);

    return traceback;
}

/* Requires:	traceback is a list of strings containing the traceback
 *			information to date.  THIS FUNCTION CONSUMES TRACEBACK.
 *		id is an error id.  This function accounts for an error id
 *			which is "owned" by a data stack frame that we will
 *			nuke in the course of unwinding the call stack.
 *		str is a string containing an explanation of the error. */
void propagate_error(List *traceback, Ident error)
{
    int i, ind, propagate = 0;
    Error_action_specifier *spec;
    Error_list *errors;
    Handler_info *hinfo;

    /* If there's no current frame, drop all this on the floor. */
    if (!cur_frame) {
	list_discard(traceback);
	return;
    }

    if (debugging & DEB_ERROR) {
      write_log("propagate_error start %D", traceback);
    }

    /* Add message to traceback. */
    if (traceback) {
      /* DBREF errors from op_return don't need traceback */
      traceback = traceback_add(traceback, error);
    }

    /* Look for an appropriate specifier in this frame. */
    for (; (spec = cur_frame->specifiers); pop_error_action_specifier()) {

	switch (spec->type) {

	case DBREF:
	  /* an object error-handler was called and threw an error */
	  if (debugging & DEB_ERROR) {
	    write_log("propagate_error DBREF %d object:$%d method:$%d.%I pc:%d/%d opcode:%s stack: %d arg: %d",
		      error, 
		      cur_frame->object->dbref, 
		      cur_frame->method->object->dbref,
		      (cur_frame->method->name != NOT_AN_IDENT)
		      ? cur_frame->method->name
		      : opcode_id,
		      line_number(cur_frame->method, cur_frame->pc),
		      cur_frame->pc,
		      op_table[cur_frame->opcodes[cur_frame->pc]].name,
		      stack_pos,
		      arg_pos
		      );
	  }


	  /* Nuke the stack back to where we were when calling the object error-handler */
	  pop(stack_pos - spec->stack_pos);

	  /* Pop this error spec, discard the traceback */
#ifdef 0
	  /* CMC - one too many was popped */
	  pop_error_action_specifier();
#endif
	  if (traceback) {
	    list_discard(traceback);
	  }

	  /* grab handler information describing the original error*/
	  traceback = list_dup(cur_frame->handler_info->traceback);
	  error = ident_dup(cur_frame->handler_info->error);
	  /* Add message to traceback. */
	  if (traceback) {
	    /* DBREF errors from op_return don't need traceback */
	    traceback = traceback_add(traceback, error);
	  }
	  pop_handler_info();
	  
	  /* continue processing the original error */
	  propagate = 1;
	  break;

	  case CRITICAL:

	    /* We're in a critical expression.  Make a copy of the error,
	     * since it may currently be living in the region of the stack
	     * we're about to nuke. */
	    error = ident_dup(error);

	    /* Nuke the stack back to where we were at the beginning of the
	     * critical expression. */
	    pop(stack_pos - spec->stack_pos);

	    /* Jump to the end of the critical expression. */
	    cur_frame->pc = spec->u.critical.end;

	    /* Push the error on the stack, and discard our copy of it. */
	    push_error(error);
	    ident_discard(error);

	    /* Pop this error spec, discard the traceback, and continue
	     * processing. */
	    pop_error_action_specifier();
	    list_discard(traceback);
	    if (debugging & DEB_ERROR) {
	      write_log("propagate_error CRITICAL %d object:$%d method:$%d.%I pc:%d/%d opcode:%s stack: %d arg: %d",
			error, 
			cur_frame->object->dbref, 
			cur_frame->method->object->dbref,
			(cur_frame->method->name != NOT_AN_IDENT)
			? cur_frame->method->name
			: opcode_id,
			line_number(cur_frame->method, cur_frame->pc),
			cur_frame->pc,
			op_table[cur_frame->opcodes[cur_frame->pc]].name,
			stack_pos,
			arg_pos
		);
	    }
	    return;

	  case PROPAGATE:

	    /* We're in a propagate expression.  Set the propagate flag and
	     * keep going. */
	    propagate = 1;
	    break;

	  case CATCH:

	    /* We're in a catch statement.  Get the error list index. */
	    ind = spec->u.ccatch.error_list;

	    /* If the index is -1, this was a 'catch any' statement.
	     * Otherwise, check if this error code is in the error list. */
	    if (spec->u.ccatch.error_list != NOT_AN_IDENT) {
		errors = &cur_frame->method->error_lists[ind];
		for (i = 0; i < errors->num_errors; i++) {
		    if (errors->error_ids[i] == error)
			break;
		}

		/* Keep going if we didn't find the error. */
		if (i == errors->num_errors)
		    break;
	    }

	    /* We catch this error.  Make a handler info structure and push it
	     * onto the stack. */
	    hinfo = EMALLOC(Handler_info, 1);
	    hinfo->traceback = traceback;
	    hinfo->error = ident_dup(error);
	    hinfo->next = cur_frame->handler_info;
	    cur_frame->handler_info = hinfo;

	    /* Pop the stack down to where we were at the beginning of the
	     * catch statement.  This may nuke our copy of error, but we don't
	     * need it any more. */
	    pop(stack_pos - spec->stack_pos);

	    /* Jump to the handler expression, pop this specifier, and continue
	     * processing. */
	    cur_frame->pc = spec->u.ccatch.handler;
	    pop_error_action_specifier();
	    if (debugging & DEB_ERROR) {
	      write_log("propagate_error CATCH %d object:$%d method:$%d.%I pc:%d/%d opcode:%s stack: %d arg: %d",
			error, 
			cur_frame->object->dbref, 
			cur_frame->method->object->dbref,
			(cur_frame->method->name != NOT_AN_IDENT)
			? cur_frame->method->name
			: opcode_id,
			line_number(cur_frame->method, cur_frame->pc),
			cur_frame->pc,
			op_table[cur_frame->opcodes[cur_frame->pc]].name,
			stack_pos,
			arg_pos
		);
	    }
	    return;
	default:
	  panic("Undefined error handler from stack");
	}
	if (debugging & DEB_ERROR) {
	  write_log("propagate_error - seeking specifier in current frame");
	}
    }

    /* There was no handler in the current frame. */
    if (debugging & DEB_ERROR) {
      write_log("propagate_error - pop frame");
    }

    frame_return();
    propagate_error(traceback, (propagate) ? error : methoderr_id);

    if (debugging & DEB_ERROR) {
      write_log("propagate_error - complete");
    }
}

/* construct an error traceback from components */
static List *construct_traceback(Ident error, String *explanation, Data *arg, List *location)
{
    List *error_condition, *traceback;
    Data *d;

    /* Construct a three-element list for the error condition. */
    error_condition = list_new(3);
    d = list_empty_spaces(error_condition, 3);

    /* The first element is the error code. */
    d->type = ERROR;
    d->u.error = ident_dup(error);
    d++;

    /* The second element is the explanation string. */
    d->type = STRING;
    d->u.str = string_dup(explanation);
    d++;

    /* The third element is the error arg, or 0 if there is none. */
    if (arg) {
	data_dup(d, arg);
    } else {
	d->type = INTEGER;
	d->u.val = 0;
    }

    /* Now construct a traceback, starting as a two-element list. */
    traceback = list_new(2);
    d = list_empty_spaces(traceback, 2);

    /* The first element is the error condition. */
    d->type = LIST;
    d->u.list = error_condition;
    d++;

    /* The second argument is the location. */
    d->type = LIST;
    d->u.list = list_dup(location);

    /* return traceback to be consumed by propagate_error. */
    list_discard(location);
    return traceback;
}

static List *locate_error()
{
  List *location;
  Data *d;
  Ident location_type;
  char *opname;

  /* Get the opcode name and decide whether it's a function or not. */
  opname = op_table[cur_frame->opcodes[cur_frame->last_pc]].name;
  location_type = (islower(*opname)) ? function_id : opcode_id;

  /* Construct a two-element list giving the location. */
  location = list_new(2);
  d = list_empty_spaces(location, 2);

  /* The first element is 'function or 'opcode. */
  d->type = SYMBOL;
  d->u.symbol = ident_dup(location_type);
  d++;

  /* The second element is the symbol for the opcode. */
  d->type = SYMBOL;
  d->u.symbol = ident_dup(op_table[cur_frame->opcodes[cur_frame->last_pc]].symbol);
  return location;
}

/* propagate an interpreter error after determining location */
static void interp_error(Ident error, String *explanation, Data *arg)
{
    /* Start the error propagating.  This consumes traceback. */
    propagate_error(construct_traceback(error, explanation, arg, locate_error()), error);
}

void cthrowstr(long error, String *explanation)
{
    interp_error(error, explanation, NULL);
}

void cthrow(Ident error, char *fmt, ...)
{
    String *str;
    va_list arg;

    va_start(arg, fmt);
    str = vformat(fmt, arg);
    va_end(arg);

    interp_error(error, str, NULL);
    string_discard(str);
}

/* throw an interpreter error with associated traceback data */
void cthrowdata(Ident error, Data *data, char *fmt, ...)
{
    String *str;
    va_list arg;

    va_start(arg, fmt);
    str = vformat(fmt, arg);
    va_end(arg);

    interp_error(error, str, data);
    string_discard(str);
}

/* throw an interpreter error with a string traceback data */
void cthrowchar(Ident error, char *data, char *fmt, ...)
{
  String *str;
  Data d;
  va_list arg;

  d.type = STRING;
  d.u.str = string_new(strlen(data));
  d.u.str = string_add_chars(d.u.str, data, strlen(data));

  va_start(arg, fmt);
  str = vformat(fmt, arg);
  va_end(arg);

  interp_error(error, str, &d);

  string_discard(str);
  string_discard(d.u.str);
}

void op_start_args(void);
int op_expr_message(void);

/* call an object error-handler with the traceback given
 * 
 * This leaves a DBREF-type error specifier in the current frame
 * which will be cleared upon successful return by the handler,
 * or will be interpreted by any error propagation which sees it as a signal to give up.
 */
static void err_handler(Data *target, Data *obj, Data *arg, Ident suberror, Ident error, String *str)
{
  List *traceback;
  Error_action_specifier *spec;
  Handler_info *hinfo;
  Dbref dbref;
  Object *tobj;
  Method *method;
  int num_args, failed;

  if (debugging & DEB_ERROR) {
    write_log("err_handler(%D,%D,%D,%I,%I,%S) object:$%d method:$%d.%I pc:%d/%d opcode:%s stack: %d arg: %d",
	      target,
	      obj,
	      arg,
	      suberror,
	      error,
	      str,
	      cur_frame->object->dbref, 
	      cur_frame->method->object->dbref,
	      (cur_frame->method->name != NOT_AN_IDENT)
	      ? cur_frame->method->name
	      : opcode_id,
	      line_number(cur_frame->method, cur_frame->pc),
	      cur_frame->pc,
	      op_table[cur_frame->opcodes[cur_frame->pc]].name,
	      stack_pos,
	      arg_pos
	      );
  }

  /* construct a traceback */
  traceback = construct_traceback(error, str, arg, locate_error());
  string_discard(str);

  /* get the dbref of the handler */
  switch (target->type) {
  case DBREF:
    dbref = target->u.dbref;
    num_args = 3;
    break;

  case  FROB:
    dbref = target->u.frob->cclass;
    num_args = 3;
    break;

  default:
    /* anonymous type */
    if (!lookup_retrieve_name(data_type_id(target->type), &dbref)) {
      dbref = -1;
    }
    num_args = 3;
    break;
  }

  /* ensure op_expr_message can't fail
     find the handler object and ensure it has the 'error method
     */
  if ((frame_depth <= MAX_CALL_DEPTH) && (tobj = cache_retrieve(dbref))) {
    if ((method = object_find_method(dbref, catch_id))) {
      if (method->num_args == num_args
	  || (method->num_args < num_args && method->rest != NOT_AN_IDENT)) {
	/* construct a DBREF error specifier */
	spec = EMALLOC(Error_action_specifier, 1);
	spec->type = DBREF;
	spec->stack_pos = stack_pos;
	spec->u.obj.result = obj;	/* what is the intervention to affect? */
	spec->next = cur_frame->specifiers;
	cur_frame->specifiers = spec;
	if (debugging & DEB_ERROR) {
	  write_log("DBREF handler.  next: %d", spec->next);
	}

	/* record the current error information in case the intervention fails */
	hinfo = EMALLOC(Handler_info, 1);
	hinfo->traceback = list_dup(traceback);
	hinfo->error = ident_dup(error);
	hinfo->next = cur_frame->handler_info;
	cur_frame->handler_info = hinfo;

	/* The last opcode executed failed.
	 * We plan to restart it with the result of obj.error(traceback()), unless +it+ throws.
	 * To that end, we restore the stack and pc, permitting the handler's stack frame to
	 * restart with the single modified argument, and everything else already in place.
	 */
	cur_frame->pc = cur_frame->last_pc; /* prepare to restart the opcode after intervention */
	arg_pos = last_argpos;		/* undo any pop_args */
	if (debugging & DEB_ERROR) {
	  write_log("handling %D with $%d.error", traceback, dbref);
	}
	/* set up a calling frame to send an obj.error() message */
	push_data(target);		/* push the offending datum's target */
	push_symbol(catch_id);		/* push the 'error symbol */
	
	op_start_args();			/* construct a stack frame for our intervention */

	push_error(suberror);		/* push the suberror */
	push_list(traceback);		/* push the traceback */
	push_data(obj);			/* what caused the error - what's to be modified */

	/* start the intervention */
	if ((failed = op_expr_message())) {
	  debug_output();
	  write_log("op_expr_message returned %d", failed);
	  panic("Error intervention failed - can't happen");
	}

	/* clean up extra refs */
	/* method_discard(method); */
	cache_discard(tobj);
	return;
      } else {
	/* no such method */
	cache_discard(tobj);
      }
    }
  }

  /* intervention couldn't be made - just rethrow the error */
  propagate_error(traceback, error);
}

void obj_handler(Object *handler, Data *data, Ident error, char *fmt, ...)
{
  String *str;
  Data target;
  va_list arg;

  va_start(arg, fmt);
  str = vformat(fmt, arg);
  va_end(arg);

  target.type = DBREF;
  target.u.dbref = handler->dbref;
  err_handler(&target, data, NULL, error, error, str);
}

void data_obj_handler(Data *target, Data *data, Ident error, char *fmt, ...)
{
  String *str;
  va_list arg;

  va_start(arg, fmt);
  str = vformat(fmt, arg);
  va_end(arg);

  err_handler(target, data, NULL, error, error, str);
}

void data_handler(Data *obj, Ident error, char *fmt, ...)
{
  String *str;
  va_list arg;

  va_start(arg, fmt);
  str = vformat(fmt, arg);
  va_end(arg);

  err_handler(obj, obj, NULL, error, error, str);
}

void data_arg_handler(Data *obj, Data *errarg, Ident error, char *fmt, ...)
{
  String *str;
  va_list arg;

  va_start(arg, fmt);
  str = vformat(fmt, arg);
  va_end(arg);

  err_handler(obj, obj, errarg, error, error, str);
}

void type_error(Data *obj, Ident error, char *fmt, ...)
{
  String *str;
  va_list arg;

  va_start(arg, fmt);
  str = vformat(fmt, arg);
  va_end(arg);

  err_handler(obj, obj, NULL, error, type_id, str);
}

void user_error(Ident error, String *explanation, Data *arg)
{
    List *location;
    Data *d;

    /* Construct a list giving the location. */
    location = list_new(5);
    d = list_empty_spaces(location, 5);

    /* The first element is 'method. */
    d->type = SYMBOL;
    d->u.symbol = ident_dup(method_id);
    d++;

    /* The second through fifth elements are the current method info. */
    fill_in_method_info(d);

    /* Return from the current method, and propagate the error. */
    frame_return();
    propagate_error(construct_traceback(error, explanation, arg, location), error);
}

static void out_of_ticks_error(void)
{
    static String *explanation;
    List *location;
    Data *d;

    /* Construct a list giving the location. */
    location = list_new(5);
    d = list_empty_spaces(location, 5);

    /* The first element is 'interpreter. */
    d->type = SYMBOL;
    d->u.symbol = ident_dup(interpreter_id);
    d++;

    /* The second through fifth elements are the current method info. */
    fill_in_method_info(d);

    /* Don't give the topmost frame a chance to return. */
    frame_return();

    if (!explanation)
      explanation = string_from_chars("Out of ticks", 12);
    propagate_error(construct_traceback(methoderr_id, explanation, NULL, location), methoderr_id);
    list_discard(location);
}

int check_perms()
{
  if (cur_frame->object->dbref != SYSTEM_DBREF) {
    cthrow(perm_id, "Current object (#%l) is not the system object.",
	   cur_frame->object->dbref);
    return 1;
  } else {
    return 0;
  }
}

int check_index(int index, int len, Data *arg)
{
  if (index < 0) {
    data_obj_handler(arg, arg+1, range_id, "Index (%d) is less than one.", index + 1);
  } else if (index > len - 1) {
    data_obj_handler(arg, arg+1,
		     range_id, "Index (%d) is greater than length (%d)", index + 1, len);
  } else {
    return 1;
  }
  return 0;
}

int check_range(int start, int len, int end, Data *arg)
{
  if (start < 0) {
    data_obj_handler(arg, arg + 1,
		range_id, "Segment start (%d) is less than one.", start + 1);
  } else if (len < 0) {
    data_obj_handler(arg, arg + 2,
		range_id, "Segment length (%d) is less than zero.", len);
  } else if (start + len > end) {
    data_obj_handler(arg, arg + 2,
		range_id, "Segment extends to %d, past the end of the object (%d).",
		start + len, end);
  } else {
    return 1;
  }
  return 0;
}

static void func_num_error(int num_args, char *required)
{
    Number_buf nbuf;

    cthrow(numargs_id, "Called with %s argument%s, requires %s.",
	  english_integer(num_args, nbuf),
	  (num_args == 1) ? "" : "s", required);
}

static void func_type_error(char *which, Data *wrong, int required)
{
  type_error(wrong, type2id(required), "The %s argument (%D) is not %s.", which, wrong, english_type(required));
}

int func_init_0(void)
{
    int arg_start = pop_args();
    int num_args = stack_pos - arg_start;

    if (num_args)
	func_num_error(num_args, "none");
    else
	return 1;
    return 0;
}

int func_init_1(Data **args, int type1)
{
    int arg_start = pop_args();
    int num_args = stack_pos - arg_start;

    *args = &stack[arg_start];
    if (num_args != 1)
	func_num_error(num_args, "one");
    else if (type1 && stack[arg_start].type != type1)
	func_type_error("first", &stack[arg_start], type1);
    else
	return 1;
    return 0;
}

int func_init_2(Data **args, int type1, int type2)
{
    int arg_start = pop_args();
    int num_args = stack_pos - arg_start;

    *args = &stack[arg_start];
    if (num_args != 2)
	func_num_error(num_args, "two");
    else if (type1 && stack[arg_start].type != type1)
	func_type_error("first", &stack[arg_start], type1);
    else if (type2 && stack[arg_start + 1].type != type2)
	func_type_error("second", &stack[arg_start + 1], type2);
    else
	return 1;
    return 0;
}

int func_init_3(Data **args, int type1, int type2, int type3)
{
    int arg_start = pop_args();
    int num_args = stack_pos - arg_start;

    *args = &stack[arg_start];
    if (num_args != 3)
	func_num_error(num_args, "three");
    else if (type1 && stack[arg_start].type != type1)
	func_type_error("first", &stack[arg_start], type1);
    else if (type2 && stack[arg_start + 1].type != type2)
	func_type_error("second", &stack[arg_start + 1], type2);
    else if (type3 && stack[arg_start + 2].type != type3)
	func_type_error("third", &stack[arg_start + 2], type3);
    else
	return 1;
    return 0;
}

int func_init_0_or_1(Data **args, int *num_args, int type1)
{
    int arg_start = pop_args();

    *args = &stack[arg_start];
    *num_args = stack_pos - arg_start;
    if (*num_args > 1)
	func_num_error(*num_args, "at most one");
    else if (type1 && *num_args == 1 && stack[arg_start].type != type1)
	func_type_error("first", &stack[arg_start], type1);
    else
	return 1;
    return 0;
}

int func_init_1_or_2(Data **args, int *num_args, int type1, int type2)
{
    int arg_start = pop_args();

    *args = &stack[arg_start];
    *num_args = stack_pos - arg_start;
    if (*num_args < 1 || *num_args > 2)
	func_num_error(*num_args, "one or two");
    else if (type1 && stack[arg_start].type != type1)
	func_type_error("first", &stack[arg_start], type1);
    else if (type2 && *num_args == 2 && stack[arg_start + 1].type != type2)
	func_type_error("second", &stack[arg_start + 1], type2);
    else
	return 1;
    return 0;
}

int func_init_2_or_3(Data **args, int *num_args, int type1, int type2,
		     int type3)
{
    int arg_start = pop_args();

    *args = &stack[arg_start];
    *num_args = stack_pos - arg_start;
    if (*num_args < 2 || *num_args > 3)
	func_num_error(*num_args, "two or three");
    else if (type1 && stack[arg_start].type != type1)
	func_type_error("first", &stack[arg_start], type1);
    else if (type2 && stack[arg_start + 1].type != type2)
	func_type_error("second", &stack[arg_start + 1], type2);
    else if (type3 && *num_args == 3 && stack[arg_start + 2].type != type3)
	func_type_error("third", &stack[arg_start + 2], type3);
    else
	return 1;
    return 0;
}

int func_init_1_to_3(Data **args, int *num_args, int type1, int type2,
		     int type3)
{
    int arg_start = pop_args();

    *args = &stack[arg_start];
    *num_args = stack_pos - arg_start;
    if (*num_args < 1 || *num_args > 3)
	func_num_error(*num_args, "one to three");
    else if (type1 && stack[arg_start].type != type1)
	func_type_error("first", &stack[arg_start], type1);
    else if (type2 && *num_args >= 2 && stack[arg_start + 1].type != type2)
	func_type_error("second", &stack[arg_start + 1], type2);
    else if (type3 && *num_args == 3 && stack[arg_start + 2].type != type3)
	func_type_error("third", &stack[arg_start + 2], type3);
    else
	return 1;
    return 0;
}

