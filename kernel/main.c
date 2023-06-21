#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"

int main(void)
{
    put_str("kernel begin\n");
    init_all();

    void *addr = get_kernel_pages(3);
    put_str("\nget kernel start vaddr is: 0x");
    put_int((uint32_t)addr);
    put_char('\n');

    while (1)
        ;

    return 0;
}