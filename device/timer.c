#include "timer.h"
#include "io.h"
#include "print.h"
#include "thread.h"
#include "debug.h"
#include "interrupt.h"

#define IRQ0_FREQUENCY 100
#define INPUT_FREQUENCY 1193180
#define COUNTER0_VALUE INPUT_FREQUENCY / IRQ0_FREQUENCY
#define COUNTER0_PORT 0X40
#define COUNTER0_NO 0
#define COUNTER_MODE 2
#define READ_WRITE_LATCH 3
#define PIT_COUNTROL_PORT 0x43

uint32_t ticks; // 内核中断开启以来总共的滴答数
/*将待操作计数器counter_no、读写锁属性rwl、计数器模式counter_mode写入模式控制寄存器并赋予初值counter_value*/
static void
frequency_set(uint8_t counter_port,
              uint8_t counter_no,
              uint8_t rwl,
              uint8_t counter_mode,
              uint16_t counter_value)
{
    /*向控制字寄存器端口0x43写入控制字*/
    outb(PIT_COUNTROL_PORT,
         (uint8_t)(counter_no << 6 | rwl << 4 | counter_mode << 1));
    /*先写入counter_value低8bit*/
    outb(counter_port, (uint8_t)counter_value);
    /*再写入counter_value的高8bit*/
    outb(counter_port, (uint8_t)(counter_value >> 8));
}

static void intr_timer_handler(void)
{
    struct task_struct *cur = running_thread();
    ASSERT(cur->stack_magic == T_MAGIC);
    cur->elapsed_ticks++;
    ticks++;
    if (cur->ticks == 0)
    {
        scheduler();
    }
    else
    {
        cur->ticks--;
    }
}

void timer_init()
{
    put_str("timer_init start\n");
    frequency_set(COUNTER0_PORT,
                  COUNTER0_NO,
                  READ_WRITE_LATCH,
                  COUNTER_MODE,
                  COUNTER0_VALUE);
    register_handler(0x20, intr_timer_handler);
    put_str("timer_init done\n");
}
