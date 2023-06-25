#include "print.h"
#include "interrupt.h"
#include "timer.h"
#include "memory.h"
#include "console.h"
#include "keyboard.h"

/*初始化所有模块*/
void init_all()
{
    put_str("init_all\n");
    idt_init();
    timer_init();
    mem_init();
    thread_init();
    console_init();
    keyboard_init();
}