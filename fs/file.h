#ifndef __FS__FILE_H
#define __FS__FILE_H
/*文件结构*/
#include "../lib/stdint.h"
#include "inode.h"

#define MAX_FILES_OPEN 32 // 系统最大打开的文件数

struct file
{
    uint32_t fd_pos; // 记录文件偏移
    uint32_t fd_flag;
    struct inode *fd_inode; // 记录文件inode指针
};

// 标准输入输出描述符
enum std_fd
{
    stdin,
    stdout,
    stderr
};

enum bitmap_type
{
    INODE_BITMAP,
    BLOCK_BITMAP
};

extern struct file file_table[MAX_FILES_OPEN];

int32_t get_free_ft_pos(void);
int32_t pcb_fd_install(int32_t fd_idx);
int32_t inode_bitmap_alloc(struct partition *part);
int32_t block_bitmap_alloc(struct partition *part);
void bitmap_sync(struct partition *part, uint32_t bit_idx, uint8_t btmp);
int32_t fopen(uint32_t ino, uint8_t flag);
int32_t fclose(struct file *f);
int32_t fwrite(struct file *f, const void *buf, uint32_t cnt);
int32_t fread(struct file *f, void *buf, uint32_t cnt);
#endif