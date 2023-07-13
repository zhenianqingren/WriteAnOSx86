#ifndef __USERPROG_SYSCALL_INIT_H
#define __USERPROG_SYSCALL_INIT_H
#include "stdint.h"

uint32_t sys_getpid(void);
void sys_putchar(char ch);
void sys_clear(void);
void syscall_init(void);

#endif
