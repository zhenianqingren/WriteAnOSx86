#include "../lib/stdio.h"
#include "../lib/user/syscall.h"
#include "../lib/string.h"

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("cat: error command format\n");
        exit(-1);
    }

    uint32_t buf_siz = 1024;
    char abs_path[512];
    memset(abs_path, 0, 512);
    void *buf = malloc(buf_siz);

    if (buf == NULL)
    {
        printf("cat: malloc memory failed\n");
        return -1;
    }

    if (argv[1][0] != '/')
    {
        getcwd(abs_path, 512);
        strcat(abs_path, "/");
    }
    strcat(abs_path, argv[1]);

    int fd = open(abs_path, O_RDONLY);
    if (fd == -1)
    {
        printf("cat: cannot open %s\n", argv[1]);
        return -1;
    }

    int nread = 0;
    while (1)
    {
        memset(buf, 0, buf_siz);
        nread = read(fd, buf, buf_siz);
        if (nread < buf_siz)
        {
            break;
        }
        write(1, buf, nread);
    }
    free(buf);
    close(fd);
    exit(0);
}