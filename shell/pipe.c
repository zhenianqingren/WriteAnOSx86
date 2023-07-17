#include "pipe.h"
#include "fs.h"
#include "file.h"

bool ispipe(uint32_t locl_fd)
{
    uint32_t glob_fd = fd_locl2glob(locl_fd);
    return file_table[glob_fd].fd_flag == PIPE;
}

// 创建管道 成功返回0 失败返回-1
int32_t sys_pipe(int32_t pipefd[2])
{
    int32_t glob_fd = get_free_ft_pos();
    // 申请一页内核内存作为环形缓冲区
    file_table[glob_fd].fd_inode = get_kernel_pages(1);
    // 初始化环形缓冲区
    ioqueue_init((struct ioqueue *)file_table[glob_fd].fd_inode);
    if (file_table[glob_fd].fd_inode == NULL)
    {
        return -1;
    }

    // 将fd_flag复用为管道标志
    file_table[glob_fd].fd_flag = PIPE;

    // 将fd_pos复用为管道打开数
    file_table[glob_fd].fd_pos = 2; // 读端和写端
    pipefd[0] = pcb_fd_install(glob_fd);
    pipefd[1] = pcb_fd_install(glob_fd);
    return 0;
}

// 从管道中读数据
uint32_t pipe_read(int32_t fd, void *buf, uint32_t cnt)
{
    char *bufp = (char *)buf;
    uint32_t bread = 0;
    uint32_t glob_fd = fd_locl2glob(fd);

    // 获取管道的环形缓冲区
    struct ioqueue *queue = (struct ioqueue *)file_table[glob_fd].fd_inode;

    while (bread < cnt)
    {
        *bufp = ioqueue_pop(queue);
        bread++;
        bufp++;
    }
    return bread;
}

// 往管道中写数据
uint32_t pipe_write(int32_t fd, const void *buf, uint32_t cnt)
{
    uint32_t bwrite = 0;
    uint32_t glob_fd = fd_locl2glob(fd);
    const char *bufp = buf;

    struct ioqueue *queue = (struct ioqueue *)file_table[glob_fd].fd_inode;

    while (bwrite < cnt)
    {
        ioqueue_push(queue, *bufp);
        bwrite++;
        bufp++;
    }

    return bwrite;
}