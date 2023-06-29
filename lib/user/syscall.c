#include "syscall.h"

// 无参数系统调用 NUMBER是系统调用号
#define _syscall0(NUMBER)          \
    ({                             \
        uint32_t ret;              \
        asm volatile("int $0x80"   \
                     : "=a"(ret)   \
                     : "a"(NUMBER) \
                     : "memory");  \
        ret;                       \
    })
// 大括号中最后一个语句的值会作为代码块的返回值

// 一个参数的系统调用
#define _syscall1(NUMBER, ARG)               \
    ({                                       \
        int ret;                             \
        asm volatile("int $0x80"             \
                     : "=a"(ret)             \
                     : "a"(NUMBER), "b"(ARG) \
                     : "memory");            \
        ret;                                 \
    })

// 两个参数的系统调用
// 三个参数的系统调用
#define _syscall2(NUMBER, ARG1, ARG2)                    \
    ({                                                   \
        int ret;                                         \
        asm volatile("int $0x80"                         \
                     : "=a"(ret)                         \
                     : "a"(NUMBER), "b"(ARG1), "c"(ARG2) \
                     : "memory");                        \
        ret;                                             \
    })

// 三个参数的系统调用
#define _syscall3(NUMBER, ARG1, ARG2, ARG3)                         \
    ({                                                              \
        int ret;                                                    \
        asm volatile("int $0x80"                                    \
                     : "=a"(ret)                                    \
                     : "a"(NUMBER), "b"(ARG1), "c"(ARG2), "d"(ARG3) \
                     : "memory");                                   \
        ret;                                                        \
    })

uint32_t getpid()
{
    return _syscall0(SYS_GETPID);
}

uint32_t write(char *str)
{
    return _syscall1(SYS_WRITE, str);
}

void *malloc(uint32_t size)
{
    return (void *)_syscall1(SYS_MALLOC, size);
}

void free(void *ptr)
{
    _syscall1(SYS_FREE, ptr);
}