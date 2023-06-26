#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "interrupt.h"
#include "ioqueue.h"
#include "process.h"

void k_thread(void *arg);
void user_process_a(void);
void user_process_b(void);
int test_a = 0, test_b = 0;

int main(void)
{
    put_str("kernel begin\n");
    init_all();

    thread_start("k_thread_a", 31, k_thread, "argA ");
    thread_start("k_thread_b", 31, k_thread, "argB ");
    process_execute(user_process_a, "user_proc_a");
    process_execute(user_process_b, "user_proc_b");
    intr_enable();
    while (1)
        ;

    return 0;
}

void k_thread(void *arg)
{
    char *para = arg;
    while (1)
    {
        enum intr_status old = intr_disable();
        if (!ioqueue_empty(&kbd_buf))
        {
            console_put_str(para);
            console_put_char(ioqueue_pop(&kbd_buf));
            console_put_char('\n');
        }
        intr_set_status(old);
    }
}

void user_process_a(void)
{
    while (1)
    {
        test_a++;
    }
}

void user_process_b(void)
{
    while (1)
    {
        test_b++;
    }
}