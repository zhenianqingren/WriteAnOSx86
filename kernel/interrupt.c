#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "print.h"

#define IDT_DESC_CNT 0x81
#define PIC_M_CTRL 0x20 /*主片控制端口  ICW1 OCW2 OCW3*/
#define PIC_M_DATA 0x21 /*主片数据端口  ICW2 ICW3 ICW4 OCW1*/
#define PIC_S_CTRL 0xa0 /*从片控制端口  ICW1 OCW2 OCW3*/
#define PIC_S_DATA 0xa1 /*从片数据端口  ICW2 ICW3 ICW4 OCW1*/

#define EFLAGS_IF 0x200
#define GET_EFLAGS(EFLAG_VAR) asm volatile("pushfl;popl %0" \
                                           : "=g"(EFLAG_VAR))

extern uint32_t syscall_handler(void);

enum intr_status intr_get_status()
{
    uint32_t eflags = 0;
    GET_EFLAGS(eflags);
    return (EFLAGS_IF & eflags) ? INTR_ON : INTR_OFF;
}

enum intr_status intr_set_status(enum intr_status status)
{
    return status & INTR_ON ? intr_enable() : intr_disable();
}

/*开中断并返回中断前的状态*/
enum intr_status intr_enable(void)
{
    if (intr_get_status() == INTR_ON)
    {
        return INTR_ON;
    }
    else
    {
        asm volatile("sti");
        return INTR_OFF;
    }
}

/*关中断并返回中断前的状态*/
enum intr_status intr_disable(void)
{
    if (intr_get_status() == INTR_ON)
    {
        asm volatile("cli"
                     :
                     :
                     : "memory");
        return INTR_ON;
    }
    else
    {
        return INTR_OFF;
    }
}

/*中断门描述符结构体*/
struct gate_desc
{
    uint16_t func_offset_low_word;
    uint16_t selector;
    uint8_t dcount; /*未使用*/
    uint8_t attribute;
    uint16_t func_offset_high_word;
};

static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function);
static struct gate_desc idt[IDT_DESC_CNT];

char *intr_name[IDT_DESC_CNT];
intr_handler idt_table[IDT_DESC_CNT];
extern intr_handler intr_entry_table[IDT_DESC_CNT];

static void pic_init(void)
{
    /*初始化主片*/
    outb(PIC_M_CTRL, 0x11); // ICW1: 边沿触发，级联，需要ICW4
    outb(PIC_M_DATA, 0x20); // ICW2: 起始中断向量号为0x20
    outb(PIC_M_DATA, 0x04); // ICW3: IR2接从片
    outb(PIC_M_DATA, 0x01); // ICW4: 8086模式，正常EOI

    /*初始化从片*/
    outb(PIC_S_CTRL, 0x11); // ICW1: 边沿触发，级联，需要ICW4
    outb(PIC_S_DATA, 0x28); // ICW2: 起始中断向量号为0x28
    outb(PIC_S_DATA, 0x02); // ICW3: 设置从片连接到主片的IR2引脚
    outb(PIC_S_DATA, 0x01); // ICW4: 8086模式，正常EOI

    // 打开主片IR0，IRQ0的时钟，IRQ1的键盘和级联从片的IRQ2
    outb(PIC_M_DATA, 0xf8); // 主
    // 打开从片IRQ14，接收硬盘控制器的中断
    outb(PIC_S_DATA, 0xbf); // 从

    // 在中断处理程序中，如果中断源来自从片8259A，发送中断结束信号EOI的时候主片从片都要发送

    put_str("   pic_init done\n");
}

static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function)
{
    p_gdesc->func_offset_low_word = (uint32_t)function & 0x0000ffff;
    p_gdesc->selector = SELECTOR_K_CODE;
    p_gdesc->dcount = 0;
    p_gdesc->attribute = attr;
    p_gdesc->func_offset_high_word = ((uint32_t)function & 0xffff0000) >> 16;
}

static void general_intr_handler(uint8_t vec_nr)
{
    if (vec_nr == 0x27 || vec_nr == 0x2f)
    {
        // IRQ7和IRQ15产生伪中断，无需处理
        // 0x2f是从片最后一个IRQ引脚，保留项
    }

    // 光标置为0，在屏幕左上角清出一片打印异常信息的区域，方便阅读
    set_cursor(0);
    int cursor_pos = 0;
    while (cursor_pos < 320)
    {
        put_char(' ');
        cursor_pos++;
    }
    set_cursor(0);
    put_str("--------exception message start--------\n");
    set_cursor(88); // 从第二行第8个字符开始打印
    put_str(intr_name[vec_nr]);
    if (vec_nr == 14)
    {
        // Page Fault 将缺失地址打印并悬停
        uint32_t page_fault_vaddr = 0;
        asm("movl %%cr2,%0"
            : "=r"(page_fault_vaddr));
        put_str("\npage fault addr is: 0x");
        put_int(page_fault_vaddr);
    }
    put_str("--------exception message end--------\n");
    while (1)
        ;

    put_str("int vector : 0x");
    put_int(vec_nr);
    put_char('\n');

    return;
}

void register_handler(uint8_t vec_no, intr_handler func)
{
    idt_table[vec_no] = func;
}

static void exception_init(void)
{
    int i;
    for (i = 0; i < IDT_DESC_CNT; ++i)
    {
        idt_table[i] = general_intr_handler;
        intr_name[i] = "unknown";
    }

    intr_name[0] = "#DE Divide Error";
    intr_name[1] = "#DB Debug Exception";
    intr_name[2] = "NMI Interrupt";
    intr_name[3] = "#BP Breakpoint Exception";
    intr_name[4] = "#OF Overflow Exception";
    intr_name[5] = "#BR BOUND Range Exceeded Exception";
    intr_name[6] = "#UD Invalid Opcode Exception";
    intr_name[7] = "#NM Device Not Available Exception";
    intr_name[8] = "#DF Double Fault Exception";
    intr_name[9] = "Coprocessor Segment Overrun";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present";
    intr_name[12] = "#SS Stack Fault Exception";
    intr_name[13] = "#GP General Protection Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    // intr_name[15] 第15项是intel保留项，未使用
    intr_name[16] = "#MF x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine-Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
}

static void idt_desc_init(void)
{
    int i;
    for (i = 0; i < IDT_DESC_CNT; ++i)
        make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);

    // 单独处理系统调用中断
    make_idt_desc(&idt[IDT_DESC_CNT - 1], IDT_DESC_ATTR_DPL3, syscall_handler);
    put_str("   idt_desc_init done\n");
}

void idt_init()
{
    put_str("idt init start\n");
    idt_desc_init();
    exception_init();
    pic_init();

    /*加载idt*/
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
    asm volatile("lidt %0" ::"m"(idt_operand)); /*内存约束是传递的C变量的指针给汇编指令当作操作数*/
    put_str("idt init done\n");
}