#ifndef __USERPROG__PROCESS_CTL_H
#define __USERPROG__PROCESS_CTL_H

#include "global.h"
#include "../lib/stdint.h"

pid_t sys_wait(int16_t *status);
void sys_exit(int32_t status);

#endif