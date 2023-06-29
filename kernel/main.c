#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "interrupt.h"
#include "ioqueue.h"
#include "process.h"
#include "../lib/user/syscall.h"
#include "../lib/stdio.h"

void k_thread_a(void *arg);
void k_thread_b(void *arg);
void user_process_a(void);
void user_process_b(void);
int test_a = 0, test_b = 0;

int main(void)
{
    put_str("kernel begin\n");
    init_all();

    intr_enable();
    process_execute(user_process_a, "user_process_a");
    process_execute(user_process_b, "user_process_b");
    // thread_start("k_thread_a", 31, k_thread_a, "argA");
    // thread_start("k_thread_b", 31, k_thread_b, "argB");
    while (1)
        ;

    return 0;
}

void k_thread_a(void *arg)
{

    void *addr1 = sys_malloc(256);
    void *addr2 = sys_malloc(255);
    void *addr3 = sys_malloc(254);
    console_put_str(" thread_a malloc addr:0x");
    console_put_int((int)addr1);
    console_put_char(',');
    console_put_int((int)addr2);
    console_put_char(',');
    console_put_int((int)addr3);
    console_put_char('\n');

    sys_free(addr1);
    sys_free(addr2);
    sys_free(addr3);
    while (1)
        ;
}

void k_thread_b(void *arg)
{
    void *addr1 = sys_malloc(256);
    void *addr2 = sys_malloc(255);
    void *addr3 = sys_malloc(254);
    console_put_str(" thread_b malloc addr:0x");
    console_put_int((int)addr1);
    console_put_char(',');
    console_put_int((int)addr2);
    console_put_char(',');
    console_put_int((int)addr3);
    console_put_char('\n');

    sys_free(addr1);
    sys_free(addr2);
    sys_free(addr3);
    while (1)
        ;
}

void user_process_a(void)
{
    void *addr1 = malloc(256);
    void *addr2 = malloc(2048);
    void *addr3 = malloc(1024);
    printf("a malloc addr: 0x%x 0x%x 0x%x\n", (uint32_t)addr1, (uint32_t)addr2, (uint32_t)addr3);
    int delay = 10000;
    while (delay-- > 0)
        ;
    free(addr1);
    free(addr2);
    free(addr3);
    while (1)
        ;
}

void user_process_b(void)
{
    void *addr1 = malloc(256);
    void *addr2 = malloc(255);
    void *addr3 = malloc(254);
    printf("b malloc addr: %x %x %x\n", (uint32_t)addr1, (uint32_t)addr2, (uint32_t)addr3);
    int delay = 10000;
    while (delay-- > 0)
        ;
    free(addr1);
    free(addr2);
    free(addr3);
    while (1)
        ;
}