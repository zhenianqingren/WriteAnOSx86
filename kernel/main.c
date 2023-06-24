#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"

void k_thread_a(void *arg);
void k_thread_b(void *arg);

int main(void)
{
    put_str("kernel begin\n");
    init_all();

    thread_start("k_thread_a", 31, k_thread_a, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    thread_start("k_thread_b", 8, k_thread_b, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    intr_enable();
    while (1)
    {
        put_str("mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm");
    }

    return 0;
}

void k_thread_a(void *arg)
{
    char *para = arg;
    while (1)
    {
        put_str(para);
    }
}

void k_thread_b(void *arg)
{
    char *para = arg;
    while (1)
    {
        put_str(para);
    }
}