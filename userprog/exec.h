#ifndef __USERPROG__EXEC_H
#define __USERPROG__EXEC_H

#include "../stdint.h"

int32_t sys_execv(const char *path, const char *argv[]);

#endif