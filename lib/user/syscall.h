#ifndef __LIB_USER_SCSCALL_H
#define __LIB_USER_SCSCALL_H
#include "../stdint.h"
#include "../../fs/dir.h"
enum SYSCALL_NR
{
    SYS_GETPID,
    SYS_WRITE,
    SYS_MALLOC,
    SYS_FREE,
    SYS_OPEN,
    SYS_CLOSE,
    SYS_READ,
    SYS_LSEEK,
    SYS_UNLINK,
    SYS_MKDIR,
    SYS_OPENDIR,
    SYS_CLOSEDIR,
    SYS_READDIR,
    SYS_REWINDDIR,
    SYS_RMDIR,
    SYS_CHDIR,
    SYS_STAT,
    SYS_FORK,
    SYS_CLEAR,
    SYS_PUTCHAR,
    SYS_PS,
    SYS_GETCWD,
    SYS_EXECV,
    SYS_WAIT,
    SYS_EXIT,
    SYS_PIPE
};

uint32_t getpid(void);
int32_t write(int32_t fd, const void *buf, uint32_t cnt);
int32_t read(int32_t fd, void *buf, uint32_t count);
int32_t open(const char *pathname, uint8_t flags);
int32_t close(int32_t fd);
int32_t lseek(int32_t fd, int32_t offset, uint8_t whence);
int32_t unlink(const char *pathname);
struct dir *opendir(const char *name);
int32_t closedir(struct dir *dir);
struct dir_entry *readdir(struct dir *dir);
void rewinddir(struct dir *dir);
int32_t chdir(const char *path);
int32_t fstat(const char *path, struct stat *buf);
int32_t rmdir(const char *pathname);
void putchar(char ch);
void clear(void);
void *malloc(uint32_t size);
void free(void *);
pid_t fork(void);
char *getcwd(char *buf, uint32_t size);
void ps(void);
int32_t mkdir(const char *dn);
int32_t execv(const char *path, const char *argv[]);
pid_t wait(int16_t *status);
void exit(int32_t status);
int32_t pipe(int32_t pipefd[2]);
#endif