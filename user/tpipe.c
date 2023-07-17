#include "../lib/stdio.h"
#include "../lib/user/syscall.h"
#include "../lib/string.h"

int main(int argc, char *argv[])
{
    int32_t fd[2];
    pipe(fd);
    pid_t pid;
    if ((pid = fork()))
    {
        close(fd[0]);
        write(fd[1], "test\n", 5);
        exit(0);
    }
    else
    {
        close(fd[1]);
        char buf[32];
        memset(buf, 0, 32);
        read(fd[0], buf, 5);
        printf("%s\n", buf);
        exit(0);
    }
    return 0;
}