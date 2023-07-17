#include "shell.h"
#include "stdio.h"
#include "../lib/stdint.h"
#include "debug.h"
#include "../lib/user/syscall.h"
#include "file.h"
#include "fs.h"
#include "../lib/string.h"
#include "buildin_cmd.h"
#define CMD_LEN 128   // 命令行输入最大字符数为128
#define MAX_ARG_NR 16 // 加上命令名外最多支持15个参数

// 存储输入命令
static char cmd_line[CMD_LEN] = {0};
// 存储当前目录
char cwd_cache[64] = {0};
// 去掉.和..后的绝对路径

// 输出提示符
void print_prompt(void)
{
    printf("[micer@localhost %s]$:", cwd_cache);
}

static int32_t cmd_parse(char *cmd_str, char **argv, char token)
{
    ASSERT(cmd_str != NULL);
    int32_t arg_idx = 0;
    while (arg_idx < MAX_ARG_NR)
    {
        argv[arg_idx] = NULL;
        arg_idx++;
    }
    char *next = cmd_str;
    int32_t argc = 0;
    while (*next)
    {
        while (*next == token)
        {
            next++;
        }
        if (*next == 0)
        {
            break;
        }
        argv[argc] = next;
        while (*next && *next != token)
        {
            next++;
        }
        // 如果未结束，使token变成0
        if (*next)
        {
            *next++ = 0;
        }
        if (argc > MAX_ARG_NR)
        {
            return -1;
        }
        argc++;
    }

    return argc;
}

char *argv[MAX_ARG_NR];
int32_t argc = -1;

// 从键盘缓冲区中最多读入cnt个字节到buf
static void readline(char *buf, int32_t cnt)
{
    ASSERT(buf != NULL && cnt > 0);
    char *pos = buf;
    while (read(stdin, pos, 1) != -1 && (pos - buf) < cnt)
    {
        switch (*pos)
        {
        case 'l' - 'a':
            //  将键入字符置0
            *pos = 0;
            //  清空屏幕
            clear();
            // 重新打印提示符
            print_prompt();
            // 重新打印已输入的字符
            printf("%s", buf);
            break;
        case 'u' - 'a':
            //  清空输入
            while (buf != pos)
            {
                putchar('\b');
                *(pos--) = 0;
            }
            break;
        case '\n':
        case '\r':
            *pos = 0;
            putchar('\n');
            return;
        case '\b':
            if (buf[0] != '\b')
            {
                --pos;
                putchar('\b');
            }
            break;
        default:
            putchar(*pos);
            pos++;
        }
    }
    printf("readline: cannot find entry in the cmdline, max num of char is 128!\n");
}

char final[32] = {0};
void shell(void)
{
    static const char *buildin_cmd[] = {"pwd", "cd", "ls", "ps", "clear", "mkdir", "rmdir", "rm"};
    static entry_point cmd_entry[] = {buildin_pwd, buildin_cd, buildin_ls, buildin_ps, buildin_clear, buildin_mkdir, buildin_rmdir, buildin_rm};
    clear();
    cwd_cache[0] = '/';
    cwd_cache[1] = 0;
    while (1)
    {
        print_prompt();
        memset(cmd_line, 0, CMD_LEN);
        memset(final, 0, 32);
        readline(cmd_line, CMD_LEN);
        if (cmd_line[0] == 0)
        {
            continue;
        }
        argc = -1;
        argc = cmd_parse(cmd_line, argv, ' ');
        if (argc == -1)
        {
            printf("count of arguments exceed!!!\n");
            continue;
        }
        int8_t i;
        void *ret;
        for (i = 0; i < BUILDIN_CNT; i++)
        {
            if (!strcmp(buildin_cmd[i], argv[0]))
            {
                ret = cmd_entry[i](argc, argv);
                if (CD == i)
                {
                    strcpy(cwd_cache, final);
                }
                break;
            }
        }
        if (i == BUILDIN_CNT)
        {
            pid_t pid;
            if ((pid = fork()))
            {
                int16_t status;
                int32_t cpid = wait(&status);
                ASSERT(cpid != -1);
                printf("child pid: %d with status %d\n", cpid, status);
            }
            else
            {
                make_clear_abs_path(argv[0], final);
                argv[0] = final;
                // 先判断文件是否存在
                struct stat stat;
                memset(&stat, 0, sizeof(struct stat));
                if (fstat(argv[0], &stat) == -1)
                {
                    printf("shell: cannot access %s: No such file or directory\n", argv[0]);
                    exit(-1);
                }
                else
                {
                    if (execv(argv[0], argv) == -1)
                    {
                        exit(-1);
                    }
                }
            }
        }
    }
    PANIC("shell: not allowed to here!\n");
}