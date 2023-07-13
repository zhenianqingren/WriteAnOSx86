#include "ioqueue.h"
#include "debug.h"
#include "interrupt.h"

void ioqueue_init(struct ioqueue *queue)
{
    lock_init(&queue->lock);
    queue->producer = NULL;
    queue->consumer = NULL;
    queue->head = 0;
    queue->tail = 0;
}

static int32_t next(int32_t pos)
{
    return (pos + 1) % BUFSIZ;
}

int ioqueue_full(struct ioqueue *queue)
{
    ASSERT(INTR_OFF == intr_get_status());
    return next(queue->tail) == queue->head;
}

int ioqueue_empty(struct ioqueue *queue)
{
    ASSERT(INTR_OFF == intr_get_status());
    return queue->head == queue->tail;
}

/*使当前生产者或消费者在此缓冲区等待
 */
static void ioqueue_wait(struct task_struct **waiter)
{
    ASSERT(waiter != NULL && *waiter == NULL);
    *waiter = running_thread();
    thread_block(TASK_BLOCKED);
}

/*唤醒waiter
 */
static void wakeup(struct task_struct **waiter)
{
    ASSERT(waiter != NULL && *waiter != NULL);
    thread_unblock(*waiter);
    *waiter = NULL;
}

/*弹出队首元素
 */
char ioqueue_pop(struct ioqueue *queue)
{
    ASSERT(INTR_OFF == intr_get_status()); // 确保原子性
    /*
        若缓冲区为空，将queue的consumer标记为自己
        等待将来被producer唤醒
    */
    while (ioqueue_empty(queue)) // 先同步
    {
        lock_acquire(&queue->lock); // 卡住其他消费者
        ioqueue_wait(&queue->consumer);
        lock_release(&queue->lock);
    }
    char ch = queue->buf[queue->head];
    queue->head = next(queue->head);
    if (queue->producer != NULL)
    {
        wakeup(&queue->producer);
    }
    return ch;
}

void ioqueue_push(struct ioqueue *queue, char byte)
{
    ASSERT(INTR_OFF == intr_get_status()); // 确保原子性
    while (ioqueue_full(queue))
    {
        lock_acquire(&queue->lock); // 卡住其他生产者
        ioqueue_wait(&queue->producer);
        lock_release(&queue->lock);
    }

    queue->buf[queue->tail] = byte;
    queue->tail = next(queue->tail);
    if (queue->consumer != NULL)
    {
        wakeup(&queue->consumer);
    }
}