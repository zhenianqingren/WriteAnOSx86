#include "debug.h"
#include "print.h"
#include "interrupt.h"

void panic_spin(char *fn, int line, const char *func, const char *condition)
{
    intr_disable();

    put_str("\n\nPANIC !!!\n\n");

    put_str("filename: ");
    put_str(fn);
    put_char('\n');

    put_str("line: 0x");
    put_int(line);
    put_char('\n');

    put_str("function: ");
    put_str(func);
    put_char('\n');

    put_str("condition: ");
    put_str(condition);
    put_char('\n');

    while (1)
        ;
}
