#ifndef __BUILDIN_CMD_H
#define __BUILDIN_CMD_H

#define BUILDIN_CNT 8
#define PWD 0
#define CD 1
#define LS 2
#define PS 3
#define CLEAR 4
#define MKDIR 5
#define RMDIR 6
#define RM 7

#include "../lib/stdint.h"

typedef void *(*entry_point)(uint32_t, char **);

void make_clear_abs_path(char *path, char *final);
void *buildin_pwd(uint32_t argc, char **argv);
void *buildin_cd(uint32_t argc, char **argv);
void *buildin_ls(uint32_t argc, char **argv);
void *buildin_ps(uint32_t argc, char **argv);
void *buildin_clear(uint32_t argc, char **argv);
void *buildin_mkdir(uint32_t argc, char **argv);
void *buildin_rmdir(uint32_t argc, char **argv);
void *buildin_rm(uint32_t argc, char **argv);
#endif