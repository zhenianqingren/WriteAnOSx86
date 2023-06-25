#include "sync.h"
#include "interrupt.h"
#include "debug.h"
#include "thread.h"

void sema_init(struct semaphore *sema, uint8_t value)
{
    sema->value = value;
    list_init(&sema->waiters);
}

void lock_init(struct lock *plock)
{
    sema_init(&plock->semaphore, 1); // 信号量初值为1
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
}

void sema_down(struct semaphore *sema)
{
    enum intr_status old = intr_disable();
    struct task_struct *cur = running_thread();
    while (sema->value == 0)
    {
        ASSERT(!elem_find(&sema->waiters, &cur->general_tag));
        if (elem_find(&sema->waiters, &cur->general_tag))
        {
            PANIC("sema_down: thread blocked has been in waiters_list\n");
        }
        list_append(&sema->waiters, &cur->general_tag);
        thread_block(TASK_BLOCKED);
    }
    sema->value--;
    ASSERT(sema->value == 0);
    intr_set_status(old);
}

void sema_up(struct semaphore *sema)
{
    enum intr_status old = intr_disable();
    ASSERT(sema->value == 0);
    if (!list_empty(&sema->waiters))
    {
        struct task_struct *pthread = elem2entry(struct task_struct, general_tag, list_pop(&sema->waiters));
        thread_unblock(pthread);
    }
    sema->value++;
    ASSERT(sema->value == 1);
    intr_set_status(old);
}

void lock_acquire(struct lock *plock)
{
    struct task_struct *cur = running_thread();
    /*
     *排除自己已经持有锁但还未释放的情况
     */
    if (plock->holder != cur)
    {
        sema_down(&plock->semaphore);
        plock->holder = cur;
        ASSERT(plock->holder_repeat_nr == 0);
        plock->holder_repeat_nr = 1;
    }
    else
    {
        plock->holder_repeat_nr++;
    }
}

void lock_release(struct lock *plock)
{
    ASSERT(plock->holder == running_thread());
    if (plock->holder_repeat_nr > 1)
    {
        plock->holder_repeat_nr--;
        return;
    }
    ASSERT(plock->holder_repeat_nr == 1);
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_up(&plock->semaphore);
}