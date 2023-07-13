#ifndef __USERPROG__FORK_H
#define __USERPROG__FORK_H

#include "../lib/stdint.h"
#include "thread.h"

pid_t sys_fork(void);

#endif