#ifndef __DEVICE_IOQUEUE_H
#define __DEVICE_IOQUEUE_H

#include "stdint.h"
#include "thread.h"
#include "sync.h"

#define BUFSIZ 64
extern struct ioqueue kbd_buf;
/*环形队列 实际容量63*/
struct ioqueue
{
    // 生产者消费者
    struct lock lock;
    struct task_struct *producer;
    struct task_struct *consumer;
    char buf[BUFSIZ];
    int32_t head;
    int32_t tail;
};

extern struct ioqueue kbd_buf;
void ioqueue_init(struct ioqueue *queue);
int ioqueue_full(struct ioqueue *queue);
char ioqueue_pop(struct ioqueue *queue);
void ioqueue_push(struct ioqueue *queue, char byte);
int ioqueue_empty(struct ioqueue *queue);

#endif