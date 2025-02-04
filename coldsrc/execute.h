/* execute.h: Declarations for executing C-- tasks. */

#ifndef EXECUTE_H
#define EXECUTE_H

typedef struct frame Frame;
typedef struct error_action_specifier Error_action_specifier;
typedef struct handler_info Handler_info;
typedef struct vmstate VMState;
typedef struct vmstack VMStack;

#include <sys/types.h>
#include <stdlib.h>
#include <stdarg.h>
#include "data.h"
#include "object.h"
#include "io.h"

/* We use the MALLOC_DELTA defines to keep table sizes thirty-two bytes less
 * than a power of two, which is helpful on buddy systems. */
#define STACK_MALLOC_DELTA 4
#define ARG_STACK_MALLOC_DELTA 8

struct vmstack {
  Data *stack;
  int stack_size;
  int *arg_starts, arg_size;
  VMStack *next;
};

struct vmstate {
    Frame *cur_frame;
    Connection *cur_conn;
    Data *stack;
    int stack_pos, stack_size;
    int *arg_starts, arg_pos, arg_size;
    int task_id;
    int paused;
    VMState *next;
};

struct frame {
    Object *object;
    Dbref sender;
    Data rep;		/* the rep of sender, if it's a frob */
    Dbref caller;
    Method *method;
    long *opcodes;
    int pc;
    int last_pc;
    int ticks;
    double profile;	/* microseconds elapsed in this frame */
    int stack_start;
    int var_start;
    int argpos_start;
    Error_action_specifier *specifiers;
    Handler_info *handler_info;
    Frame *caller_frame;
};

struct error_action_specifier {
    int type;
    int stack_pos;
    union {
	struct {
	    int end;
	} critical;
	struct {
	    int end;
	} propagate;
	struct {
	    int error_list;
	    int handler;
	} ccatch;
	struct {
	  /* this is a marker left where an object error-handler is called */
	    Data *result;	/* where the handler result should go (ptr to stack) */
	} obj;
    } u;
    Error_action_specifier *next;
};

struct handler_info {
    List *traceback;
    Ident error;
    Handler_info *next;
};

extern Frame *cur_frame;
extern Connection *cur_conn;
extern Data *stack;
extern int stack_pos, stack_size;
extern int *arg_starts, arg_pos, arg_size;
extern String *numargs_str;
extern long task_id;
extern long tick;
extern VMState *paused;
extern int opcode_restart;	/* one-shot signalling successful return from object error-handler */

void init_execute(void);
long task(Connection *conn, Dbref dbref, long message, int num_args, ...);
void task_method(Connection *conn, Object *obj, Method *method);
long frame_start(Object *obj, Method *method, Dbref sender, Data *rep, Dbref caller,
		 int stack_start, int arg_start, int arg_pos);
void frame_return(void);
void anticipate_assignment(void);
Ident pass_message(int stack_start, int arg_start);
Ident send_message(Dbref dbref, Ident message, Data *rep, int stack_start, int arg_start);
void pop(int n);
void check_stack(int n);
void push_int(long n);
void push_string(String *str);
void push_dbref(Dbref dbref);
void push_list(List *list);
void push_symbol(Ident id);
void push_error(Ident id);
void push_dict(Dict *dict);
void push_buffer(Buffer *buffer);
void push_data(Data *data);
int pop_args();
int func_init_0();
int func_init_1(Data **args, int type1);
int func_init_2(Data **args, int type1, int type2);
int func_init_3(Data **args, int type1, int type2, int type3);
int func_init_0_or_1(Data **args, int *num_args, int type1);
int func_init_1_or_2(Data **args, int *num_args, int type1, int type2);
int func_init_2_or_3(Data **args, int *num_args, int type1, int type2,
		     int type3);
int func_init_1_to_3(Data **args, int *num_args, int type1, int type2,
		     int type3);
void obj_handler(Object *obj, Data *data, Ident error, char *fmt, ...);
void data_obj_handler(Data *target, Data *obj, Ident error, char *fmt, ...);
void data_handler(Data *obj, Ident error, char *fmt, ...);
void type_error(Data *obj, Ident error, char *fmt, ...);
void data_arg_handler(Data *obj, Data *arg, Ident error, char *fmt, ...);
int check_perms(void);
int check_index(int index, int len, Data *arg);
int check_range(int start, int len, int end, Data *arg);

void cthrow(long id, char *fmt, ...);
void cthrowstr(long id, String *explanation);
void cthrowdata(long id, Data *data, char *fmt, ...);
void cthrowchar(long id, char *str, char *fmt, ...);
void unignorable_error(Ident id, String *str);
void user_error(Ident error, String *str, Data *arg);
void propagate_error(List *traceback, Ident error);
void pop_error_action_specifier(void);
void pop_handler_info(void);
void task_suspend();
void task_resume(long tid, Data *ret);
void task_cancel(long tid);
void task_pause(void);
VMState *task_lookup(long tid);
List *task_list(void);
void run_paused_tasks();
List *task_callers(void);
#endif

