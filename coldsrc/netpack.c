/* dbpack.c: Write and retrieve objects to disk. */

#define _POSIX_SOURCE

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "x.tab.h"
#include "dbpack.h"
#include "object.h"
#include "data.h"
#include "memory.h"
#include "cmstring.h"
#include "ident.h"
#include "cache.h"
#include "config.h"
#include "execute.h"
#include "lookup.h"
#include "opcodes.h"
#include "util.h"

static int unpackRaw(Buffer *f, int *ppc)
{
  int result;
  int pc = *ppc;
  unsigned char *rp = (unsigned char *)&result;

  rp[3] = f->s[pc++];
  rp[2] = f->s[pc++];
  rp[1] = f->s[pc++];
  rp[0] = f->s[pc++];
  *ppc = pc;

  return result;
}

static Buffer *packRaw(Buffer *f, int value)
{
  unsigned char *vp = (unsigned char *)&value;

  f = buffer_add(f, vp[3]);
  f = buffer_add(f, vp[2]);
  f = buffer_add(f, vp[1]);
  f = buffer_add(f, vp[0]);
  return f;
}

static int unpackInt(Buffer *f, int *ppc)
{
  int pc = *ppc;
  int result = f->s[pc++];

  if (result == 0xF1) {
    *ppc = pc;
    return -unpackInt(f, ppc);
  }
  if (result < 0x7F) {
    *ppc = pc;
    return result;
  }

  switch (result & 0xF0) {
  case 0x80:
  case 0x90:
  case 0xA0:
  case 0xB0:
    result = (result & 0x7f) << 8 | f->s[pc++];
    break;
  case 0xC0:
  case 0xD0:
    result = (result & 0x3f) << 8 | f->s[pc++];
    result = result << 8 | f->s[pc++];
    break;
  case 0xE0:
    result = (result & 0x1f) << 8 | f->s[pc++];
    result = result << 8 | f->s[pc++];
    result = result << 8 | f->s[pc++];
    break;
  case 0xF0:
    result = unpackRaw(f, &pc);
    break;

  default:
    break;
  }

  *ppc = pc;
  return result;
}

static Buffer *packInt(Buffer *b, int value)
{
  if (value < 0) {
    b = buffer_add(b, 0xF1);
    b = packInt(b, -value);
    return b;
  }
  if (value < 0x7f) {
    b = buffer_add(b, value);
  } else if (value < 0x3fff) {
    b = buffer_add(b, 0x80 | (value >> 8));
    b = buffer_add(b, value & 0xFF);
  } else if (value < 0x1fffff) {
    b = buffer_add(b, 0xC0 | (value >> 16));
    b = buffer_add(b, (value >> 8) & 0xFF);
    b = buffer_add(b, value & 0xFF);
  } else if (value < 0x0fffffff) {
    b = buffer_add(b, 0xE0 | (value >> 24));
    b = buffer_add(b, (value >> 16) & 0xFF);
    b = buffer_add(b, (value >> 8) & 0xFF);
  } else {
    /* default case - int encoding + one flag byte */
    b = buffer_add(b, 0xF0);
    b = packRaw(b, value);
  }

  return b;
}

static unsigned unpackOp(Buffer *b, int *pc)
{
  int op = unpackInt(b, pc);
  return op_info[op].opcode;
}

static Buffer *packOp(Buffer *b, unsigned value)
{
  return packInt(b, op_table[value].opcode);
}

static Buffer *packType(Buffer *fp, long n)
{
  return packInt(fp, n - INTEGER);
}

static long unpackType(Buffer *fp, int *pc)
{
  return unpackInt(fp, pc) + INTEGER;
}

static Buffer *packIdent(Buffer *fp, long id)
{
  char *s;
  int len;

  if (id == NOT_AN_IDENT)
    return packInt(fp, 0);

  s = ident_name(id);
  len = strlen(s);
  fp = packInt(fp, len);
  if (len)
    fp = buffer_add_chars(fp, s, len);
  return fp;
}


static long unpackIdent(Buffer *fp, int *pc)
{
  int len;
  char *s;
  long id;

  /* Read the length of the identifier. */
  len = unpackInt(fp, pc);

  /* If the length is 0, it's not really an identifier,
   * but a blank variable or method. */
  if (!len)
    return NOT_AN_IDENT;

  /* Otherwise, it's an identifier.  Read it into temporary storage. */
  s = TMALLOC(char, len + 1);
  MEMCPY(s, fp->s + *pc, len);
  s[len] = 0;
  (*pc) += len;

  /* Get the index for the identifier and free the temporary memory. */
  id = ident_get(s);
  tfree_chars(s);

  return id;
}

static Buffer *packString(Buffer *fp, String *str)
{
  if (str) {
    fp = packInt(fp, str->len);
    if (str->len)
      fp = buffer_add_chars(fp, string_chars(str), str->len);
  } else {
    fp = packInt(fp, -1);
  }
  return fp;
}

static String *unpackString(Buffer *fp, int *pc)
{
  String *str;
  int len;

  len = unpackInt(fp, pc);
  if (len == -1) {
    return NULL;
  }

  str = string_new(len);

  MEMCPY(string_chars(str), fp->s + *pc, len);
  (*pc) += len;
  str->len = len;
  str->s[len] = 0;

  return str;
}

static Buffer *packData(Buffer *fp, Data *data);
static Data *unpackData(Buffer *fp, int *pc, Data *data);

static Buffer *packList(Buffer *fp, List *list)
{
  Data *d;

  fp = packInt(fp, list_length(list));
  for (d = list_first(list); d; d = list_next(list, d))
    fp = packData(fp, d);
  return fp;
}

static List *unpackList(Buffer *fp, int *pc)
{
  int len, i;
  List *list;
  Data *d;

  len = unpackInt(fp, pc);
  list = list_new(len);

  d = list_empty_spaces(list, len);
  for (i = 0; i < len; i++)
    unpackData(fp, pc, d++);

  return list;
}


static Buffer *packDict(Buffer *fp, Dict *dict)
{
  fp = packList(fp, dict->keys);
  if (list_length(dict->keys))
    fp = packList(fp, dict->values);

  return fp;
}

static Dict *unpackDict(Buffer *fp, int *pc)
{
  Dict *dict;
  List *keys, *values;

  keys = unpackList(fp, pc);
  if (list_length(keys)) {
    values = unpackList(fp, pc);

    dict = dict_new(keys, values);

    list_discard(keys);
    list_discard(values);
  } else {
    dict = dict_new_empty();
    list_discard(keys);
  }

  return dict;
}

/* addRef - add a mapping dbref->dict */
static Dict *refs;
static void addRef(Dbref dbref)
{
  /* construct the dict key */
  Data val;
  val.type = INTEGER;
  val.u.val = dbref;

  if (refs && dict_find(refs, &val, (Data*)0) == keynf_id) {
    /* find the dbref's name and add to the dictionary */
    Object *root = cache_retrieve(ROOT_DBREF);	/* get root */
    Object *obj = cache_retrieve(dbref);	/* get object */
    Ident iname = ident_get("name");		/* get name ident */
    Data name;					/* name string */

    if (obj) {
      /* get dbref's name parameter */
      if (object_retrieve_var(obj, root, iname, &name) != paramnf_id) {
	char *s;

	/* we have the name as a symbol - get the char* from the ident form */
	s = ident_name(name.u.symbol);

	/* make a string datum from the char* */
	data_discard(&name);
	name.type = STRING;
	name.u.str = string_from_chars(s, strlen(s));

	/* add dbref-as-int->name-string to the reference dictionary */
	refs = dict_add(refs, &val, &name);
      }
    }

    /* clean up */
    data_discard(&name);
    ident_discard(iname);
    cache_discard(obj);
    cache_discard(root);
    data_discard(&val);
  }
}

/* getRef - given a dbref, look up its name in the dict
   and substitute the dbref with the given name */
static Dbref getRef(Dbref dbref)
{
  Data key;
  key.type = INTEGER;
  key.u.val = dbref;

  /* Translate the external to a local reference */
  if (refs && (dict_find(refs, &key, &key) != keynf_id)) {
    return key.u.dbref;
  }
  else
    return NOT_AN_IDENT;
}

static Buffer *packFrob(Buffer *fp, Frob *fr)
{
  addRef(fr->cclass);
  fp = packInt(fp, fr->cclass);
  fp = packData(fp, &fr->rep);
  return fp;
}

static Frob *unpackFrob(Buffer *fp, int *pc)
{
  Frob *fr = TMALLOC(Frob, 1);
  fr->cclass = getRef(unpackInt(fp, pc));
  unpackData(fp, pc, &fr->rep);
  return fr;
}


static Buffer *packData(Buffer *fp, Data *data)
{
  fp = packType(fp, data->type);
  switch (data->type) {

  case INTEGER:
    fp = packInt(fp, data->u.val);
    break;

  case STRING:
    fp = packString(fp, data->u.str);
    break;

  case DBREF:
    addRef(data->u.dbref);
    fp = packInt(fp, data->u.dbref);
    break;

  case LIST:
    fp = packList(fp, data->u.list);
    break;

  case SYMBOL:
    fp = packIdent(fp, data->u.symbol);
    break;

  case ERROR:
    fp = packIdent(fp, data->u.error);
    break;

  case FROB:
    fp = packFrob(fp, data->u.frob);
    break;

  case DICT:
    fp = packDict(fp, data->u.dict);
    break;

  case BUFFER: {
      int i;

      fp = packInt(fp, data->u.buffer->len);
      for (i = 0; i < data->u.buffer->len; i++)
	fp = packInt(fp, data->u.buffer->s[i]);
      break;
    }
  }
  return fp;
}

static Data *unpackData(Buffer *fp, int *pc, Data *data)
{
  data->type = unpackType(fp, pc);

  switch (data->type) {
  case INTEGER:
    data->u.val = unpackInt(fp, pc);
    break;

  case STRING:
    data->u.str = unpackString(fp, pc);
    break;
    
  case DBREF:
    data->u.dbref = getRef(unpackInt(fp, pc));
    break;
    
  case LIST:
    data->u.list = unpackList(fp, pc);
    break;
    
  case SYMBOL:
    data->u.symbol = unpackIdent(fp, pc);
    break;
    
  case ERROR:
    data->u.error = unpackIdent(fp, pc);
    break;
    
  case FROB:
    data->u.frob = unpackFrob(fp, pc);
    break;
    
  case DICT:
    data->u.dict = unpackDict(fp, pc);
    break;
    
  case BUFFER: {
      int len;
      
      len = unpackInt(fp, pc);
      data->u.buffer = buffer_new(len);
      MEMCPY(data->u.buffer->s, fp + *pc, len);
      (*pc) += len;
      break;
    }
  }
  return data;
}

static int *strings;	/* packed array of strings for this object */

static Buffer *packStrings(Buffer *fp, Object *obj)
{
  int in, out;
  int len = obj->num_strings;

  /* size after blank removal */
  for (in = 0; (in < obj->num_strings); in++)
    if (!obj->strings[in].str)
      len--;

  fp = packInt(fp, len);	/* store actual number of strings */

  if (!len)
    return fp;

  strings = EMALLOC(int, obj->strings_size);

  for (in = 0, out = 0; in < obj->strings_size; in++) {
    if (obj->strings[in].str) {
      strings[in] = out++;
      fp = packString(fp, obj->strings[in].str);
    } else
      strings[in] = -1;
  }
  assert(out == len);

  return fp;
}

static void unpackStrings(Buffer *fp, int *pc, Object *obj)
{
  int i;
  int len = unpackInt(fp, pc);

  for (i = 0; i < len; i++) {
    object_add_string(obj, unpackString(fp, pc));
  }
}

static int *idents;	/* packed array of idents for this object */

static Buffer *packIdents(Buffer *fp, Object *obj)
{
  int in, out;
  int len = obj->num_idents;

  /* adjust size for blank removal */
  for (in = 0; in < obj->num_idents; in++)
    if (obj->idents[in].id == NOT_AN_IDENT)
      len--;

  if (len)
    idents = EMALLOC(int, len);

  fp = packInt(fp, len);
  for (in = 0, out = 0; in < len; in++) {
    if (obj->idents[in].id != NOT_AN_IDENT) {
      fp = packIdent(fp, obj->idents[in].id);
      idents[in] = out++;
    } else {
      idents[in] = NOT_AN_IDENT;
    }
  }
  return fp;
}

static void unpackIdents(Buffer *fp, int *pc, Object *obj)
{
  int i;
  int len = unpackInt(fp, pc);

  for (i = 0; i < len; i++) {
    object_add_ident(obj, ident_name(unpackIdent(fp, pc)));
  }
}

static int digesting;
static Buffer *packVars(Buffer *fp, Object *obj)
{
  int in, out;
  int len;

  for (in = 0, len = 0; in < obj->vars.size; in++)
    if (obj->vars.tab[in].name != NOT_AN_IDENT)
      len++;

  fp = packInt(fp, len);

  for (in = 0, out = 0; in < obj->vars.size; in++) {
    Var *v = obj->vars.tab + in;
    if (v->name != NOT_AN_IDENT) {

      fp = packIdent(fp, v->name);

      /* record the reference to this var's defining class, and output it */
      addRef(v->cclass);
      fp = packInt(fp, v->cclass);

      /* output the value */
      if (digesting)
	fp = packData(fp, &(v->val));
    }
  }
  return fp;
}

static void unpackVars(Buffer *fp, int *pc, Object *obj)
{
  int i;
  int len;

  len = unpackInt(fp, pc);

  /* unpack values, recording order */
  for (i = 0; i < len; i++) {
    Data val;
    Ident id;
    Dbref class;

    id = unpackIdent(fp, pc);		/* get var name */
    class = getRef(unpackInt(fp, pc));	/* translate dbref */
    unpackData(fp, pc, &val);		/* get value */

    object_put_var(obj, class, id, &val);
  }
}

static int methodrefs;
static Buffer *packM(Buffer *fp, Method *method)
{
  int i;
  fp = packInt(fp, method->num_opcodes);
  for (i = 0; i < method->num_opcodes;) {
    int opcode = method->opcodes[i++];
    Op_info *info = &op_table[opcode];
    int arg;
    int id;
    long dbref;

    fp = packOp(fp, opcode);

    /* record the reference if this is an explicit dbref */
    if (methodrefs) {
      if (opcode == DBREF)
	addRef(method->opcodes[i]);
      else if (opcode == NAME) {
	id = object_get_ident(method->object, method->opcodes[i]);
	if ((id != NOT_AN_IDENT) && lookup_retrieve_name(id, &dbref)) {
	  addRef(dbref);
	}
      }
    }

    for (arg = 0; arg < 2; arg++) {
      int arg_type = (arg == 0) ? info->arg1 : info->arg2;
      if (arg_type) {
	switch (arg_type) {
	case STRING:
	  fp = packInt(fp, strings[method->opcodes[i++]]);
	  break;

	case IDENT:
	  fp = packInt(fp, idents[method->opcodes[i++]]);
	  break;

	case INTEGER:
	case ERROR:
	case JUMP:
	case VAR:
	  fp = packInt(fp, method->opcodes[i++]);
	  break;

	  break;
	}
      } else
	break;
    }
  }
  return fp;
}

static Buffer *packMethod(Buffer *fp, Method *method)
{
  int i, j;

  fp = packIdent(fp, method->name);

  fp = packInt(fp, method->num_args);
  for (i = 0; i < method->num_args; i++) {
    fp = packInt(fp, idents[method->argnames[i]]);
  }

  if (method->rest != NOT_AN_IDENT) {
    fp = packInt(fp, idents[method->rest]);
  } else {
    fp = packInt(fp, NOT_AN_IDENT);
  }

  fp = packInt(fp, method->num_vars);
  for (i = 0; i < method->num_vars; i++)
    fp = packInt(fp, idents[method->varnames[i]]);

  fp = packInt(fp, method->num_error_lists);
  for (i = 0; i < method->num_error_lists; i++) {
    fp = packInt(fp, method->error_lists[i].num_errors);
    for (j = 0; j < method->error_lists[i].num_errors; j++)
      fp = packIdent(fp, method->error_lists[i].error_ids[j]);
  }

  fp = packInt(fp, method->overridable);
  fp = packM(fp, method);

  return fp;
}

static Buffer *packMethods(Buffer *fp, Object *obj)
{
  int i, len;

  for (i = 0, len = 0; i < obj->methods.size; i++)
    if (obj->methods.tab[i].m)
      len++;

  fp = packInt(fp, len);

  for (i = 0; i < obj->methods.size; i++) {
    if (obj->methods.tab[i].m) {
      fp = packMethod(fp, obj->methods.tab[i].m);
    }
  }
  return fp;
}

static void unpackM(Buffer *fp, int *pc, Method *method)
{
  int i;
  method->num_opcodes = unpackInt(fp, pc);
  method->opcodes = TMALLOC(long, method->num_opcodes);

  for (i = 0; i < method->num_opcodes;) {
    Op_info *info;
    int arg;

    info = &op_table[method->opcodes[i++] = unpackOp(fp, pc)];

    for (arg = 0; arg < 2; arg++) {
      int arg_type = (arg == 0) ? info->arg1 : info->arg2;
      if (arg_type) {
	switch (arg_type) {
	case STRING:
	  method->opcodes[i++] = unpackInt(fp, pc);
	  break;

	case IDENT:
	  method->opcodes[i++] = unpackInt(fp, pc);
	  break;

	case ERROR:
	case JUMP:
	case VAR:
	case INTEGER:
	  method->opcodes[i++] = unpackInt(fp, pc);
	  break;

	}
      } else
	break;
    }
  }
}

static Method *unpackMethod(Buffer *fp, int *pc)
{
  int name, i, j, n;
  Method *method;

  /* Read in the name. */
  name = unpackIdent(fp, pc);
  if (name == NOT_AN_IDENT) {
    /* If this is -1, it was a marker for a blank entry. */
    return NULL;
  }

  method = EMALLOC(Method, 1);

  method->name = name;

  method->num_args = unpackInt(fp, pc);
  if (method->num_args) {
    method->argnames = TMALLOC(int, method->num_args);
    for (i = 0; i < method->num_args; i++) {
      method->argnames[i] = unpackInt(fp, pc);
    }
  }
  method->rest = unpackInt(fp, pc);
  
  method->num_vars = unpackInt(fp, pc);
  if (method->num_vars) {
    method->varnames = TMALLOC(int, method->num_vars);
    for (i = 0; i < method->num_vars; i++)
      method->varnames[i] = unpackInt(fp, pc);
  }

  method->num_error_lists = unpackInt(fp, pc);
  if (method->num_error_lists) {
    method->error_lists = TMALLOC(Error_list, method->num_error_lists);
    for (i = 0; i < method->num_error_lists; i++) {
      n = unpackInt(fp, pc);
      method->error_lists[i].num_errors = n;
      method->error_lists[i].error_ids = TMALLOC(int, n);
      for (j = 0; j < n; j++)
	method->error_lists[i].error_ids[j] = unpackIdent(fp, pc);
    }
  }

  method->overridable = unpackInt(fp, pc);

  unpackM(fp, pc, method);

  method->refs = 1;
  return method;
}

static void unpackMethods(Buffer *fp, int *pc, Object *obj)
{
  int i = unpackInt(fp, pc);

  for (; i; i--) {
    Method *m = unpackMethod(fp, pc);
    object_add_method(obj, m->name, m);
  }
}

/* op_depends - look for object references within this object */
void op_depends()
{
  Buffer *buf;
  Object *obj;
  Data *args;
  int num_args;

  if (!func_init_0_or_1(&args, &num_args, 0))
    return;

  /* Implode object into buffer. */
  obj = cur_frame->object;

  /* set up translation dict */
  refs = dict_new_empty();

  /* traverse object looking for references */
  strings = 0;
  idents = 0;
  digesting = 1;	/* pack var data */
  methodrefs = num_args;	/* capture op_dbref references if we have an arg */
  buf = buffer_new(0);
  buf = packList(buf, obj->parents);
  buf = packVars(buf, obj);
  buf = packStrings(buf, obj);
  buf = packIdents(buf, obj);
  buf = packMethods(buf, obj);
  buffer_discard(buf);

  if (idents)
    free(idents);
  if (strings)
    free(strings);

  /* return the reference translation dictionary */
  pop(num_args);
  push_dict(refs);
}

/* op_pack - pack this object into a buffer */
void op_pack()
{
  Buffer *buf;
  Dict *orefs;
  Object *obj;
  Data *args;
  int num_args;

  /* Accept an optional argument of (nearly) any type. */
  if (!func_init_0_or_1(&args, &num_args, 0))
    return;

  strings = 0;
  idents = 0;
  digesting = 1;	/* pack var data */
  methodrefs = 1;	/* capture op_dbref references */

  buf = buffer_new(0);
  if (num_args) {
    switch (args[0].type) {
    case INTEGER:
    case STRING:
    case SYMBOL:
    case ERROR:
    case BUFFER:
      refs = 0;
      buf = packData(buf, args);
      break;

    case DICT:
    case LIST:
    case FROB:
      {
	Buffer *obuf;
	refs = dict_new_empty();	/* set up translation dict */

	/* first pack datum and collect external refs */
	switch (args[0].type) {
	case DICT:
	  obuf = packDict(buffer_new(0), args[0].u.dict);
	  break;
	case LIST:
	  obuf = packList(buffer_new(0), args[0].u.list);
	  break;
	case FROB:
	  obuf = packFrob(buffer_new(0), args[0].u.frob);
	  break;
	}

	/* assemble buffer */
	buf = packType(buf, args[0].type);
	buf = packDict(buf, refs);
	buf = buffer_append(buf, obuf);
	buffer_discard(obuf);
	dict_discard(refs);
	refs = 0;
      }
      break;

    case DBREF:
      cthrow(type_id, "Can't pack data of type %s.",
	     english_type(args[0].type));
      return;
    }
  } else {
    Buffer *obuf;

    /* Implode object into buffer. */
    obj = cur_frame->object;

    /* set up translation dict */
    refs = dict_new_empty();

    /* traverse and pack object */
    buf = packList(buf, obj->parents);
    buf = packVars(buf, obj);
    buf = packStrings(buf, obj);
    buf = packIdents(buf, obj);
    buf = packMethods(buf, obj);

    if (idents)
      free(idents);
    if (strings)
      free(strings);

    /* traverse and pack reference translation dict */
    orefs = refs;
    refs = (Dict *)0; /* collect no new references */
    obuf = buffer_append(packDict(buffer_new(0), orefs), buf);
    buffer_discard(buf);
    dict_discard(orefs);
  
    /* prepend DBREF type to buffer */
    buf = buffer_append(packType(buffer_new(0), DBREF), obuf);
    buffer_discard(obuf);
  }

  pop(num_args);
  push_buffer(buf);
  buffer_discard(buf);
}

static int refTrans(Dict *trans)
{
  Data *d;
  List *err = (List *)0;

  /* translate all dbrefs to local dbrefs in trans dictionary*/
  for (d = list_first(trans->values); d; d = list_next(trans->values, d)) {

    /* find the dbref from the object name */
    Dbref dbref;
    Ident name = ident_get(string_chars(d->u.str));  /* get name ident */
    if (!lookup_retrieve_name(name, &dbref)) {
      if (!err)
	err = list_new(1);
      err = list_add(err, d);
      data_discard(d);
      ident_discard(name);
    } else {
      /* found name's dbref - enter translation in the value list */
      ident_discard(name);
      data_discard(d);
      d->type = DBREF;
      d->u.dbref = dbref;
    }
  }

  if (err) {
    /* Unresolved references - throw 'em */
    Data errlist;
    errlist.type = LIST;
    errlist.u.list = err;
    cthrowdata(namenf_id, &errlist, "Unresolved References");

    list_discard(err);
    dict_discard(trans);
    pop(1);
    return 1;
  } else 
    return 0;
}


void op_unpack()
{
  Data *args;
  Buffer *buf;
  int cnt;
  List *parents;
  Dict *trans;
  Object *obj;

  Data result;
  List *returned;

  /* require one buffer argument */
  if (!func_init_1(&args, BUFFER))
    return;

  /* only $system may call this */
  if (check_perms()) {
    return;
  }

  buf = args[0].u.buffer;	/* grab input buffer */
  refs = (Dict *)0;
  cnt = 0;
  result.type = unpackType(buf, &cnt);

  switch (result.type) {
  case INTEGER:
  case STRING:
  case SYMBOL:
  case ERROR:
  case BUFFER:
    cnt = 0;
    unpackData(buf, &cnt, &result);
    break;

  case DICT:
  case LIST:
  case FROB:
    /* Unpack reference lists and create reference dictionary */
    /* construct a mapping external value -> local value */
    trans = unpackDict(buf, &cnt);
    if (refTrans(trans))
      return;
    else
      refs = trans;

    switch (result.type) {
    case DICT:
      result.u.dict = unpackDict(buf, &cnt);
      break;
    case LIST:
      result.u.list = unpackList(buf, &cnt);
      break;
    case FROB:
      result.u.frob = unpackFrob(buf, &cnt);
      break;
    }
    break;

  case DBREF:

    /* Unpack reference lists and create reference dictionary */
    /* construct a mapping external value -> local value */
    trans = unpackDict(buf, &cnt);
    if (refTrans(trans))
      return;
    else
      refs = trans;

    /* Unpack parents. */
    parents = unpackList(buf, &cnt);

    /* Create the new object with a new dbref. */
    obj = object_new(-1, parents);

    /* unpack the rest of the object */
    unpackVars(buf, &cnt, obj);
    unpackStrings(buf, &cnt, obj);
    unpackIdents(buf, &cnt, obj);
    unpackMethods(buf, &cnt, obj);

    result.u.dbref = obj->dbref;
    break;

  default:
    cthrow(type_id, "Buffer contained unrecognised type.");
    pop(1);
    return;
  }

  /* prepare the return value - a list of value and overflow buffer */
  returned = list_new(3);
  returned = list_add(returned, &result);

  /* return any unused buffer segment */
  if (cnt < buffer_len(buf)) {
    int len = buffer_len(buf) - cnt;
    Buffer *bp = buffer_new(len);
    buffer_add_chars(bp, buf->s + cnt, len);
    result.type = BUFFER;
    result.u.buffer = bp;
    returned = list_add(returned, &result);
  }

  /* return all externally referenced object names,
     all of which will be valid dbref names */
  if (refs) {
    Data external;

    /* add the list of referenced keys to the return value */
    external.type = LIST;
    external.u.list = list_dup(refs->keys);
    returned = list_add(returned, &external);

    /* zero out the refs dictionary for next time */
    dict_discard(refs);
    refs = (Dict *)0;
  }

  pop(1);
  push_list(returned);
  list_discard(returned);
}

void op_digestable(void)
{
  Buffer *buf;
  Buffer *obuf;
  Dict *orefs;
  Object *obj;

  /* Accept an optional argument of (nearly) any type. */
  if (!func_init_0())
    return;

  strings = 0;
  idents = 0;
  digesting = 0;	/* don't pack var data for digest */
  methodrefs = 1;	/* capture op_dbref references */

  buf = buffer_new(0);

  /* Implode object into buffer. */
  obj = cur_frame->object;

  /* set up translation dict */
  refs = dict_new_empty();

  /* traverse and pack object */
  buf = packList(buf, obj->parents);
  buf = packVars(buf, obj);
  buf = packStrings(buf, obj);
  buf = packIdents(buf, obj);
  buf = packMethods(buf, obj);

  if (idents)
    free(idents);
  if (strings)
    free(strings);

  /* traverse and pack reference translation dict */
  orefs = refs;
  refs = (Dict *)0; /* collect no new references */
  obuf = buffer_append(packDict(buffer_new(0), orefs), buf);
  buffer_discard(buf);
  dict_discard(orefs);
  
  /* prepend DBREF type to buffer */
  buf = buffer_append(packType(buffer_new(0), DBREF), obuf);
  buffer_discard(obuf);

  push_buffer(buf);
  buffer_discard(buf);
}


