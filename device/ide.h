#ifndef __DEVICE__IDE_H
#define __DEVICE__IDE_H
#include "stdint.h"
#include "sync.h"
#include "list.h"
#include "super_block.h"
#include "bitmap.h"

/*分区结构*/
struct partition
{
    uint32_t start_lba;         // 起始扇区
    uint32_t sec_cnt;           // 扇区数
    struct disk *my_disk;       // 分区所属硬盘
    struct list_elem part_tag;  // 用于队列中的标记
    char name[8];               // 分区名称
    struct super_block *sb;     // 本分区超级块
    struct bitmap block_bitmap; // 块位图 本实现一个块就是一个扇区
    struct bitmap inode_bitmap; // i结点位图
    struct list open_inodes;    // 本分区打开的i节点队列
};

struct disk
{
    char name[8];                    // 磁盘名称
    struct ide_channel *my_channel;  // 此disk归属哪一个channel
    uint8_t dev_no;                  // 主盘或者从盘
    struct partition prim_parts[4];  // 主分区只有4个
    struct partition logic_parts[8]; // 逻辑分区上限支持8个
};

struct ide_channel
{
    char name[8];               // channel名
    uint16_t port_base;         // 本通道起始端口号
    uint8_t irq_no;             // 本通道中断号
    struct lock lock;           // 通道锁 中断来自通道的哪一块硬盘不易区分，因此每次I/O都加锁
    bool expect_int;            // 等待硬盘的中断
    struct semaphore disk_done; // 阻塞、唤醒驱动程序,避免浪费CPU
    struct disk devices[2];     // 一个通道上连接一主一从硬盘
};

void ide_init();
void ide_read(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt);
void ide_write(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt);
void intr_hd_handler(uint8_t irq_no);
#endif