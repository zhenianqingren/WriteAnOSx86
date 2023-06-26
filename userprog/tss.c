#include "tss.h"
#include "string.h"

struct tss
{
    uint32_t backlink;
    uint32_t *esp0;
    uint32_t ss0;
    uint32_t *esp1;
    uint32_t ss1;
    uint32_t *esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t (*eip)(void);
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint32_t trace;
    uint32_t io_base;
};

static struct tss tss;

// 更新tss中esp0字段的值为pthread的0级栈
void updata_tss_esp(struct task_struct *pthread)
{
    tss.esp0 = (uint32_t *)((uint32_t)pthread + PG_SIZE);
}

// 制作段描述符
struct gdt_desc make_gdt_desc(uint32_t *desc_addr, uint32_t limit, uint8_t attr_low, uint8_t attr_high)
{
    uint32_t desc_base = (uint32_t)desc_addr;
    struct gdt_desc desc;
    desc.limit_low_word = limit & 0xffff;
    desc.base_low_word = desc_base & 0xffff;
    desc.base_mid_byte = (desc_base & (0xff << 16)) >> 8;
    desc.attr_low_byte = (uint8_t)attr_low;
    desc.limit_high_attr_high = (((limit & 0x000f0000) >> 16) + (uint8_t)attr_high);
    desc.base_high_byte = desc_base >> 24;
    return desc;
}

// 在gdt中创建tss并重新加载gdt
void tss_init(void)
{
    put_str("tss init start\n");
    uint32_t tss_siz = sizeof(tss);
    memset((void *)&tss, 0, tss_siz);

    tss.ss0 = SELECTOR_K_STACK;
    tss.io_base = tss_siz;
    // gdt段基址是0x900,则tss段描述符位于0x900+4*8
    *((struct gdt_desc *)0xc0000920) = make_gdt_desc((uint32_t *)&tss, tss_siz - 1, TSS_ATTR_LOW, TSS_ATTR_HIGH);

    // 在gdt中添加DPL为3的数据段和代码段描述符
    *((struct gdt_desc *)0xc0000928) = make_gdt_desc((uint32_t *)0, 0xfffff, GDT_CODE_ATTR_LOW_DPL3, GDT_ATTR_HIGH);

    *((struct gdt_desc *)0xc0000930) = make_gdt_desc((uint32_t *)0, 0xfffff, GDT_DATA_ATTR_LOW_DPL3, GDT_ATTR_HIGH);

    // lgdt操作数 [32bitGDT起始地址][16bit表界限]
    uint64_t gdt_operand = ((uint64_t)(uint32_t)0xc0000900 << 16) | (8 * 7 - 1);

    // 加载GDT
    asm volatile("lgdt %0" ::"m"(gdt_operand));
    // 加载选择子到TR寄存器 选择子索引TSS描述符
    asm volatile("ltr %w0" ::"r"(SELECTOR_TSS));
    put_str("tss init and ltr done\n");
}