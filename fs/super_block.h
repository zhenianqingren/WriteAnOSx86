#ifndef __FS__SUPER_BLOCK_H
#define __FS__SUPER_BLOCK_H

#include "stdint.h"

struct super_block
{
    uint32_t magic;         // 标识文件系统类型
    uint32_t sec_cnt;       // 本分区扇区数
    uint32_t inode_cnt;     // inode数量
    uint32_t part_lba_base; // 本分区起始扇区地址

    uint32_t block_bitmap_lba;   // 块位图起始扇区地址
    uint32_t block_bitmap_sects; // 块位图占用扇区数量

    uint32_t inode_bitmap_lba;   // inode结点位图起始扇区地址
    uint32_t inode_bitmap_sects; // inode结点位图占用扇区数量

    uint32_t inode_table_lba;   // inode结点表起始扇区地址
    uint32_t inode_table_sects; // inode结点表占用扇区数量

    uint32_t data_start_lba; // 数据区开始的第一个扇区
    uint32_t root_dir_no;    // 根目录的i结点索引
    uint32_t dir_entry_siz;  // 目录项大小

    uint8_t pad[460]; // 凑够一个扇区大小
} __attribute__((packed));

#endif