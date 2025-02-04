/* list.c: Routines for list manipulation.
 * This code is not ANSI-conformant, because it allocates memory at the end
 * of List structure and references it with a one-element array. */

#define _POSIX_SOURCE

#include "x.tab.h"
#include "list.h"
#include "memory.h"
#include <assert.h>

/* Note that we number list elements [0..(len - 1)] internally, while the
 * user sees list elements as numbered [1..len]. */

/* We use MALLOC_DELTA to keep our blocks about 32 bytes less than a power of
 * two.  We also have to account for the size of a List (16 bytes) which gets
 * added in before we allocate.  This works if a Data is sixteen bytes. */

#define MALLOC_DELTA	3
#define STARTING_SIZE	(16 - MALLOC_DELTA)

#ifndef NDEBUG
int list_check(List *l)
{
  int i;
  assert(l->refs > 0);

  for (i = l->start; i < (l->start + l->len); i++) {
    assert(data_refs(l->el + i));
  }
  return 1;
}
#endif

/* Input to this routine should be a list you want to modify, a start, and a
 * length.  The start gives the offset from list->el at which you start being
 * interested in data; the length is the amount of data there will be in the
 * list after that point after you finish modifying it.
 *
 * The return value of this routine is a list whose contents can be freely
 * modified, containing at least the information you claimed was interesting.
 * list->start will be set to the beginning of the interesting data; list->len
 * will be set to len, even though this will make some data invalid if
 * len > list->len upon input.  Also, the returned string may not be null-
 * terminated.
 *
 * If start is increased or len is decreased by this function, and list->refs
 * is 1, the uninteresting data will be discarded by this function.
 *
 * In general, modifying start and len is the responsibility of this routine;
 * modifying the contents is the responsibility of the calling routine. */
static List *prepare_to_modify(List *list, int start, int len)
{
    List *cnew;
    int i, need_to_move, need_to_resize, size;

    /* Figure out if we need to resize the list or move its contents.  Moving
     * contents takes precedence. */
    need_to_resize = (len - start) * 4 < list->size;
    need_to_resize = need_to_resize && list->size > STARTING_SIZE;
    need_to_resize = need_to_resize || (list->size < len);
    need_to_move = (list->refs > 1) || (need_to_resize && start > 0);

    if (need_to_move) {
      /* Move the list contents into a new list. */
      cnew = list_new(len);
      cnew->len = len;
      len = (list->len < len) ? list->len : len;
      for (i = 0; i < len; i++)
	data_dup(&cnew->el[i], &list->el[start + i]);
      list_discard(list);
      return cnew;
    } else if (need_to_resize) {
      /* Resize the list.  We can assume that list->start == start == 0. */
      assert((list->start == start) && (start == 0));
      for (; list->len > len; list->len--)
	data_discard(&list->el[list->len - 1]);
      list->len = len;
      size = STARTING_SIZE;
      while (size < len)
	size = size * 2 + MALLOC_DELTA;
      list = (List *)erealloc(list, sizeof(List) + (size * sizeof(Data)));
      list->size = size;
      return list;
    } else {
      for (; list->start < start; list->start++, list->len--)
	data_discard(&list->el[list->start]);
      for (; list->len > len; list->len--)
	data_discard(&list->el[list->start + list->len - 1]);
      list->start = start;
      list->len = len;
      return list;
    }
}


List *list_new(int len)
{
    List *cnew;
    int size;

    assert(len >= 0);

    size = STARTING_SIZE;
    while (size < len)
	size = size * 2 + MALLOC_DELTA;
    cnew = (List *)emalloc(sizeof(List) + (size * sizeof(Data)));
    cnew->len = 0;
    /*cnew->len = len;*/
    cnew->start = 0;
    cnew->size = size;
    cnew->refs = 1;
    return cnew;
}

List *list_dup(List *list)
{
    list->refs++;
    return list;
}

int list_length(List *list)
{
    return list->len;
}

Data *list_first(List *list)
{
    return (list->len) ? list->el + list->start : NULL;
}

Data *list_next(List *list, Data *d)
{
    assert(list->refs > 0);
    return (d < list->el + list->start + list->len - 1) ? d + 1 : NULL;
}

Data *list_last(List *list)
{
    return (list->len) ? list->el + list->start + list->len - 1 : NULL;
}

Data *list_prev(List *list, Data *d)
{
    assert(list->refs > 0);
    return (d > list->el + list->start) ? d - 1 : NULL;
}

Data *list_elem(List *list, int i)
{
    return list->el + list->start + i;
}

/* This is a horrible abstraction-breaking function.  Call it just after you
 * make a list with list_new(<spaces>).  Then fill in the data slots yourself.
 * Don't manipulate <list> until you're done. */
Data *list_empty_spaces(List *list, int spaces)
{
    list->len += spaces;
    return list->el + list->start + list->len - spaces;
}

int list_search(List *list, Data *data, int offset)
{
    Data *d, *start, *end;

    start = list_first(list) + offset;
    end = list_last(list);
    for (d = list_elem(list, offset); d  && d <= list_last(list); d=list_next(list,d)) {
	if (data_cmp(data, d) == 0)
	    return d - list_first(list);
    }
    return -1;
}

/* Effects: Returns 0 if the lists l1 and l2 are equivalent, or 1 if not. */
int list_cmp(List *l1, List *l2)
{
    int i;

    /* They're obviously the same if they're the same list. */
    if (l1 == l2)
	return 0;

    /* Lists can only be equal if they're of the same length. */
    if (l1->len != l2->len)
	return 1;

    /* See if any elements differ. */
    for (i = 0; i < l1->len; i++) {
	if (data_cmp(&l1->el[l1->start + i], &l2->el[l2->start + i]) != 0)
	    return 1;
    }

    /* No elements differ, so the lists are the same. */
    return 0;
}

/* Effects: Returns -1,0,1 for list order. */
int list_order(List *l1, List *l2)
{
    int i, l;

    /* They're obviously the same if they're the same list. */
    if (l1 == l2)
	return 0;

    /* Lists can only be equal if they're of the same length. */
    if (l1->len >= l2->len)
      l = l2->len;
    else
      l = l1->len;

    /* See if any elements differ. */
    for (i = 0; i < l; i++) {
      int diff;
      diff = data_order(&l1->el[l1->start + i], &l2->el[l2->start + i]);
      if (diff != 0)
	return diff;
    }

    return l1->len - l2->len;
}

/* Error-checking on pos is the job of the calling function. */
List *list_insert(List *list, int pos, Data *elem)
{
    list = prepare_to_modify(list, list->start, list->len + 1);
    pos += list->start;
    MEMMOVE(list->el + pos + 1, list->el + pos, list->len - 1 - pos);
    data_dup(&list->el[pos], elem);
    return list;
}

List *list_add(List *list, Data *elem)
{
    list = prepare_to_modify(list, list->start, list->len + 1);
    data_dup(&list->el[list->start + list->len - 1], elem);
    return list;
}

/* Error-checking on pos is the job of the calling function. */
List *list_replace(List *list, int pos, Data *elem)
{
  /* prepare_to_modify needed here only for multiply referenced lists */
  if (list->refs > 1)
    list = prepare_to_modify(list, list->start, list->len);
  pos += list->start;
  data_discard(&list->el[pos]);
  data_dup(&list->el[pos], elem);
  return list;
}

/* Error-checking on pos is the job of the calling function. */
List *list_delete(List *list, int pos)
{
  /* Special-case deletion of last element. */
  if (pos == list->len - 1)
    return prepare_to_modify(list, list->start, list->len - 1);

  /* prepare_to_modify needed here only for multiply referenced lists */
  if (list->refs > 1)
    list = prepare_to_modify(list, list->start, list->len);

  pos += list->start;
  data_discard(&list->el[pos]);
  list->len--;
  MEMMOVE(list->el + pos, list->el + pos + 1, list->len - pos);

  /* prepare_to_modify needed here only if list has shrunk */
  if (((list->len - list->start) * 4 < list->size)
      && (list->size > STARTING_SIZE))
    list = prepare_to_modify(list, list->start, list->len);

  return list;
}

/* This routine will crash if elem is not in list. */
List *list_delete_element(List *list, Data *elem)
{
    int pos;
    pos = list_search(list, elem, 0);
    assert(pos >= 0);
    return list_delete(list, pos);
}

List *list_append(List *list1, List *list2)
{
    int i;
    Data *p, *q;

    list1 = prepare_to_modify(list1, list1->start, list1->len + list2->len);
    p = list1->el + list1->start + list1->len - list2->len;
    q = list2->el + list2->start;
    for (i = 0; i < list2->len; i++)
	data_dup(&p[i], &q[i]);
    return list1;
}

List *list_reverse(List *list)
{
  Data *d, tmp;
  int i;

  /* prepare_to_modify needed here only for multiply referenced lists */
  if (list->refs > 1)
    list = prepare_to_modify(list, list->start, list->len);

  d = list->el + list->start;
  for (i = 0; i < list->len / 2; i++) {
    tmp = d[i];
    d[i] = d[list->len - i - 1];
    d[list->len - i - 1] = tmp;
  }
  return list;
}

List *list_setadd(List *list, Data *d)
{
    if (list_search(list, d, 0) != -1)
	return list;
    return list_add(list, d);
}

List *list_setremove(List *list, Data *d)
{
    int pos;

    pos = list_search(list, d, 0);
    if (pos == -1)
	return list;
    return list_delete(list, pos);
}

/* convert list to set */
List *list_toset(List *list)
{
  int li, pos;
  Data *d;

  for (li = 0; li < list->len; li++) {
    d = list_elem(list, li);
    pos = li + 1;
    for (pos = list_search(list, d, pos);
	 (pos > 0) && (pos < list->len);
	 pos = list_search(list, d, pos)) {
      list = list_delete(list, pos);
    }
  }
  return list;
}

List *list_union(List *list1, List *list2)
{
    Data *start, *end, *d;

    /* Simplistic O(len1 * len2) implementation for now.  Later, use lengths to
     * decide whether to use a O(len1 + len2) hash table algorithm. */
    start = list2->el + list2->start;
    end = start + list2->len;
    for (d = start; d < end; d++) {
	if (list_search(list1, d, 0) == -1)
	    list1 = list_add(list1, d);
    }
    return list1;
}

/* list_factor
 * returns a list of 3 elements: [elements only of list1, intersection, elements only of list2]
 */
List *list_factor(List *list1, List *list2)
{
    Data *dl1, *inter, *dl2;
    int i, pos;
    List *factor = list_new(3);
    list_empty_spaces(factor, 3);

    dl1 = list_elem(factor, 0);
    dl1->type = LIST;
    dl1->u.list = list_dup(list1);

    inter = list_elem(factor, 1);
    inter->type = LIST;
    inter->u.list = list_new(list1->len + list2->len);

    dl2 = list_elem(factor, 2);
    dl2->type = LIST;
    dl2->u.list = list_dup(list2);

    for (i = dl2->u.list->len; i >= 0; i--) {
      pos = list_search(dl1->u.list, list_elem(dl2->u.list, i), 0);
      if (pos != -1) {
	/* in intersection - move it and remove it */
	inter->u.list = list_add(inter->u.list, list_elem(dl2->u.list, i));
	dl1->u.list = list_delete(dl1->u.list, pos);
	dl2->u.list = list_delete(dl2->u.list, i);
      }
    }
    return factor;
}

List *list_qsort(List *list)
{
  list = prepare_to_modify(list, list->start, list->len);
  qsort(list_elem(list, 0), list->len, sizeof(Data), (int (*)(const void*,const void*))data_order);
  return list;
}

List *list_sublist(List *list, int start, int len)
{
    return prepare_to_modify(list, list->start + start, len);
}

/* Warning: do not discard a list before initializing its data elements. */
void list_discard(List *list)
{
    int i;

    if (!--list->refs) {
	for (i = list->start; i < list->start + list->len; i++)
	    data_discard(&list->el[i]);
	free(list);
    }
}
