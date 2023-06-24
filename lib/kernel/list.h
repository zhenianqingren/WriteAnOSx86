#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H

#include "stdint.h"

#define offset(type, member) (uint32_t)(&((type *)0)->member)
#define elem2entry(type, member, eptr) \
    (type *)((uint32_t)eptr - offset(type, member))

/*
    链表节点
*/
struct list_elem
{
    struct list_elem *prev;
    struct list_elem *next;
};

/*
    链表队列
*/
struct list
{
    struct list_elem head;
    struct list_elem tail;
};

typedef int(function)(struct list_elem *, int arg);

void list_init(struct list *);
void list_insert_before(struct list_elem *before, struct list_elem *e);
void list_push(struct list *l, struct list_elem *e);
void list_iterate(struct list *l);
void list_append(struct list *l, struct list_elem *e);
void list_remove(struct list_elem *e);
struct list_elem *list_pop(struct list *l);
int list_empty(struct list *l);
uint32_t list_len(struct list *l);
struct list_elem *list_traversal(struct list *l, function func, int arg);
int elem_find(struct list *l, struct list_elem *obj);

#endif