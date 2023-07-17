#include "file.h"
#include "fs.h"
#include "super_block.h"
#include "kio.h"
#include "thread.h"
#include "global.h"
#include "dir.h"
#include "debug.h"
// 文件表
struct file file_table[MAX_FILES_OPEN];

// 从文件表file_table中返回一个空位 成功返回下标 失败返回-1

int32_t get_free_ft_pos(void)
{
    int32_t fd_idx = 3;
    while (fd_idx < MAX_FILES_OPEN)
    {
        if (file_table[fd_idx].fd_inode == NULL)
        {
            break;
        }
        ++fd_idx;
    }
    if (fd_idx == MAX_FILES_OPEN)
    {
        printk("exceed max open files!!!\n");
        return -1;
    }
    return fd_idx;
}

// 将全局描述符安装到进程或内核线程自己的描述符表之中
// 成功返回下标，失败返回-1
int32_t pcb_fd_install(int32_t fd_idx)
{
    struct task_struct *cur = running_thread();
    int32_t local = 3; // 跨过0、1、2
    while (local < MAX_FILES_OPEN_PER_PROC)
    {
        if (cur->fd_table[local] == -1)
        {
            cur->fd_table[local] = fd_idx;
            break;
        }
        ++local;
    }
    if (local == MAX_FILES_OPEN_PER_PROC)
    {
        printk("exceed max open files for process!!!\n");
        return -1;
    }
    return local;
}

// 分配一个i结点 返回其编号
int32_t inode_bitmap_alloc(struct partition *part)
{
    int32_t idx = bitmap_scan(&part->inode_bitmap, 1);
    if (idx == -1)
    {
        return -1;
    }
    bitmap_set(&part->inode_bitmap, idx, 1);
    return idx;
}

// 分配一个块 返回其扇区地址
int32_t block_bitmap_alloc(struct partition *part)
{
    int32_t idx = bitmap_scan(&part->block_bitmap, 1);
    if (idx == -1)
    {
        return -1;
    }
    bitmap_set(&part->block_bitmap, idx, 1);
    return (part->sb->data_start_lba + idx); // 返回可用的扇区地址
}

// 将内存中bitmap第bit_idx位所在的512字节同步到硬盘
void bitmap_sync(struct partition *part, uint32_t bit_idx, uint8_t btmp)
{
    uint32_t off_sec = bit_idx / 4096;       // 在本位图中的扇区偏移量
    uint32_t off_siz = off_sec * BLOCK_SIZE; // 在本位图中扇区的字节偏移量的倍数
    uint32_t sec_lba;
    uint8_t *bitmap_off;
    // 需要被同步到硬盘的只有inode_bitmap和block_bitmap
    switch (btmp)
    {
    case INODE_BITMAP:
        sec_lba = part->sb->inode_bitmap_lba + off_sec;
        bitmap_off = part->inode_bitmap.bits + off_siz;
        break;

    case BLOCK_BITMAP:
        sec_lba = part->sb->block_bitmap_lba + off_sec;
        bitmap_off = part->block_bitmap.bits + off_siz;
        break;
    default:
        break;
    }

    ide_write(part->my_disk, sec_lba, (void *)bitmap_off, 1);
}

// 创建文件 成功返回返回文件描述符 失败返回-1
int32_t fcreate(struct dir *parent, char *fn, uint8_t flag)
{
    // 公共缓冲区创建 1K
    void *io_buf = sys_malloc(1024);
    if (io_buf == NULL)
    {
        printk("in fcreate: sysmalloc for io_buf failed!!!\n");
        return -1;
    }
    uint8_t rollback = 0; // 失败时回滚状态
    // 为新文件分配inode
    int32_t ino = inode_bitmap_alloc(cur_part);
    if (ino == -1)
    {
        printk("in fcreate: alloc for inode failed!!!\n");
        return -1;
    }
    struct inode *new_inode = (struct inode *)sys_malloc(sizeof(struct inode));
    if (new_inode == NULL)
    {
        printk("in fcreate: alloc for new inode failed!!!\n");
        rollback = 1;
        goto _rollback;
    }
    inode_init(ino, new_inode);

    // 在文件表中寻找合适位置 装入文件表
    int32_t fd_idx = get_free_ft_pos();
    if (fd_idx == -1)
    {
        printk("exceed max open files!!!\n");
        rollback = 2;
        goto _rollback;
    }

    file_table[fd_idx].fd_flag = flag;
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_inode = new_inode;
    new_inode->write_only = false;
    // 将目录项填入
    struct dir_entry dire;
    memset(&dire, 0, sizeof(struct dir_entry));
    create_dire(fn, ino, FT_REGULAR, &dire);
    // 同步内存数据到硬盘
    // 安装目录项 包括block_bitmap 以及inode指向的逻辑块的修改
    memset(io_buf, 0, 1024);
    if (!sync_dire(parent, &dire, io_buf))
    {
        printk("sync directory entry failed!!!\n");
        rollback = 3;
        goto _rollback;
    }
    memset(io_buf, 0, 1024);
    // 父目录i结点的内容同步到硬盘 其大小的改变
    inode_sync(cur_part, parent->inode, io_buf);
    // 将新创建的inode的内容同步到硬盘
    memset(io_buf, 0, 1024);
    inode_sync(cur_part, new_inode, io_buf);
    // 同步inode_bitmap
    bitmap_sync(cur_part, ino, INODE_BITMAP);
    // 将新创建的inode加入链表
    list_push(&cur_part->open_inodes, &new_inode->inode_tag);
    sys_free(io_buf);
    return pcb_fd_install(fd_idx);

_rollback:
    switch (rollback)
    {
    case 3:
        memset(&file_table[fd_idx], 0, sizeof(struct file));
    case 2:
        sys_free(new_inode);
    case 1:
        bitmap_set(&cur_part->inode_bitmap, ino, 0);
        break;
    default:
        break;
    }
    sys_free(io_buf);
    return -1;
}

// 打开编号ino的inode对应的文件，成功返回文件描述符，失败返回-1
int32_t fopen(uint32_t ino, uint8_t flag)
{
    int fd_idx = get_free_ft_pos();
    if (fd_idx == -1)
    {
        printk("exceed max open files\n");
        return -1;
    }
    file_table[fd_idx].fd_inode = iopen(cur_part, ino);
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;
    bool *wonly = &file_table[fd_idx].fd_inode->write_only;
    if (flag & O_WRONLY || flag & O_RDWR)
    {
        // 如果写文件，要注意有没有其他进程正在写
        // 进入临界区先关中断
        enum intr_status old = intr_disable();
        if (!(*wonly))
        {
            // 当前无其他进程写文件
            *wonly = true;
            intr_set_status(old);
        }
        else
        {
            intr_set_status(old);
            printk("file cannot be written now!!!\n");
            return -1;
        }
    }
    // 如果是读文件或者创建文件
    return pcb_fd_install(fd_idx);
}

int32_t fclose(struct file *f)
{
    if (f->fd_inode == NULL)
    {
        return -1;
    }
    f->fd_inode->write_only = false;
    iclose(f->fd_inode);
    f->fd_inode = NULL;
    return 0;
}

// 将buf中的count个字节写入文件f
int32_t fwrite(struct file *f, const void *buf, uint32_t cnt)
{
    if (f->fd_inode->isiz + cnt > SECTOR_SIZE * 140)
    {
        // 根据inode结构可知支持的最大文件大小是512*140
        printk("exceed max file size!!!\n");
        return -1;
    }
    uint8_t *io_buf = sys_malloc(512);
    if (io_buf == NULL)
    {
        printk("fwrite: sys_malloc for io_buf failed!!!\n");
        return -1;
    }
    uint32_t *all_blocks = (uint32_t *)sys_malloc(BLOCK_SIZE + 48); // 12*4+128*4
    // 记录文件所有的块地址
    if (all_blocks == NULL)
    {
        printk("fwrite: sys_malloc for all_blocks failed!!!\n");
        return -1;
    }

    memset(all_blocks, 0, BLOCK_SIZE + 48);
    memset(io_buf, 0, SECTOR_SIZE);
    const uint8_t *src = buf; // 用src指向buf中待写入的数据
    uint32_t bwritten = 0;
    uint32_t siz_left = cnt;
    int32_t blk_lba = -1;
    uint32_t blk_bitmap_idx = 0;

    uint32_t sec_idx;
    uint32_t sec_lba;
    uint32_t sec_off_bytes;
    uint32_t sec_left_bytes;
    uint32_t chunk_siz;      // 每次写入硬盘的数据块大小
    int32_t indirect_blk_tb; // 获取间接块地址
    uint32_t blk_idx;

    uint32_t used_blks;
    uint32_t add;
    uint32_t will_blks;

    used_blks = 0;
    //  判断文件是否是第一次写 如果是则为其分配块
    if (f->fd_inode->i_sects[0] == 0)
    {
        blk_lba = block_bitmap_alloc(cur_part);
        if (blk_lba == -1)
        {
            printk("fwrite: block_bitmap_alloc failed!!!\n");
            return -1;
        }
        f->fd_inode->i_sects[0] = blk_lba;
        // 每次分配一个位图就将其同步到硬盘
        blk_bitmap_idx = blk_lba - cur_part->sb->data_start_lba;
        ASSERT(blk_bitmap_idx != 0);
        bitmap_sync(cur_part, blk_bitmap_idx, BLOCK_BITMAP);
        used_blks = 1;
    }

    if (used_blks == 0)
    {
        // 写入cnt个字节之前 该文件已经占用多少 不足一块按一块算
        used_blks = f->fd_inode->isiz / BLOCK_SIZE;
        if (f->fd_inode->isiz % BLOCK_SIZE)
        {
            ++used_blks;
        }
    }
    // 存储cnt个字节后该文件将占用的块数
    will_blks = (f->fd_inode->isiz + cnt) / BLOCK_SIZE;
    if ((f->fd_inode->isiz + cnt) % BLOCK_SIZE)
    {
        ++will_blks;
    }
    ASSERT(will_blks <= 140);
    // 通过增量判断是否需要分配扇区
    add = will_blks - used_blks;
    // 将写文件要用到的块地址收集到all_blocks
    if (add == 0)
    {
        if (will_blks <= 12)
        {
            blk_idx = used_blks - 1;
            all_blocks[blk_idx] = f->fd_inode->i_sects[blk_idx];
        }
        else
        {
            // 未写入新数据之前已经占用了间接块 将间接块导入
            indirect_blk_tb = f->fd_inode->i_sects[12];
            ide_read(cur_part->my_disk, indirect_blk_tb, all_blocks + 12, 1);
        }
    }
    else
    {
        // 如果有增量 也就是要分配新的块
        if (will_blks <= 12)
        {
            // 已经用的最后一个块可能有剩余空间
            blk_idx = used_blks - 1;
            ASSERT(f->fd_inode->i_sects[blk_idx] != 0);
            all_blocks[blk_idx] = f->fd_inode->i_sects[blk_idx];
            // 将未来要用的扇区分配好写入all_blocks
            blk_idx = used_blks;
            while (blk_idx < will_blks)
            {
                blk_lba = block_bitmap_alloc(cur_part);
                if (blk_lba == -1)
                {
                    printk("fwrite: allocate for blk_lba failed!!!\n");
                    return -1;
                }
                // 不应该有块未使用但已经分配的情况
                ASSERT(f->fd_inode->i_sects[blk_idx] == 0);
                f->fd_inode->i_sects[blk_idx] = blk_lba;
                all_blocks[blk_idx] = blk_lba;
                // 每分配一个块 将块位图同步到硬盘
                blk_bitmap_idx = blk_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, blk_bitmap_idx, BLOCK_BITMAP);
                blk_idx++;
            }
        }
        else if (used_blks <= 12 && will_blks > 12)
        {
            // 旧数据在12个块以内 新数据将使用间接块
            blk_idx = used_blks - 1;
            all_blocks[blk_idx] = f->fd_inode->i_sects[blk_idx];
            // 创建一级间接块表
            blk_lba = block_bitmap_alloc(cur_part);
            if (blk_lba == -1)
            {
                printk("fwrite: block_bitmap_allo for blk failed!!!\n");
                return -1;
            }
            ASSERT(f->fd_inode->i_sects[12] == 0);
            f->fd_inode->i_sects[12] = blk_lba;
            indirect_blk_tb = blk_lba;
            // 同步块位图
            blk_bitmap_idx = blk_lba - cur_part->sb->data_start_lba;
            bitmap_sync(cur_part, blk_bitmap_idx, BLOCK_BITMAP);

            blk_idx = used_blks;
            while (blk_idx < will_blks)
            {
                blk_lba = block_bitmap_alloc(cur_part);
                if (blk_lba == -1)
                {
                    printk("fwrite: block_bitmap_allo for blk failed!!!\n");
                    return -1;
                }
                if (blk_idx < 12)
                {
                    ASSERT(f->fd_inode->i_sects[blk_idx] == 0);
                    f->fd_inode->i_sects[blk_idx] = blk_lba;
                    all_blocks[blk_idx] = blk_lba;
                }
                else
                {
                    // 间接块的内容只写入到all_blocks
                    all_blocks[blk_idx] = blk_lba;
                }
                blk_bitmap_idx = blk_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, blk_bitmap_idx, BLOCK_BITMAP);
                blk_idx++;
            }
            ide_write(cur_part->my_disk, indirect_blk_tb, all_blocks + 12, 1); // 同步一级间接块到硬盘
        }
        else if (used_blks > 12)
        {
            // 新数据占据间接块
            ASSERT(f->fd_inode->i_sects[12] == 0);
            indirect_blk_tb = f->fd_inode->i_sects[12];
            ide_read(cur_part->my_disk, indirect_blk_tb, all_blocks + 12, 1);

            blk_idx = used_blks;
            while (blk_idx < will_blks)
            {
                blk_lba = block_bitmap_alloc(cur_part);
                if (blk_lba == -1)
                {
                    printk("fwrite: block_bitmap_allo for blk failed!!!\n");
                    return -1;
                }
                all_blocks[blk_idx++] = blk_lba; // 只需要写入all_blocks
                blk_bitmap_idx = blk_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, blk_bitmap_idx, BLOCK_BITMAP);
            }
            ide_write(cur_part->my_disk, indirect_blk_tb, all_blocks + 12, 1); // 同步一级间接块到硬盘
        }
    }
    bool firstw = true; // 待写入的第一个块是否有剩余空间
    f->fd_pos = f->fd_inode->isiz;
    while (bwritten < cnt)
    {
        memset(io_buf, 0, SECTOR_SIZE);
        sec_idx = f->fd_inode->isiz / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx];
        sec_off_bytes = f->fd_inode->isiz % BLOCK_SIZE;
        sec_left_bytes = SECTOR_SIZE - sec_off_bytes;
        // 判断此次写入硬盘的数据的大小
        chunk_siz = siz_left < sec_left_bytes ? siz_left : sec_left_bytes;
        if (firstw)
        {
            ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
            firstw = false;
        }
        memcpy(io_buf + sec_off_bytes, src, chunk_siz);
        ide_write(cur_part->my_disk, sec_lba, io_buf, 1);
        printk("file write at lba: 0x%x\n", sec_lba);
        src += chunk_siz;
        f->fd_inode->isiz += chunk_siz;
        f->fd_pos += chunk_siz;
        bwritten += chunk_siz;
        siz_left -= chunk_siz;
    }
    memset(io_buf, 0, SECTOR_SIZE);
    // 同步变化的i_sects i_siz
    inode_sync(cur_part, f->fd_inode, io_buf);
    sys_free(all_blocks);
    sys_free(io_buf);

    printk("current file with size:%d\n", f->fd_inode->isiz);
    return bwritten;
}

int32_t fread(struct file *f, void *buf, uint32_t cnt)
{
    uint8_t *buf_dst = (uint8_t *)buf;
    uint32_t siz = cnt > f->fd_inode->isiz ? f->fd_inode->isiz : cnt;
    uint32_t left = cnt;
    // 如果要读取的字节量超过文件可读的剩余量
    if ((f->fd_pos + cnt) > f->fd_inode->isiz)
    {
        siz = f->fd_inode->isiz - f->fd_pos;
        left = siz;
        if (siz == 0)
        {
            return -1;
        }
    }
    uint8_t *io_buf = (uint8_t *)sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL)
    {
        printk("fread: sys_malloc for io_buf failed!!!\n");
        return -1;
    }
    uint32_t *all_blocks = (uint32_t *)sys_malloc(BLOCK_SIZE + 48);
    // 用来记录文件所有的块地址
    if (all_blocks == NULL)
    {
        printk("fread: sys_malloc for all blocks failed!!!\n");
        sys_free(io_buf);
        return -1;
    }
    memset(io_buf, 0, BLOCK_SIZE);
    memset(all_blocks, 0, BLOCK_SIZE + 48);
    uint32_t blk_read_start_idx = f->fd_pos / BLOCK_SIZE;
    uint32_t blk_read_end_idx = (f->fd_pos + siz) / BLOCK_SIZE;
    uint32_t read_blks = blk_read_end_idx - blk_read_start_idx;
    ASSERT(blk_read_end_idx < 139 && blk_read_start_idx < 139);
    int32_t indirect_tb;
    uint32_t blk_idx;

    if (read_blks == 0)
    {
        ASSERT(blk_read_end_idx == blk_read_start_idx);
        if (blk_read_end_idx < 12)
        {
            blk_idx = blk_read_end_idx;
            all_blocks[blk_idx] = f->fd_inode->i_sects[blk_idx];
        }
        else
        {
            indirect_tb = f->fd_inode->i_sects[12];
            ide_read(cur_part->my_disk, indirect_tb, all_blocks + 12, 1);
        }
    }
    else
    {
        if (blk_read_end_idx < 12)
        {
            blk_idx = blk_read_start_idx;
            while (blk_idx <= blk_read_end_idx)
            {
                all_blocks[blk_idx] = f->fd_inode->i_sects[blk_idx];
                blk_idx++;
            }
        }
        else if (blk_read_start_idx < 12 && blk_read_end_idx >= 12)
        {
            blk_idx = blk_read_start_idx;
            while (blk_idx < 12)
            {
                all_blocks[blk_idx] = f->fd_inode->i_sects[blk_idx];
                blk_idx++;
            }
            ASSERT(f->fd_inode->i_sects[12] != 0);
            indirect_tb = f->fd_inode->i_sects[12];
            ide_read(cur_part->my_disk, indirect_tb, all_blocks + 12, 1);
        }
        else
        {
            ASSERT(f->fd_inode->i_sects[12] != 0);
            indirect_tb = f->fd_inode->i_sects[12];
            ide_read(cur_part->my_disk, indirect_tb, all_blocks + 12, 1);
        }
    }
    uint32_t sec_idx;
    uint32_t sec_lba;
    uint32_t sec_off;
    uint32_t sec_left;
    uint32_t chunk_siz;
    uint32_t bread = 0;
    while (bread < siz)
    {
        sec_idx = f->fd_pos / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx];
        sec_off = f->fd_pos % BLOCK_SIZE;
        sec_left = BLOCK_SIZE - sec_off;
        chunk_siz = left < sec_left ? left : sec_left;
        memset(io_buf, 0, SECTOR_SIZE);
        ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
        memcpy(buf_dst, io_buf + sec_off, chunk_siz);
        buf_dst += chunk_siz;
        f->fd_pos += chunk_siz;
        bread += chunk_siz;
        left -= chunk_siz;
    }
    sys_free(io_buf);
    sys_free(all_blocks);
    return bread;
}