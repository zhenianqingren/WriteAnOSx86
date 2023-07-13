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

int32_t write(int32_t fd, const void *buf, uint32_t cnt)
{
    return _syscall3(SYS_WRITE, fd, buf, cnt);
}

int32_t read(int32_t fd, void *buf, uint32_t count)
{
    return _syscall3(SYS_READ, fd, buf, count);
}

int32_t lseek(int32_t fd, int32_t offset, uint8_t whence)
{
    return _syscall3(SYS_LSEEK, fd, offset, whence);
}

int32_t open(const char *pathname, uint8_t flags)
{
    return _syscall2(SYS_OPEN, pathname, flags);
}

int32_t close(int32_t fd)
{
    return _syscall1(SYS_CLOSE, fd);
}

int32_t unlink(const char *pathname)
{
    return _syscall1(SYS_UNLINK, pathname);
}

int32_t mkdir(const char *dn)
{
    return _syscall1(SYS_MKDIR, dn);
}

struct dir *opendir(const char *name)
{
    return _syscall1(SYS_OPENDIR, name);
}

int32_t closedir(struct dir *dir)
{
    return _syscall1(SYS_CLOSEDIR, dir);
}

struct dir_entry *readdir(struct dir *dir)
{
    return _syscall1(SYS_READDIR, dir);
}

pid_t fork(void)
{
    return _syscall0(SYS_FORK);
}

void putchar(char ch)
{
    _syscall1(SYS_PUTCHAR, ch);
}

void clear(void)
{
    _syscall0(SYS_CLEAR);
}

void rewinddir(struct dir *dir)
{
    _syscall1(SYS_REWINDDIR, dir);
}

int32_t chdir(const char *path)
{
    return _syscall1(SYS_CHDIR, path);
}

int32_t stat(const char *path, struct stat *buf)
{
    return _syscall2(SYS_STAT, path, buf);
}

int32_t rmdir(const char *pathname)
{
    _syscall1(SYS_RMDIR, pathname);
}

void *malloc(uint32_t size)
{
    return (void *)_syscall1(SYS_MALLOC, size);
}

void free(void *ptr)
{
    _syscall1(SYS_FREE, ptr);
}