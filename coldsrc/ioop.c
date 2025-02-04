/* ioop.c: Function operators for input and output. */

#define _POSIX_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "data.h"
#include "memory.h"
#include "io.h"
#include "cmstring.h"
#include "config.h"
#include "ident.h"
#include "util.h"

#define FILE_BUF_SIZE 100

void op_echo(void)
{
    Data *args;

    /* Accept a string to echo. */
    if (!func_init_1(&args, BUFFER))
	return;

    /* Write the string to any connection associated with this object. */
    tell(cur_frame->object->dbref, args[0].u.buffer);

    pop(1);
    push_int(1);
}

void op_echo_file(void)
{
    size_t size, i, r;
    Data *args;
    FILE *fp;
    char *fname;
    Buffer *buf;
    struct stat statbuf;
    int len;

    /* Accept the name of a file to echo. */
    if (!func_init_1(&args, STRING))
	return;

    /* Don't allow walking back up the directory tree. */
    if (strstr(string_chars(args[0].u.str), "../")) {
	cthrow(perm_id, "Filename %D is not legal.", &args[0]);
	return;
    }

    len = string_length(args[0].u.str);
    fname = TMALLOC(char, len + 6);
    memcpy(fname, "text/", 5);
    memcpy(fname + 5, string_chars(args[0].u.str), len);
    fname[len + 5] = 0;

    /* Stat the file to get its size. */
    if (stat(fname, &statbuf) < 0) {
	tfree_chars(fname);
	cthrow(perm_id, "Cannot find file %D.", &args[0]);
	return;
    }
    size = statbuf.st_size;

    /* Open the file for reading. */
    fp = open_scratch_file(fname, "r");
    tfree_chars(fname);
    pop(1);
    if (!fp) {
	cthrow(file_id, "Cannot open file %D for reading.", &args[0]);
	return;
    }

    /* Allocate a buffer to hold the file contents. */
    buf = buffer_new(size);

    /* Read in the file. */
    i = 0;
    while (i < size) {
	r = fread(buf->s + i, sizeof(unsigned char), size, fp);
	if (r <= 0) {
	    buffer_discard(buf);
	    close_scratch_file(fp);
	    cthrow(file_id, "Trouble reading file %D.", &args[0]);
	    return;
	}
	i += r;
    }

    /* Write the file. */
    tell(cur_frame->object->dbref, buf);

    /* Discard the buffer and close the file. */
    buffer_discard(buf);
    close_scratch_file(fp);

    push_int(1);
}

void op_disconnect(void)
{
    /* Accept no arguments. */
    if (!func_init_0())
	return;

    /* Kick off anyone assigned to the current object. */
    push_int(boot(cur_frame->object->dbref));
}

void op_filestat(void)
{
  Data *args;
  struct stat statbuf;

  Data *dp, *dp2;
  List *list, *l;

  /* Accept the name of a file to stat. */
  if (!func_init_1(&args, STRING))
    return;

  if (check_perms()) {
    return;
  }

  /* Stat the file */
  if (stat(string_chars(args[0].u.str), &statbuf) < 0) {
    cthrowchar(file_id, strerror(errno), "Cannot find file %D.", &args[0]);
    return;
  }

  list = list_new(3);
  dp = list_empty_spaces(list,3);

  {
    /* file size */
    dp->type = INTEGER;
    dp->u.val = statbuf.st_size;
  }

  dp++;
  dp->type = LIST;
  dp->u.list = l = list_new(3);

  {
    /* file perms:  mode, uid, gid */
    dp2 = list_empty_spaces(l,3);
    dp2->type = INTEGER;
    dp2->u.val = statbuf.st_mode;
    dp2++;
    dp2->type = INTEGER;
    dp2->u.val = statbuf.st_uid;
    dp2++;
    dp2->type = INTEGER;
    dp2->u.val = statbuf.st_gid;
  }

  dp++;
  dp->type = LIST;
  dp->u.list = l = list_new(3);
  {
    /* file times:  access, modification, change */
    dp2 = list_empty_spaces(l,3);
    dp2->type = INTEGER;
    dp2->u.val = statbuf.st_atime;
    dp2++;
    dp2->type = INTEGER;
    dp2->u.val = statbuf.st_mtime;
    dp2++;
    dp2->type = INTEGER;
    dp2->u.val = statbuf.st_ctime;
  }

  anticipate_assignment();
  args[0].type = LIST;
  args[0].u.list = list;
}

void op_read(void)
{
  size_t i, r;
  Data *args;
  FILE *fp;
  Buffer *buf;
  struct stat statbuf;
  int offset, size, argc;

  /* Accept the name of a file to read, an offset in file, and the length. */
  if (!func_init_1_to_3(&args, &argc, STRING, INTEGER, INTEGER))
    return;

  if (check_perms()) {
    return;
  }

  /* Open the file for reading. */
  fp = open_scratch_file(string_chars(args[0].u.str), "r");
  if (!fp) {
    cthrow(file_id, "Cannot open file %D for reading.", &args[0]);
    return;
  }

  if (argc > 1) {
    offset = args[1].u.val;
  } else {
    offset = 0;
  }

  /* seek to the offset */
  if (offset > 0) {
    offset = fseek(fp, offset, SEEK_SET);
  } else if (offset < 0) {
    offset = fseek(fp, offset, SEEK_END);
  }

  if (offset != 0) {
    close_scratch_file(fp);
    cthrowchar(file_id, strerror(errno),
	       "Cannot lseek %D to %d: %s", &args[0], offset, strerror(errno));
    return;
  }

  if (argc > 2) {
    size = args[2].u.val;
  } else {
    /* Stat the file to get its size. */
    if (stat(string_chars(args[0].u.str), &statbuf) < 0) {
      close_scratch_file(fp);
      cthrowchar(file_id, strerror(errno), "Cannot find file %D.", &args[0]);
      return;
    }
    size = statbuf.st_size;
  }
  
  /* Allocate a buffer to hold the file contents. */
  buf = buffer_new(size);

  /* Read in the file. */
  i = 0;
  while (i < size) {
    r = fread(buf->s + i, sizeof(unsigned char), size, fp);
    if (r <= 0) {
      Data d, *dp;
      List *l;
      char *strerr = "Out of data";
      close_scratch_file(fp);

      d.type = LIST;
      d.u.list = l = list_new(2);
      dp = list_empty_spaces(l,2);

      dp->type = STRING;
      dp->u.str = string_new(strlen(strerr));
      dp->u.str = string_add_chars(dp->u.str, strerr, strlen(strerr));

      dp++;
      dp->type = BUFFER;
      dp->u.buffer = buf;
      buf->len = i;
      cthrowdata(file_id, &d, "Trouble (%s) reading file %D after %d bytes.", strerr, &args[0], i);
      list_discard(l);
      return;
    }
    i += r;
  }
  buf->len = i;

  close_scratch_file(fp);

  anticipate_assignment();
  pop(argc);
  push_buffer(buf);
  buffer_discard(buf);
}

void op_write(void)
{
  size_t i, r;
  Data *args;
  FILE *fp;
  Buffer *buf;
  int offset, size, argc;

  /* Accept the name of a file to read, an offset in file, and the length. */
  if (!func_init_2_or_3(&args, &argc, STRING, BUFFER, INTEGER))
    return;

  if (check_perms()) {
    return;
  }

  /* Open the file for writing. */
  fp = open_scratch_file(string_chars(args[0].u.str), "a");
  if (!fp) {
    cthrow(file_id, "Cannot open file %D for writing.", &args[0]);
    return;
  }

  buf = args[1].u.buffer;
  size = buffer_len(buf);

  /* seek to the offset */
  if (argc > 2) {
    offset = args[2].u.val;
    /* seek to the offset */
    if (offset >= 0) {
      offset = fseek(fp, offset, SEEK_SET);
    } else {
      offset = fseek(fp, offset, SEEK_END);
    }
  } else {
    offset = 0; /* no seek needed - "a" mode puts us at the end */
  }  

  if (offset != 0) {
    close_scratch_file(fp);
    cthrowchar(file_id, strerror(errno),
	       "Cannot lseek %D to %d: %s", &args[0], offset, strerror(errno));
    return;
  }

  /* write out the file. */
  i = 0;
  while (i < size) {
    r = fwrite(buf->s + i, sizeof(unsigned char), size, fp);
    if (r <= 0) {
      Data d, *dp;
      List *l;
      char *strerr = "Unable to write more";
      close_scratch_file(fp);

      d.type = LIST;
      d.u.list = l = list_new(2);

      dp = list_empty_spaces(l,2);
      dp->type = STRING;
      dp->u.str = string_new(strlen(strerr));
      dp->u.str = string_add_chars(dp->u.str, strerr, strlen(strerr));

      dp++;
      dp->type = INTEGER;
      dp->u.val = i;

      cthrowdata(file_id, &d,
		 "Trouble (%s) writing file %D after %d bytes.", strerr, &args[0], i);
      list_discard(l);
      return;
    }
    fflush(fp);
    i += r;
  }

  close_scratch_file(fp);

  pop(argc);
  push_int(size);
}

void op_ls(void)
{
  Data *args;
  List *list;

  DIR *dir;
  struct dirent *ent;

  /* Accept the name of a file to stat. */
  if (!func_init_1(&args, STRING))
    return;

  dir = opendir(string_chars(args[0].u.str));
  if (!dir) {
    cthrowchar(file_id, strerror(errno), "Can't ls %D", &args[0]);
    return;
  }

  list = list_new(0);
  while ((ent = readdir(dir))) {
    Data d;
    d.type = STRING;
    d.u.str = string_from_chars(ent->d_name, strlen(ent->d_name));
    list = list_add(list, &d);
    string_discard(d.u.str);
  }
  closedir(dir);
  anticipate_assignment();
  args[0].type = LIST;
  args[0].u.list = list;  
}
