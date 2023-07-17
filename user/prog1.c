#include "../lib/stdio.h"
#include "../lib/user/syscall.h"
#include "../lib/string.h"

int main(int argc, char *argv[])
{
    int idx = 0;
    while (idx < argc)
    {
        printf("argument: %d->%s\n", idx, argv[idx]);
        idx++;
    }

    printf("I'm NO.%d process!\n", getpid());
    printf("please input a file's name, I will show the type: ");

    char buf[32];
    memset(buf, 0, 32);
    char *bufp = buf;
    while (1)
    {
        if (read(0, bufp, 1) != 1)
        {
            printf("error read!\n");
        }
        if (*bufp == '\n')
        {
            *bufp = 0;
            break;
        }
        bufp++;
    }

    printf("\n");
    struct stat stat;
    if (fstat(buf, &stat) == -1)
    {
        printf("error finally!\n");
    }
    else
    {
        printf("type: %s\n", stat.st_ftype == FT_DIRECTORY ? "directory" : "regular file");
    }

    while (1)
        ;
    return 0;
}