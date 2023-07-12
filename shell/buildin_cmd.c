#include "buildin_cmd.h"
#include "debug.h"
#include "global.h"
#include "file.h"
#include "dir.h"
#include "../lib/string.h"
#include "../lib/user/syscall.h"

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
            char *slash_ptr = strrchr((const char *)new_abs_path, "/");
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
            sub_path = path_parse(sub_path, name);
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