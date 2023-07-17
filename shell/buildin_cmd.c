#include "buildin_cmd.h"
#include "debug.h"
#include "global.h"
#include "file.h"
#include "dir.h"
#include "fs.h"
#include "../lib/string.h"
#include "../lib/user/syscall.h"
#include "../lib/stdio.h"

// 将路径old_abs_path中的.和..转换为实际路径后存入new_abs_path
static void wash_path(char *old_abs_path, char *new_abs_path)
{
    ASSERT(old_abs_path[0] == '/');
    char name[MAX_FN_LEN] = {0};
    char *sub_path = old_abs_path;
    sub_path = path_parse((const char *)sub_path, name);
    if (name[0] == 0)
    {
        // 若只键入了"/",直接将"/"存入new_abs_path后返回
        new_abs_path[0] = '/';
        new_abs_path[1] = 0;
        return;
    }
    new_abs_path[0] = 0; // 避免缓冲区不干净
    strcat(new_abs_path, "/");
    while (name[0])
    {
        if (!strcmp("..", name))
        {
            char *slash_ptr = strrchr((const char *)new_abs_path, '/');
            if (slash_ptr != new_abs_path)
            {
                // 如new_abs_path为/a/b 变为/a
                *slash_ptr = 0;
            }
            else
            {
                // /..变为/
                *(slash_ptr + 1) = 0;
            }
        }
        else if (strcmp(".", name))
        {
            if (strcmp(new_abs_path, "/"))
            {
                strcat(new_abs_path, "/");
            }
            strcat(new_abs_path, name);
        }
        memset(name, 0, MAX_FN_LEN);
        if (sub_path)
        {
            sub_path = path_parse((const char *)sub_path, name);
        }
    }
}

void make_clear_abs_path(char *path, char *final)
{
    char abs[MAX_FN_LEN];
    memset(abs, 0, MAX_FN_LEN);
    if (path[0] != '/')
    {
        // 如果不是相对路径
        if (getcwd(abs, MAX_FN_LEN) != NULL)
        {
            if (!((abs[0] == '/') && (abs[1] == 0)))
            {
                strcat(abs, "/");
            }
        }
    }
    strcat(abs, path);
    wash_path(abs, final);
}

// pwd命令的内建函数
void *buildin_pwd(uint32_t argc, char **argv)
{
    if (argc == -1)
    {
        printf("pwd: no argument support!\n");
        return (void *)-1;
    }
    else
    {
        char cleaned_path[32];
        memset(cleaned_path, 0, 32);
        if (getcwd(cleaned_path, 32) != NULL)
        {
            printf("%s\n", cleaned_path);
        }
        else
        {
            printf("getcwd: current work directory failed!\n");
            return (void *)-1;
        }
    }
    return (void *)0;
}

extern char final[32];
// cd命令的内建函数
void *buildin_cd(uint32_t argc, char **argv)
{
    if (argc != 2)
    {
        printf("cd: the count of arguments not supported!\n");
        return (void *)0;
    }
    make_clear_abs_path(argv[1], final);
    if (chdir(final) == -1)
    {
        printf("cd: no such file or directory!\n");
        return (void *)0;
    }
    return (void *) final;
}

// // ls内建命令
void *buildin_ls(uint32_t argc, char **argv)
{
    char *pathname = NULL;
    struct stat stat;
    memset(&stat, 0, sizeof(struct stat));

    char cleaned_path[32];
    memset(cleaned_path, 0, 32);
    if (argc == 1)
    {
        if (getcwd(cleaned_path, 32) != NULL)
        {
            pathname = cleaned_path;
        }
        else
        {
            printf("ls: getcwd for default path failed!\n");
            return (void *)-1;
        }
    }
    else
    {
        make_clear_abs_path(argv[1], cleaned_path);
        pathname = cleaned_path;
    }

    if (fstat(pathname, &stat) == -1)
    {
        printf("ls: cannot access %s:No such file or directory!\n", pathname);
        return (void *)-1;
    }

    if (stat.st_ftype == FT_DIRECTORY)
    {
        struct dir *dir = opendir(pathname);
        struct dir_entry *dire = NULL;
        char sub[32];
        memset(sub, 0, 32);

        uint32_t len = strlen(pathname);
        uint32_t last = len - 1;
        memcpy(sub, pathname, len);
        if (sub[last] != '/')
        {
            last++;
            sub[last] = '/';
        }
        rewinddir(dir);
        while ((dire = readdir(dir)) != NULL)
        {
            memset(&stat, 0, sizeof(struct stat));
            sub[last + 1] = 0;
            strcat(sub, dire->fn);
            fstat(sub, &stat);
            printf("-%c-%d-%d-%s\n", stat.st_ftype == FT_DIRECTORY ? 'd' : 'f', stat.st_ino, stat.st_size, sub);
        }
    }
    else
    {
        printf("-%c-%d-%d-%s\n", 'f', stat.st_ino, stat.st_size, pathname);
    }

    return (void *)0;
}

// ps内建命令
void *buildin_ps(uint32_t argc, char **argv)
{
    if (argc != 1)
    {
        printf("ps: no arguments support!\n");
        return (void *)-1;
    }
    ps();
    return (void *)0;
}

// // clear命令内建函数
void *buildin_clear(uint32_t argc, char **argv)
{
    if (argc != 1)
    {
        printf("clear: no arguments support!\n");
        return (void *)-1;
    }
    clear();
    return (void *)0;
}

// mkdir命令内建函数
void *buildin_mkdir(uint32_t argc, char **argv)
{
    if (argc != 2)
    {
        printf("mkdir: no arguments support!\n");
        return (void *)-1;
    }
    else
    {
        char cleaned_path[32];
        memset(cleaned_path, 0, 32);
        make_clear_abs_path(argv[1], cleaned_path);
        if (strcmp("/", cleaned_path))
        {
            if (mkdir(cleaned_path) == 0)
            {
                return (void *)0;
            }
            else
            {
                printf("mkdir:failed %s\n", cleaned_path);
            }
        }
    }
    return (void *)-1;
}

void *buildin_rmdir(uint32_t argc, char **argv)
{
    if (argc != 2)
    {
        printf("rmdir: no arguments support!\n");
        return (void *)-1;
    }
    else
    {
        char cleaned_path[32];
        memset(cleaned_path, 0, 32);
        make_clear_abs_path(argv[1], cleaned_path);
        if (strcmp("/", cleaned_path))
        {
            if (rmdir(cleaned_path) == 0)
            {
                return (void *)0;
            }
            else
            {
                printf("mkdir:failed %s\n", cleaned_path);
            }
        }
    }
    return (void *)-1;
}

void *buildin_rm(uint32_t argc, char **argv)
{
    if (argc != 2)
    {
        printf("rmdir: no arguments support!\n");
        return (void *)-1;
    }
    else
    {
        char cleaned_path[32];
        memset(cleaned_path, 0, 32);
        make_clear_abs_path(argv[1], cleaned_path);
        if (strcmp("/", cleaned_path))
        {
            if (unlink(cleaned_path) == 0)
            {
                return (void *)0;
            }
            else
            {
                printf("mkdir:failed %s\n", cleaned_path);
            }
        }
    }
    return (void *)-1;
}