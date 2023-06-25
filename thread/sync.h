#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H

#include "stdint.h"
#include "list.h"
#include "thread.h"
#define NULL 0

struct semaphore
{
    uint8_t value;
    struct list waiters;
};

struct lock
{
    struct task_struct *holder; // 锁的持有者
    struct semaphore semaphore; // 用二元信号量实现锁
    uint32_t holder_repeat_nr;  // 锁的持有者重复申请锁的次数
};

void sema_init(struct semaphore *sema, uint8_t value);
void lock_init(struct lock *plock);
void sema_down(struct semaphore *sema);
void sema_up(struct semaphore *sema);
void lock_acquire(struct lock *plock);
void lock_release(struct lock *plock);
#endif