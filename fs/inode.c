#include "inode.h"
#include "global.h"
#include "list.h"
#include "debug.h"
#include "../lib/string.h"
#include "super_block.h"
#include "interrupt.h"
#include "file.h"
#include "fs.h"
/**
 * 存储inode的位置
 */
struct inode_pos
{
    bool two_sec;     // inode是否跨扇区
    uint32_t sec_lba; // inode所在扇区号
    uint32_t offset;  // inode所在扇区偏移
};

/*获取inode所在扇区和偏移*/
static void inode_locate(struct partition *part, uint32_t inode_no, struct inode_pos *pos)
{
    /*inode_table在硬盘上是连续的*/
    ASSERT(inode_no < 4096);

    uint32_t inode_tb_lba = part->sb->inode_table_lba;
    uint32_t isiz = sizeof(struct inode);
    uint32_t offset = inode_no * isiz;
    // 相对于inode_table起始扇区的偏移
    uint32_t offsect = offset / SECTOR_SIZE;
    // 在该扇区中的偏移字节量
    uint32_t offbyte = offset % SECTOR_SIZE;

    // 判断是否跨越两个扇区
    uint32_t leftb = 512 - offbyte;
    if (leftb < isiz)
    {
        pos->two_sec = true;
    }
    else
    {
        pos->two_sec = false;
    }
    pos->sec_lba = inode_tb_lba + offsect;
    pos->offset = offbyte;
}

/*将inode写入part*/
void inode_sync(struct partition *part, struct inode *inode, void *io_buf)
{
    // 定位
    uint32_t inode_no = inode->ino;
    struct inode_pos ipos;
    inode_locate(part, inode_no, &ipos);
    ASSERT(ipos.sec_lba < (part->start_lba + part->sec_cnt));

    // 硬盘中的inode不需要iopen_cnt inode_tag wrte_only
    struct inode pure;
    memcpy(&pure, inode, sizeof(struct inode));

    pure.iopen_cnt = 0;
    pure.write_only = false;
    pure.inode_tag.prev = NULL;
    pure.inode_tag.next = NULL;

    char *ibuf = (char *)io_buf;
    /*如果inode跨扇区 就要读出两个扇区后再写入两个扇区*/
    uint8_t scnt = ipos.two_sec ? 2 : 1;
    ide_read(part->my_disk, ipos.sec_lba, ibuf, scnt);
    memcpy(ibuf + ipos.offset, &pure, sizeof(struct inode));
    ide_write(part->my_disk, ipos.sec_lba, ibuf, scnt);
}

/*根据i节点号返回i节点*/
struct inode *iopen(struct partition *part, uint32_t ino)
{
    /*先在已打开的inode链表寻找i节点 此链表相当于缓冲区*/
    struct list_elem *elem = part->open_inodes.head.next;
    struct inode *found;
    while (elem != &part->open_inodes.tail)
    {
        found = elem2entry(struct inode, inode_tag, elem);
        if (found->ino == ino)
        {
            found->iopen_cnt++;
            return found;
        }
        elem = elem->next;
    }
    /*如果在缓冲区未命中 则从硬盘读入 并挂入链表*/
    struct inode_pos ipos;
    inode_locate(part, ino, &ipos);
    ASSERT(ipos.sec_lba < (part->start_lba + part->sec_cnt));
    /**
     * 为了使新创建的inode被所有任务共享
     * 需要将当前进程临时标记为内核进程 使inode处于内核空间
     */
    struct task_struct *cur = running_thread();
    uint32_t *pg_bak = cur->pgdir;
    cur->pgdir = NULL;
    found = (struct inode *)sys_malloc(sizeof(struct inode));
    cur->pgdir = pg_bak;

    uint8_t scnt = ipos.two_sec ? 2 : 1;
    char *ibuf = sys_malloc(scnt * SECTOR_SIZE);
    ide_read(part->my_disk, ipos.sec_lba, ibuf, scnt);
    memcpy(found, ibuf + ipos.offset, sizeof(struct inode));
    // 将其插入inode队列
    list_push(&part->open_inodes, &found->inode_tag);
    found->iopen_cnt = 1;
    sys_free(ibuf);
    return found;
}

// 关闭inode or 减少打开数量
void iclose(struct inode *inode)
{
    enum intr_status old = intr_disable();
    if (--inode->iopen_cnt == 0)
    {
        list_remove(&inode->inode_tag);
        ifree(inode);
    }
    intr_set_status(old);
}

// 释放inode的堆空间
void ifree(struct inode *inode)
{
    struct task_struct *cur = running_thread();
    uint32_t *pg_bak = cur->pgdir;
    cur->pgdir = NULL; // 确保释放的是内核内存池
    sys_free(inode);
    cur->pgdir = pg_bak;
}

// 初始化new inode
void inode_init(uint32_t ino, struct inode *inew)
{
    inew->ino = ino;
    inew->isiz = 0;
    inew->iopen_cnt = 0;
    inew->write_only = false;

    // 初始化块索引数组i_sec
    uint8_t idx = 0;
    while (idx < 13)
    {
        inew->i_sects[idx] = 0;
        idx++;
    }
}

// 将硬盘分区part上面的inode清空
void inode_delete(struct partition *part, uint32_t ino, void *io_buf)
{
    ASSERT(ino < 4096);
    struct inode_pos ipos;
    inode_locate(part, ino, &ipos);
    ASSERT(ipos.sec_lba <= (part->start_lba + part->sec_cnt));
    char *bufp = (char *)io_buf;
    uint32_t sects;
    if (ipos.two_sec)
    {
        sects = 2;
    }
    else
    {
        sects = 1;
    }
    ide_read(part->my_disk, ipos.sec_lba, bufp, sects);
    memset(bufp + ipos.offset, 0, sizeof(struct inode));
    ide_write(part->my_disk, ipos.sec_lba, bufp, sects);
}

// 回收inode数据块和inode本身
void inode_release(struct partition *part, uint32_t ino)
{
    struct inode *i = iopen(part, ino);
    ASSERT(i->ino == ino);

    // 回收inode指向的文件所占用的块
    uint8_t blk_idx = 0;
    uint8_t blk_cnt = 12;
    uint32_t blk_bitmap_idx;
    uint32_t all_blocks[140] = {0};

    while (blk_idx < 12)
    {
        all_blocks[blk_idx] = i->i_sects[blk_idx];
        blk_idx++;
    }
    if (i->i_sects[12] != 0)
    {
        ide_read(part->my_disk, i->i_sects[12], all_blocks + 12, 1);
        blk_cnt = 140;
        // 回收一级间接块表占用扇区
        blk_bitmap_idx = i->i_sects[12] - part->sb->data_start_lba;
        ASSERT(blk_bitmap_idx > 0);
        bitmap_set(&part->block_bitmap, blk_bitmap_idx, 0);
        bitmap_sync(part, blk_bitmap_idx, BLOCK_BITMAP);
    }
    // 块位图
    blk_idx = 0;
    while (blk_idx < blk_cnt)
    {
        if (all_blocks[blk_idx] != 0)
        {
            blk_bitmap_idx = all_blocks[blk_idx] - part->sb->data_start_lba;
            ASSERT(blk_bitmap_idx > 0);
            bitmap_set(&part->block_bitmap, blk_bitmap_idx, 0);
            bitmap_sync(part, blk_bitmap_idx, BLOCK_BITMAP);
        }
        blk_idx++;
    }
    // inode位图
    bitmap_set(&part->block_bitmap, ino, 0);
    bitmap_sync(part, ino, INODE_BITMAP);

    iclose(i);
}