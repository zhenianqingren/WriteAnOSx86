#include "syscall_init.h"
#include "thread.h"
#include "../lib/user/syscall.h"
#include "console.h"
#include "memory.h"

#define syscall_nr 32
typedef void *syscall;
syscall syscall_table[syscall_nr];

// 返回当前任务pid
uint32_t sys_getpid(void)
{
    return running_thread()->pid;
}

// 未实现文件系统前的write版本
uint32_t sys_write(char *str)
{
    console_put_str(str);
    return strlen(str);
}

// 初始化系统调用
void syscall_init(void)
{
    put_str("syscall init begin\n");
    syscall_table[SYS_GETPID] = sys_getpid;
    syscall_table[SYS_WRITE] = sys_write;
    syscall_table[SYS_MALLOC] = sys_malloc;
    syscall_table[SYS_FREE] = sys_free;
    put_str("syscall init end\n");
}