/* adminop.c: Operators for administrative functions. */

#define _POSIX_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "data.h"
#include "object.h"
#include "dump.h"
#include "io.h"
#include "log.h"
#include "cache.h"
#include "util.h"
#include "config.h"
#include "ident.h"
#include "memory.h"
#include "net.h"
#include "lookup.h"

#ifdef BSD_FEATURES
/* vfork() is not POSIX. */
extern pid_t vfork(void);
#endif

extern char *sys_errlist[];
#define strerror(n) (sys_errlist[n])

extern int running;
extern long heartbeat_freq, db_top;

/* All of the functions in this file are interpreter function operators, so
 * they require that the interpreter data (the globals in execute.c) be in a
 * state consistent with interpretation, and that a stack position has been
 * pushed onto the arg_starts stack using op_start_args().  They will pop a
 * value off the argument starts stack, and may affect the interpreter data by
 * popping and pushing the data stack or throwing exceptions. */

void op_create(void)
{
    Data *args, *d;
    List *parents;
    Object *obj;

    /* Accept a list of parents. */
    if (!func_init_1(&args, LIST))
	return;

    if (check_perms())
      return;

    /* Get parents list from second argument. */
    parents = args[0].u.list;

    /* Verify that all parents are dbrefs. */
    for (d = list_first(parents); d; d = list_next(parents, d)) {
	if (d->type != DBREF) {
	    type_error(d, dbref_id, "Parent %D is not a dbref.", d);
	    return;
	} else if (!cache_check(d->u.dbref)) {
	  Object *obj = cache_retrieve(SYSTEM_DBREF);
	  obj_handler(obj, d, objnf_id, "Parent %D does not refer to an object.", d);
	  cache_discard(obj);
	    return;
	}
    }

    /* Create the new object. */
    obj = object_new(-1, parents);

    pop(1);
    push_dbref(obj->dbref);
    cache_discard(obj);
}

void op_chparents(void)
{
    Data *args, *d;
    Object *obj;
    int wrong;

    /* Accept a dbref and a list of parents to change to. */
    if (!func_init_2(&args, DBREF, LIST))
	return;

    if (check_perms())
	return;

    if (args[0].u.dbref == ROOT_DBREF) {
	cthrow(perm_id, "You cannot change the root object's parents.");
	return;
    }

    obj = cache_retrieve(args[0].u.dbref);
    if (!obj) {
      Object *obj = cache_retrieve(SYSTEM_DBREF);
      obj_handler(obj, &args[0], objnf_id, "Object #%l not found.", args[0].u.dbref);
      cache_discard(obj);
      return;
    }

    if (!list_length(args[1].u.list)) {
	cthrow(perm_id, "You must specify at least one parent.");
	return;
    }

    /* Call object_change_parents().  This will return the number of a
     * parent which was invalid, or -1 if they were all okay. */
    wrong = object_change_parents(obj, args[1].u.list);
    if (wrong >= 0) {
	d = list_elem(args[1].u.list, wrong);
	if (d->type != DBREF) {
	    cthrow(type_id, "New parent %D is not a dbref.", d);
	} else if (d->u.dbref == args[0].u.dbref) {
	    cthrow(parent_id, "New parent %D is the same as %D.", d, &args[0]);
	} else if (!cache_check(d->u.dbref)) {
	    cthrow(objnf_id, "New parent %D does not exist.", d);
	} else {
	    cthrow(parent_id, "New parent %D is a descendent of %D.", d,
		  &args[0]);
	}
    } else {
	pop(2);
	push_int(1);
    }

    cache_discard(obj);
}

void op_destroy(void)
{
    Data *args;
    Object *obj;

    /* Accept a dbref to destroy. */
    if (!func_init_1(&args, DBREF))
	return;

    if (check_perms()) {
    } else if (args[0].u.dbref == ROOT_DBREF) {
	cthrow(perm_id, "You can't destroy the root object.");
    } else if (args[0].u.dbref == SYSTEM_DBREF) {
	cthrow(perm_id, "You can't destroy the system object.");
    } else {
	obj = cache_retrieve(args[0].u.dbref);
	if (!obj) {
	    cthrow(objnf_id, "Object #%l not found.", args[0].u.dbref);
	    return;
	}
	/* Set the object dead, so it will go away when nothing is holding onto
	 * it.  cache_discard() will notice the dead flag, and call
	 * object_destroy(). */
	obj->dead = 1;
	cache_discard(obj);
	pop(1);
	push_int(1);
    }
}

/* Effects: If called by the system object with a string argument, logs it to
 * standard error using write_log(), and returns 1. */
void op_log(void)
{
    Data *args;

    /* Accept a string. */
    if (!func_init_1(&args, STRING))
	return;

    if (!check_perms()) {
        write_log("> %S", args[0].u.str);
	pop(1);
	push_int(1);
    }
}

/* Modifies: cur_player, contents of cur_conn.
 * Effects: If called by the system object with a dbref argument, assigns that
 * 	    dbref to cur_conn->dbref and to cur_player and returns 1, unless
 *	    there is no current connection, in which case it returns 0. */
void op_conn_assign(void)
{
    Data *args;

    /* Accept a dbref. */
    if (!func_init_1(&args, DBREF))
	return;

    if (check_perms()) {
      return;
    } 
    if (cur_conn) {
      cur_conn->dbref = args[0].u.dbref;
      pop(1);
      push_int(1);
    } else {
      pop(1);
      push_int(0);
    }
}

/* Modifies: The object cache, identifier table, and binary database files via
 *	     cache_sync() and ident_dump().
 * Effects: If called by the sytem object with no arguments, performs a binary
 *	    backup, ensuring that the db is duplicated.  Returns
 *	    1 if the binary backup succeeds, or 0 if it fails. */
void op_binary_backup(void)
{
    /* Accept no arguments. */
    if (!func_init_0())
	return;

    if (!check_perms()) {
	push_int(binary_backup());
    }
}

/* Modifies: The object cache, identifier table, and binary database files via
 *	     cache_sync() and ident_dump().
 * Effects: If called by the sytem object with no arguments, performs a binary
 *	    dump, ensuring that the files db and db.* are consistent.  Returns
 *	    1 if the binary dump succeeds, or 0 if it fails. */
void op_binary_dump(void)
{
    /* Accept no arguments. */
    if (!func_init_0())
	return;

    if (!check_perms()) {
	push_int(binary_dump());
    }
}

/* Modifies: The object cache and binary database files via cache_sync() and
 *	     two sweeps through the database.  Modifies the internal dbm state
 *	     use by dbm_firstkey() and dbm_nextkey().
 * Effects: If called by the system object with no arguments, performs a text
 *	    dump, creating a file 'textdump' which contains a representation
 *	    of the database in terms of a few simple commands and the C--
 *	    language.  Returns 1 if the text dump succeeds, or 0 if it
 *	    fails.*/
void op_text_dump(void)
{
    /* Accept no arguments. */
    if (!func_init_0())
	return;

    if (!check_perms()) {
	push_int(text_dump());
    }
}

void op_run_script(void)
{
    Data *args, *d;
    List *script_args;
    int num_args, argc, len, i, fd, status;
    pid_t pid;
    char *fname, **argv;

    /* Accept a name of a script to run, a list of arguments to give it, and
     * an optional flag signifying that we should not wait for completion. */
    if (!func_init_2_or_3(&args, &num_args, STRING, LIST, INTEGER))
	return;
    script_args = args[1].u.list;

    /* Verify that all items in argument list are strings. */
    for (d = list_first(script_args), i=0; d; d = list_next(script_args, d), i++) {
	if (d->type != STRING) {
	    cthrow(type_id, "Script argument %d (%D) is not a string.", i+1, d);
	    return;
	}
    }

    /* Restrict to system object. */
    if (check_perms()) {
	return;
    }

    /* Don't walking back up the directory tree. */
    if (strstr(string_chars(args[0].u.str), "../")) {
	cthrow(perm_id, "Filename %D is not legal.", &args[0]);
	return;
    }

    /* Construct the name of the script. */
    len = string_length(args[0].u.str);
    fname = TMALLOC(char, len + 9);
    memcpy(fname, "scripts/", 8);
    memcpy(fname + 8, string_chars(args[0].u.str), len);
    fname[len + 8] = 0;

    /* Build an argument list. */
    argc = list_length(script_args) + 1;
    argv = TMALLOC(char *, argc + 1);
    argv[0] = tstrdup(fname);
    for (d = list_first(script_args), i = 0; d; d = list_next(script_args, d), i++)
	argv[i + 1] = tstrdup(string_chars(d->u.str));
    argv[argc] = NULL;

    pop(num_args);

    /* Fork off a process. */
#ifdef BSD_FEATURES
    pid = vfork();
#else
    pid = fork();
#endif
    if (pid == 0) {
	/* Pipe stdin and stdout to /dev/null, keep stderr. */
	fd = open("/dev/null", O_RDWR);
	if (fd == -1) {
	    write_log("EXEC: Failed to open /dev/null: %s.", strerror(errno));
	    exit(-1);
	}
	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	execv(fname, argv);
	write_log("EXEC: Failed to exec \"%s\": %s.", fname, strerror(errno));
	exit(-1);
    } else if (pid > 0) {
	if (num_args == 3 && args[2].u.val) {
	    if (waitpid(pid, &status, WNOHANG) == 0)
		status = 0;
	} else {
	    waitpid(pid, &status, 0);
	}
    } else {
	write_log("EXEC: Failed to fork: %s.", strerror(errno));
	status = -1;
    }

    /* Free the argument list. */
    for (i = 0; i < argc; i++)
	tfree_chars(argv[i]);
    TFREE(argv, argc + 1);

    push_int(status);
}

/* Modifies: The 'running' global may be set to 0.
 * Effects: If called by the system object with no arguments, sets 'running'
 *	    to 0, causing the program to exit after this iteration of the main
 *	    loop finishes.  Returns 1. */
void op_shutdown(void)
{
    /* Accept no arguments. */
    if (!func_init_0())
	return;

    if (!check_perms()) {
	running = 0;
	push_int(1);
    }
}

void op_bind(void)
{
    Data *args;

    /* Accept a port to bind to, and a dbref to handle connections. */
    if (!func_init_2(&args, INTEGER, DBREF))
	return;

    if (check_perms()) {
	return;
    }

    if (add_server(args[0].u.val, args[1].u.dbref))
	push_int(1);
    else if (server_failure_reason == socket_id)
	cthrow(socket_id, "Couldn't create server socket.");
    else /* (server_failure_reason == bind_id) */
	cthrow(bind_id, "Couldn't bind to port %d.", args[0].u.val);
}

void op_unbind(void)
{
    Data *args;

    /* Accept a port number. */
    if (!func_init_1(&args, INTEGER))
	return;

    if (check_perms()) {
	return;
    }

    if (!remove_server(args[0].u.val))
	cthrow(servnf_id, "No server socket on port %d.", args[0].u.val);
    else
	push_int(1);
}

void op_connect(void)
{
    Data *args;
    char *address;
    int port;
    Dbref receiver;
    long r;

    if (!func_init_3(&args, STRING, INTEGER, DBREF))
	return;

    if (check_perms()) {
	return;
    }

    address = string_chars(args[0].u.str);
    port = args[1].u.val;
    receiver = args[2].u.dbref;

    r = make_connection(address, port, receiver);
    if (r == address_id)
	cthrow(address_id, "Invalid address: %s", strerror(errno));
    else if (r == socket_id)
	cthrow(socket_id, "Couldn't create socket for connection: %s", strerror(errno));
    pop(3);
    push_int(1);
}

void op_set_heartbeat_freq(void)
{
    Data *args;

    if (!func_init_1(&args, INTEGER))
	return;

    if (check_perms()) {
	return;
    }

    if (args[0].u.val <= 0)
	args[0].u.val = -1;
    heartbeat_freq = args[0].u.val;
    pop(1);
    push_int(1);
}

void op_data(void)
{
    Data *args, key, value;
    Object *obj;
    Dict *dict;
    int i;

    if (!func_init_1(&args, DBREF))
	return;

    if (check_perms()) {
	return;
    }

    obj = cache_retrieve(args[0].u.dbref);
    if (!obj) {
	cthrow(objnf_id, "No such object #%l", args[0].u.dbref);
	return;
    }

    /* Construct the dictionary. */
    dict = dict_new_empty();
    for (i = 0; i < obj->vars.size; i++) {
	if (obj->vars.tab[i].name == -1)
	    continue;
	key.type = DBREF;
	key.u.dbref = obj->vars.tab[i].cclass;
	if (dict_find(dict, &key, &value) == keynf_id) {
	    value.type = DICT;
	    value.u.dict = dict_new_empty();
	    dict = dict_add(dict, &key, &value);
	}

	key.type = SYMBOL;
	key.u.symbol = obj->vars.tab[i].name;
	value.u.dict = dict_add(value.u.dict, &key, &obj->vars.tab[i].val);

	key.type = DBREF;
	key.u.dbref = obj->vars.tab[i].cclass;
	dict = dict_add(dict, &key, &value);
	dict_discard(value.u.dict);
    }

    cache_discard(obj);
    pop(1);
    push_dict(dict);
    dict_discard(dict);
}

void op_set_name(void)
{
    Data *args;
    int result;

    if (!func_init_2(&args, SYMBOL, DBREF))
	return;

    if (check_perms()) {
	return;
    }

    result = lookup_store_name(args[0].u.symbol, args[1].u.dbref);
    pop(2);
    push_int(result);
}

void op_del_name(void)
{
    Data *args;

    if (!func_init_1(&args, SYMBOL))
	return;

    if (check_perms()) {
	return;
    }

    if (!lookup_remove_name(args[0].u.symbol)) {
	cthrow(namenf_id, "Can't find object name %I.", args[0].u.symbol);
	return;
    }

    pop(1);
    push_int(1);
}

#define CHECK_ADMIN  if (check_perms()) return;

void op_cancel(void)
{
  Data *args;

  if (!func_init_1(&args, INTEGER))
    return;

  CHECK_ADMIN
  if (!task_lookup(args[0].u.val))
    cthrow(type_id, "No such task");
  else {
    task_cancel(args[0].u.val);
    pop(1);
    push_int(1);
  }
}

void op_suspend(void)
{
  if (!func_init_0())
    return;
  CHECK_ADMIN
  task_suspend();
  /* we'll let task_resume push something onto the stack for us */
}

void op_resume(void) {
  Data *args;
  int nargs;
  long tid;

  if (!func_init_1_or_2(&args, &nargs, INTEGER, 0))
    return;
  CHECK_ADMIN
  tid = args[0].u.val;
  if (!task_lookup(tid)) {
    cthrow(type_id, "No such task");
  } else {
    if (nargs == 1) 
      task_resume(tid, NULL);
    else
      task_resume(tid, &args[1]);
    
    pop(nargs);
    push_int(0);
  }
}

void op_pause(void) {
  if (!func_init_0())
    return;
  push_int(0);
  task_pause();
}

void op_tasks(void) {
  if (!func_init_0())
    return;
  CHECK_ADMIN
  push_list(task_list());
}

  
void op_db_top(void)
{
    if (!func_init_0())
	return;
    push_int(db_top);
}

void op_tick(void)
{
    if (!func_init_0())
        return;
    push_int(tick);
}

void op_hostname(void)
{
    Data *args;
    String *r;

    /* Accept a port number. */
    if (!func_init_1(&args, STRING))
        return;

    r = hostname(args[0].u.str->s);

    pop(1);
    push_string(r);
}

void op_ip(void)
{
    Data *args;
    String *r;

    /* Accept a hostname. */
    if (!func_init_1(&args, STRING))
        return;

    r = ip(args[0].u.str->s);

    pop(1);
    push_string(r);
}

void op_callers(void)
{
    if (!func_init_0())
        return;
    CHECK_ADMIN
    push_list(task_callers());
}
