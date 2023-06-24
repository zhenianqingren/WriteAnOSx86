#include "list.h"
#include "interrupt.h"
#include "string.h"

// 初始化链表
void list_init(struct list *l)
{
    l->head.prev = NULL;
    l->head.next = &l->tail;
    l->tail.prev = &l->head;
    l->tail.next = NULL;
}

// 头插
void list_insert_before(struct list_elem *before, struct list_elem *e)
{
    enum intr_status old = intr_disable();
    before->prev->next = e;
    e->prev = before->prev;
    before->prev = e;
    e->next = before;
    intr_set_status(old);
}

// push_front
void list_push(struct list *l, struct list_elem *e)
{
    list_insert_before(l->head.next, e);
}

void list_iterate(struct list *l)
{
    
}

// push_back
void list_append(struct list *l, struct list_elem *e)
{
    list_insert_before(&l->tail, e);
}

// 从链表中移除e
void list_remove(struct list_elem *e)
{
    enum intr_status old = intr_disable();
    e->prev->next = e->next;
    e->next->prev = e->prev;
    intr_set_status(old);
}

// pop_front
struct list_elem *list_pop(struct list *l)
{
    struct list_elem *top = l->head.next;
    list_remove(top);
    return top;
}

int list_empty(struct list *l)
{
    return l->head.next == &l->tail ? 1 : 0;
}

uint32_t list_len(struct list *l)
{
    struct list_elem *e = l->head.next;
    uint32_t len = 0;
    while (e != &l->tail)
    {
        len++;
        e = e->next;
    }
    return len;
}

// 将每个元素和arg传递给func
// arg指明判断单个元素是否符合条件
// 找到符合条件的元素返回元素指针
struct list_elem *list_traversal(struct list *l, function func, int arg)
{
    if (list_empty(l))
    {
        return NULL;
    }

    struct list_elem *e = l->head.next;
    while (e != &l->tail)
    {
        if (func(e, arg))
        {
            return e;
        }
        e = e->next;
    }
    return NULL;
}

// 在链表l中找到obj
int elem_find(struct list *l, struct list_elem *obj)
{
    struct list_elem *e = l->head.next;
    while (e != &l->tail)
    {
        if (e == obj)
            return 1;
        e = e->next;
    }
    return 0;
}