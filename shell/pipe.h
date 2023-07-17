#ifndef __SHELL__PIPE_H
#define __SHELL__PIPE_H

#include "../lib/stdint.h"
#include "global.h"
#include "ioqueue.h"

bool ispipe(uint32_t locl_fd);
int32_t sys_pipe(int32_t pipefd[2]);
uint32_t pipe_read(int32_t fd, void *buf, uint32_t cnt);
uint32_t pipe_write(int32_t fd, const void *buf, uint32_t cnt);
#endif