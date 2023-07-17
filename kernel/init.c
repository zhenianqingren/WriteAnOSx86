#include "print.h"
#include "interrupt.h"
#include "timer.h"
#include "memory.h"
#include "console.h"
#include "keyboard.h"
#include "tss.h"
#include "syscall_init.h"
#include "ide.h"
#include "fs.h"
#include "process.h"

extern void init(void);
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
    tss_init();
    syscall_init();
    intr_enable();
    ide_init();
    filesys_init();
    uint32_t siz = 4031;
    uint32_t sec_cnt = DIV_ROUND_UP(siz, SECTOR_SIZE);
    struct disk *sda = &channels[0].devices[0];
    void *buf = sys_malloc(sec_cnt * SECTOR_SIZE);
    ide_read(sda, 300, buf, sec_cnt);
    int32_t fd = sys_open("/cat", O_CREATE | O_RDWR);
    if (fd != -1)
    {
        if (sys_write(fd, buf, siz) == -1)
        {
            printk("file write error!\n");
        }
        sys_close(fd);
    }

    siz = 463;
    sec_cnt = DIV_ROUND_UP(siz, SECTOR_SIZE);
    ide_read(sda, 500, buf, sec_cnt);
    fd = sys_open("/cat.c", O_CREATE | O_RDWR);
    if (fd != -1)
    {
        if (sys_write(fd, buf, siz) == -1)
        {
            printk("file write error!\n");
        }
        sys_close(fd);
    }

    process_execute(init, "init");
}