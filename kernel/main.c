#include "print.h"
#include "init.h"

int main(void)
{
    put_str("kernel begin\n");
    init_all();
    asm volatile("sti");
    while (1)
        ;

    return 0;
}