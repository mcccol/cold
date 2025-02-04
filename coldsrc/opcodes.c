/* opcodes.c: Information about opcodes. */

#define _POSIX_SOURCE

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "x.tab.h"
#include "opcodes.h"
#include "operator.h"
#include "util.h"

#define NUM_OPERATORS (sizeof(op_info) / sizeof(*op_info))

Op_info op_table[LAST_TOKEN];

static int first_function;

Op_info op_info[] = {

    /* Opcodes generated by language syntax (syntaxop.c). */
    { COMMENT,		"COMMENT",		op_comment, STRING },
    { POP,		"POP",			op_pop },
    { SET_LOCAL,	"SET_LOCAL",		op_set_local, VAR },
    { SET_OBJ_VAR,	"SET_OBJ_VAR",		op_set_obj_var, IDENT },
    { IF,		"IF",			op_if, JUMP },
    { IF_ELSE,		"IF_ELSE",		op_if, JUMP },
    { ELSE,		"ELSE",			op_else, JUMP },
    { FOR_RANGE,	"FOR_RANGE",		op_for_range, JUMP, VAR },
    { FOR_LIST,		"FOR_LIST",		op_for_list, JUMP, VAR },
    { WHILE,		"WHILE",		op_while, JUMP, JUMP },
    { SWITCH,		"SWITCH",		op_switch, JUMP },
    { CASE_VALUE,	"CASE_VALUE",		op_case_value, JUMP },
    { CASE_RANGE,	"CASE_RANGE",		op_case_range, JUMP },
    { LAST_CASE_VALUE,	"LAST_CASE_VALUE",	op_last_case_value, JUMP },
    { LAST_CASE_RANGE,	"LAST_CASE_RANGE",	op_last_case_range, JUMP },
    { END_CASE,		"END_CASE",		op_end_case, JUMP },
    { DEFAULT,		"DEFAULT",		op_default },
    { END,		"END",			op_end, JUMP },
    { BREAK,		"BREAK",		op_break, JUMP, VAR },
    { CONTINUE,		"CONTINUE",		op_continue, JUMP, VAR },
    { RETURN,		"RETURN",		op_return },
    { RETURN_EXPR,	"RETURN_EXPR",		op_return_expr },
    { CATCH,		"CATCH",		op_catch, JUMP, ERROR },
    { CATCH_END,	"CATCH_END",		op_catch_end, JUMP },
    { HANDLER_END,	"HANDLER_END",		op_handler_end },

    { ZERO,		"ZERO",			op_zero },
    { ONE,		"ONE",			op_one },
    { INTEGER,		"INTEGER",		op_integer, INTEGER },
    { STRING,		"STRING",		op_string, STRING },
    { DBREF,		"DBREF",		op_dbref, INTEGER },
    { SYMBOL,		"SYMBOL",		op_symbol, IDENT },
    { ERROR,		"ERROR",		op_error, IDENT },
    { NAME,		"NAME",			op_name, IDENT },
    { GET_LOCAL,	"GET_LOCAL",		op_get_local, VAR },
    { GET_OBJ_VAR,	"GET_OBJ_VAR",		op_get_obj_var,	IDENT },
    { START_ARGS,	"START_ARGS",		op_start_args },
    { PASS,		"PASS",			op_pass },
    { MESSAGE,		"MESSAGE",		op_message, IDENT },
    { EXPR_MESSAGE,	"EXPR_MESSAGE",		(void(*)(void))op_expr_message },
    { LIST,		"LIST",			op_list },
    { DICT,		"DICT",			op_dict },
    { BUFFER,		"BUFFER",		op_buffer },
    { FROB,		"FROB",			op_frob },
    { INDEX,		"INDEX",		op_index },
    { AND,		"AND",			op_and, JUMP },
    { OR,		"OR",			op_or, JUMP },
    { CONDITIONAL,	"CONDITIONAL",		op_if, JUMP },
    { SPLICE,		"SPLICE",		op_splice },
    { CRITICAL,		"CRITICAL",		op_critical, JUMP },
    { CRITICAL_END,	"CRITICAL_END", 	op_critical_end },
    { PROPAGATE,	"PROPAGATE",		op_propagate, JUMP },
    { PROPAGATE_END,	"PROPAGATE_END",	op_propagate_end },

    /* Arithmetic and relational operators (arithop.c). */
    { '!',		"!",			op_not },
    { NEG,		"NEG",			op_negate },
    { '*',		"*",			op_multiply },
    { '/',		"/",			op_divide },
    { '%',		"%",			op_modulo },
    { '+',		"+",			op_add },
    { SPLICE_ADD,	"SPLICE_ADD",		op_splice_add },
    { '-',		"-",			op_subtract },
    { EQ,		"EQ",			op_equal },
    { NE,		"NE",			op_not_equal },
    { '>',		">",			op_greater },
    { GE,		">=",			op_greater_or_equal },
    { '<',		"<",			op_less },
    { LE,		"<=",			op_less_or_equal },
    { IN,		"IN",			op_in },
    { BITAND,		"and",			op_bitand },
    { BITOR,		"or",			op_bitor },
    { BITSHIFT,		"shift",		op_bitshift },

    /* Generic data manipulation (dataop.c). */
    { TYPE,		"type",			op_type },
    { CLASS,		"class",		op_class },
    { TOINT,		"toint",		op_toint },
    { TOSTR,		"tostr",		op_tostr },
    { TOLITERAL,	"toliteral",		op_toliteral },
    { TODBREF,		"todbref",		op_todbref },
    { TOSYM,		"tosym",		op_tosym },
    { TOERR,		"toerr",		op_toerr },
    { VALID,		"valid",		op_valid },

    /* Operations on strings (stringop.c). */
    { STRLEN,		"strlen",		op_strlen },
    { SUBSTR,		"substr",		op_substr },
    { EXPLODE,		"explode",		op_explode },
    { STRSUB,		"strsub",		op_strsub },
    { PAD,		"pad",			op_pad },
    { MATCH_BEGIN,	"match_begin",		op_match_begin },
    { MATCH_TEMPLATE,	"match_template",	op_match_template },
    { MATCH_PATTERN,	"match_pattern",	op_match_pattern },
    { MATCH_REGEXP,	"match_regexp",		op_match_regexp },
    { CRYPT,		"crypt",		op_crypt },
    { UPPERCASE,	"uppercase",		op_uppercase },
    { LOWERCASE,	"lowercase",		op_lowercase },
    { STRCMP,		"strcmp",		op_strcmp },

    /* List manipulation (listop.c). */
    { LISTLEN,		"listlen",		op_listlen },
    { SUBLIST,		"sublist",		op_sublist },
    { INSERT,		"insert",		op_insert },
    { REPLACE,		"replace",		op_replace },
    { DELETE,		"delete",		op_delete },
    { SETADD,		"setadd",		op_setadd },
    { SETREMOVE,	"setremove",		op_setremove },
    { TOSET,		"toset",		op_toset },
    { UNION,		"union",		op_union },
    { FACTOR,		"factor",		op_factor },
    { QSORT,		"qsort",		op_qsort },

    /* Dictionary manipulation (dictop.c). */
    { DICT_KEYS,	"dict_keys",		op_dict_keys },
    { DICT_ADD,		"dict_add",		op_dict_add },
    { DICT_DEL,		"dict_del",		op_dict_del },
    { DICT_CONTAINS,	"dict_contains",	op_dict_contains },

    /* Buffer manipulation (bufferop.c). */
    { BUFFER_LEN,	"buffer_len",		op_buffer_len },
    { BUFFER_RETRIEVE,	"buffer_retrieve",	op_buffer_retrieve },
    { BUFFER_APPEND,	"buffer_append",	op_buffer_append },
    { BUFFER_REPLACE,	"buffer_replace",	op_buffer_replace },
    { BUFFER_ADD,	"buffer_add",		op_buffer_add },
    { BUFFER_TRUNCATE,	"buffer_truncate",	op_buffer_truncate },
    { BUFFER_TO_STRINGS,"buffer_to_strings",	op_buffer_to_strings },
    { BUFFER_FROM_STRINGS,"buffer_from_strings",op_buffer_from_strings },

    /* Miscellaneous operations (miscop.c). */
    { VERSION,		"version",		op_version },
    { RANDOM,		"random",		op_random },
    { TIME,		"time",			op_time },
    { CTIME,		"ctime",		op_ctime },
    { MIN,		"min",			op_min },
    { MAX,		"max",			op_max },
    { ABS,		"abs",			op_abs },
    { GET_NAME,		"get_name",		op_get_name },
    { TICKS_LEFT,       "ticks_left",           op_ticks_left },

    /* Current method information operations (methodop.c). */
    { THIS,		"this",			op_this },
    { DEFINER,		"definer",		op_definer },
    { SENDER,		"sender",		op_sender },
    { CALLER,		"caller",		op_caller },
    { TASK_ID,		"task_id",		op_task_id },
    { REP,		"rep",			op_rep },

    /* Error handling operations (errorop.c). */
    { ERROR_FUNC,	"error",		op_error_func },
    { TRACEBACK,	"traceback",		op_traceback },
    { THROW,		"throw",		op_throw },
    { RETHROW,		"rethrow",		op_rethrow },

    /* Input and output (ioop.c). */
    { ECHO_FUNC,	"echo",			op_echo },
    { ECHO_FILE,	"echo_file",		op_echo_file },
    { DISCONNECT,	"disconnect",		op_disconnect },
    { FILESTAT,		"filestat",		op_filestat },
    { READ,		"read",			op_read },
    { WRITE,		"write",		op_write },
    { LS,		"ls",			op_ls },
    { CONNECTIONS,	"connections",		op_connections },

    /* Operations on the current object (objectop.c). */
    { ADD_PARAMETER,	"add_parameter",	op_add_parameter },
    { PARAMETERS,	"parameters",		op_parameters },
    { DEL_PARAMETER,	"del_parameter",	op_del_parameter },
    { SET_VAR,		"set_var",		op_set_var },
    { GET_VAR,		"get_var",		op_get_var },
    { COMPILE,		"compile",		op_compile },
    { METHODS,		"methods",		op_methods },
    { FIND_METHOD,	"find_method",		op_find_method },
    { FIND_NEXT_METHOD,	"find_next_method",	op_find_next_method },
    { LIST_METHOD,	"list_method",		op_list_method },
    { DEL_METHOD,	"del_method",		op_del_method },
    { PARENTS,		"parents",		op_parents },
    { CHILDREN,		"children",		op_children },
    { ANCESTORS,	"ancestors",		op_ancestors },
    { HAS_ANCESTOR,	"has_ancestor",		op_has_ancestor },
    { SIZE,		"size",			op_size },

    /* DataBase Packing operations (dbpack.c) */
    { PACK,		"pack",			op_pack },
    { UNPACK,		"unpack",		op_unpack },
    { DIGESTABLE,	"digestable",		op_digestable },
    { DEPENDS,		"depends",		op_depends },

    /* Administrative operations (adminop.c). */
    { CREATE,		"create",		op_create },
    { CHPARENTS,	"chparents",		op_chparents },
    { DESTROY,		"destroy",		op_destroy },
    { LOG,		"log",			op_log },
    { CONN_ASSIGN,	"conn_assign",		op_conn_assign },
    { BINARY_DUMP,	"binary_dump",		op_binary_dump },
    { BINARY_BACKUP,	"binary_backup",		op_binary_backup },
    { TEXT_DUMP,	"text_dump",		op_text_dump },
    { RUN_SCRIPT,	"run_script",		op_run_script },
    { SHUTDOWN,		"shutdown",		op_shutdown },
    { BIND,		"bind",			op_bind },
    { UNBIND,		"unbind",		op_unbind },
    { CONNECT,		"connect",		op_connect },
    { SET_HEARTBEAT_FREQ, "set_heartbeat_freq", op_set_heartbeat_freq },
    { DATA,		"data",			op_data },
    { SET_NAME,		"set_name",		op_set_name },
    { DEL_NAME,		"del_name",		op_del_name },
    { TICK,		"tick",			op_tick },
    { HOSTNAME,		"hostname",		op_hostname },
    { IP,		"ip",			op_ip },
    { DB_TOP,		"db_top",		op_db_top },
    { RESUME,           "resume",               op_resume },
    { SUSPEND,          "suspend",              op_suspend },
    { TASKS,            "tasks",                op_tasks },
    { CANCEL,           "cancel",               op_cancel },
    { PAUSE,            "pause",                op_pause },
    { CALLERS,          "callers",              op_callers },
    { DISASSEMBLE,      "disassemble",          op_disassemble },
    { DEBUG,		"debug",		op_debug }
#ifdef RSACRYPT
    ,{ MKRSA,		"mkRSA",		op_mkRSA },
    { ENRSA,		"enRSA",		op_enRSA },
    { ENIDEA,		"enIDEA",		op_enIDEA },
    { DEIDEA,		"deIDEA",		op_deIDEA },
    { MD5,		"md5",			op_MD5 }
#endif
};
 
void init_op_table(void)
{
    int i;

    for (i = 0; i < NUM_OPERATORS; i++) {
	op_info[i].symbol = ident_get(op_info[i].name);
	op_info[i].opcount = 0;
	op_table[op_info[i].opcode] = op_info[i];
	op_table[op_info[i].opcode].opcode = i;
    }

    /* Look for first opcode with a lowercase name to find the first
     * function. */
    for (i = 0; i < NUM_OPERATORS; i++) {
	if (islower(*op_info[i].name))
	    break;
    }
    first_function = i;
    op_table[0].func = abort;
}

int find_function(char *name)
{
    int i;

    for (i = first_function; i < NUM_OPERATORS; i++) {
	if (strcmp(op_info[i].name, name) == 0)
	    return op_info[i].opcode;
    }

    return -1;
}
