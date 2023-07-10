#ifndef __FS__INODE_H
#define __FS__INODE_H

#include "../lib/stdint.h"
#include "ide.h"
#include "list.h"

struct inode
{
    uint32_t ino;  // inode编号
    uint32_t isiz; // 当inode指向普通文件，isiz是文件大小，当inode指向目录文件，isiz是目录项大小之和

    uint32_t iopen_cnt; // 记录文件被打开的次数
    bool write_only;    // 确保写文件不能并发

    // i_secs[0-11]是直接块地址 i_sects[12]存储一级间接指针
    uint32_t i_sects[13];
    struct list_elem inode_tag;
};

struct inode *iopen(struct partition *part, uint32_t ino);
void iclose(struct inode *inode);
void inode_init(uint32_t ino, struct inode *inew);
void inode_sync(struct partition *part, struct inode *inode, void *io_buf);
void ifree(struct inode *inode);
void inode_release(struct partition *part, uint32_t ino);
#endif