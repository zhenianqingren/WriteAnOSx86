#include "dir.h"
#include "ide.h"
#include "super_block.h"
#include "file.h"
#include "kio.h"
#include "../lib/string.h"
#include "global.h"
#include "debug.h"
#include "thread.h"

struct dir root_dir; // 根目录

// 打开根目录
void open_root_dir(struct partition *part)
{
    root_dir.inode = iopen(part, part->sb->root_dir_no);
    root_dir.dir_pos = 0;
}

// 在分区part上打开编号ino的inode的目录并返回目录指针
struct dir *dir_open(struct partition *part, uint32_t ino)
{
    struct dir *pdir = (struct dir *)sys_malloc(sizeof(struct dir));
    pdir->inode = iopen(part, ino);
    pdir->dir_pos = 0;
    return pdir;
}

// 在part分区内的目录pdir内寻找名为name的文件 并将目录项存放至dire
// 成功找到返回true
bool search_dire(struct partition *part, struct dir *pdir, const char *name, struct dir_entry *dire)
{
    uint32_t blocks = 140;                                  // 12个直接块+128个一级间接块
    uint32_t *all_blocks = (uint32_t *)sys_malloc(140 * 4); // 块地址
    if (all_blocks == NULL)
    {
        printk("search_dire: malloc for all_blocks failed!!!\n");
        return false;
    }
    uint32_t idx = 0;
    while (idx < 12)
    {
        all_blocks[idx] = pdir->inode->i_sects[idx];
        idx++;
    }
    idx = 0;
    if (pdir->inode->i_sects[12] != 0)
    {
        ide_read(part->my_disk, pdir->inode->i_sects[12], all_blocks + 12, 1);
    }
    // 写的时候保证目录项不会跨扇区 因此读的时候以扇区为单位 方便操作
    uint8_t *buf = (uint8_t *)sys_malloc(SECTOR_SIZE);
    struct dir_entry *pdire = (struct dir_entry *)buf;
    uint32_t pdire_siz = part->sb->dir_entry_siz;
    uint32_t pdire_cnt = SECTOR_SIZE / pdire_siz;

    while (idx < blocks)
    {
        if (all_blocks[idx] == 0)
        {
            idx++;
            continue;
        }
        ide_read(part->my_disk, all_blocks[idx], pdire, 1);
        uint32_t dire_idx = 0;
        while (dire_idx < pdire_cnt)
        {
            if (!strcmp(pdire->fn, name))
            {
                memcpy(dire, pdire, pdire_siz);
                sys_free(buf);
                sys_free(all_blocks);
                return true;
            }
            dire_idx++;
            pdire++;
        }
        idx++;
        pdire = (struct dir_entry *)buf;
        memset(buf, 0, SECTOR_SIZE);
    }

    sys_free(buf);
    sys_free(all_blocks);
    return false;
}

void dir_close(struct dir *dir)
{
    // 根目录不能被关闭
    if (dir == &root_dir)
    {
        return;
    }
    iclose(dir->inode);
    sys_free(dir);
}

void create_dire(char *fn, uint32_t ino, uint8_t ftype, struct dir_entry *pdire)
{
    ASSERT(strlen(fn) <= MAX_FN_LEN);
    memcpy(pdire->fn, fn, strlen(fn));
    pdire->ino = ino;
    pdire->ftype = ftype;
}

bool sync_dire(struct dir *parent, struct dir_entry *pdire, void *io_buf)
{
    struct inode *dir_inode = parent->inode;
    uint32_t dir_siz = dir_inode->isiz;
    uint32_t dire_siz = cur_part->sb->dir_entry_siz;

    ASSERT(dir_siz % dire_siz == 0);

    uint32_t dires = SECTOR_SIZE / dire_siz;
    int32_t block_lba = -1;
    // 将该目录所有地址存入all_blocks
    uint8_t idx = 0;
    uint32_t all_blocks[140];
    memset(all_blocks, 0, 4 * 140);
    while (idx < 12)
    {
        all_blocks[idx] = dir_inode->i_sects[idx];
        idx++;
    }

    struct dir_entry *dire = (struct dir_entry *)io_buf;
    int32_t block_bitmap_idx = -1;
    idx = 0;
    while (idx < 140)
    {
        block_bitmap_idx = -1;
        if (all_blocks[idx] == 0)
        {
            block_lba = block_bitmap_alloc(cur_part);
            if (block_lba == -1)
            {
                printk("alloc block bitmap for sync_dire failed!!!\n");
                return false;
            }
            // 每次分配一个块就同步一次block_bitmap
            block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
            ASSERT(block_bitmap_idx != -1);
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
            block_bitmap_idx = -1;
            if (idx < 12)
            {
                all_blocks[idx] = block_lba;
                dir_inode->i_sects[idx] = block_lba;
            }
            else if (idx == 12)
            {
                // 再分配一个
                dir_inode->i_sects[12] = block_lba;
                block_lba = -1;
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1)
                {
                    block_bitmap_idx = dir_inode->i_sects[12] - cur_part->sb->data_start_lba;
                    bitmap_set(&cur_part->block_bitmap, block_bitmap_idx, 0);
                    dir_inode->i_sects[12] = 0;
                    printk("alloc block bitmap for sync_dire failed!!!\n");
                    return false;
                }
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                ASSERT(block_bitmap_idx != -1);
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
                all_blocks[12] = block_lba;
                // 把新分配的间接块地址写入
                ide_write(cur_part->my_disk, dir_inode->i_sects[12], all_blocks + 12, 1);
            }
            else
            {
                all_blocks[idx] = block_lba;
                ide_write(cur_part->my_disk, dir_inode->i_sects[12], all_blocks + 12, 1);
            }
            memset(io_buf, 0, SECTOR_SIZE);
            memcpy(io_buf, pdire, dire_siz);
            ide_write(cur_part->my_disk, block_lba, io_buf, 1);
            dir_inode->isiz += dire_siz;
            return true;
        }
        // 若已经存在，将其读入，在扇区内寻找空闲目录项
        ide_read(cur_part->my_disk, all_blocks[idx], io_buf, 1);
        // uint8_t test = 0;
        // printk("Sector:%d After Read\n", all_blocks[idx]);
        // while (true)
        // {
        //     if (((struct dir_entry *)io_buf + test)->ftype == FT_UNKNOWN)
        //     {
        //         break;
        //     }
        //     printk("NO.%d type: %s name: %s\n", test, (dire + test)->ftype == FT_DIRECTORY ? "directory" : "file", (dire + test)->fn);
        //     test++;
        // }
        uint8_t dire_idx = 0;
        while (dire_idx < dires)
        {
            if ((dire + dire_idx)->ftype == FT_UNKNOWN)
            {
                // 初始化或者删除文件后都会将其置成UNKNOWN
                // printk("sync NO.%d type: %s name: %s\n", dire_idx, pdire->ftype == FT_DIRECTORY ? "directory" : "file", pdire->fn);
                memcpy(dire + dire_idx, pdire, dire_siz);
                // test = 0;
                // printk("Sector:%d Before Sync\n", all_blocks[idx]);
                // while (true)
                // {
                //     if (((struct dir_entry *)io_buf + test)->ftype == FT_UNKNOWN)
                //     {
                //         break;
                //     }
                //     printk("NO.%d type: %s name: %s\n", test, (dire + test)->ftype == FT_DIRECTORY ? "directory" : "file", (dire + test)->fn);
                //     test++;
                // }
                ide_write(cur_part->my_disk, all_blocks[idx], io_buf, 1);
                dir_inode->isiz += dire_siz;
                return true;
            }
            dire_idx++;
        }
        idx++;
    }
    printk("directory is full!!!\n");
    return false;
}

// 解析最上层路径名称
const char *path_parse(const char *path_name, char *name_store)
{
    if (path_name[0] == 0)
    {
        return NULL;
    }
    if (path_name[0] == '/')
    {
        while (*(++path_name) == '/')
            ;
    }
    while (*path_name != '/' && *path_name != '\0')
    {
        *name_store++ = *path_name++;
    }
    return path_name;
}

// 返回路径深度
int32_t path_depth(const char *path_name)
{
    ASSERT(path_name != NULL);
    const char *p = path_name;
    char name[MAX_FN_LEN];
    memset(name, 0, MAX_FN_LEN);
    int32_t depth = 0;
    p = path_parse(path_name, name);

    while (name[0])
    {
        depth++;
        memset(name, 0, MAX_FN_LEN);
        if (p)
        {
            p = path_parse(p, name);
        }
    }

    return depth;
}

// 将目录dir中的编号为ino的目录项删除
bool del_dire(struct partition *part, struct dir *dir, uint32_t ino, void *io_buf)
{
    struct inode *i = dir->inode;
    uint32_t blk_idx = 0;
    uint32_t all_blocks[140] = {0};
    while (blk_idx < 12)
    {
        all_blocks[blk_idx] = i->i_sects[blk_idx];
        blk_idx++;
    }
    if (i->i_sects[12] != 0)
    {
        ide_read(part->my_disk, i->i_sects[12], all_blocks + 12, 1);
    }
    // 目录项存储时保证不会跨扇区
    uint32_t dire_siz = part->sb->dir_entry_siz;
    uint32_t dires = SECTOR_SIZE / dire_siz;
    // 每个扇区最大的目录项数目
    struct dir_entry *dire = (struct dir_entry *)io_buf;
    struct dir_entry *found = NULL;
    uint8_t dire_idx;
    uint8_t dire_cnt;
    bool is_first = false; // 目录的第一个块
    blk_idx = 0;
    // 遍历所有块 寻找目录项
    while (blk_idx < 140)
    {
        is_first = false;
        if (all_blocks[blk_idx] == 0)
        {
            blk_idx++;
            continue;
        }
        dire_idx = 0;
        dire_cnt = 0;
        memset(io_buf, 0, SECTOR_SIZE);
        // 读取扇区获取目录项
        ide_read(part->my_disk, all_blocks[blk_idx], io_buf, 1);
        // 遍历所有的目录项 统计该扇区的目录项数量以及是否有待删除的目录项
        while (dire_idx < dires)
        {
            if ((dire + dire_idx)->ftype != FT_UNKNOWN)
            {
                if (!strcmp((dire + dire_idx)->fn, "."))
                {
                    is_first = true;
                }
                else if (strcmp((dire + dire_idx)->fn, ".") && strcmp((dire + dire_idx)->fn, ".."))
                {
                    dire_cnt++;
                    if ((dire + dire_idx)->ino == ino)
                    {
                        ASSERT(found == NULL);
                        found = (dire + dire_idx);
                    }
                }
            }
            dire_idx++;
        }
        // 此扇区没有找到
        if (found == NULL)
        {
            blk_idx++;
            continue;
        }
        ASSERT(dire_cnt >= 1);
        // 除了目录第一个扇区以外，若该扇区上只有目录项自己，则将整个扇区回收
        if (dire_cnt == 1 && !is_first)
        {
            // 在块位图中回收该块
            uint32_t blk_bitmap_idx = all_blocks[blk_idx] - part->sb->data_start_lba;
            bitmap_set(&part->block_bitmap, blk_bitmap_idx, 0);
            bitmap_sync(part, blk_bitmap_idx, BLOCK_BITMAP);
            if (blk_idx < 12)
            {
                dir->inode->i_sects[blk_idx] = 0;
            }
            else
            {
                // 在一级间接索引表中擦除该块地址
                // 如果间接块索引表只有这一个块，连间接块一起回收
                uint32_t indirect_blks = 0;
                uint32_t indirect_blk_idx = 12;
                while (indirect_blk_idx < 140)
                {
                    if (all_blocks[indirect_blk_idx] != 0)
                    {
                        indirect_blks++;
                    }
                }
                ASSERT(indirect_blks >= 1);
                if (indirect_blks > 1)
                {
                    all_blocks[blk_idx] = 0;
                    ide_write(part->my_disk, dir->inode->i_sects[12], all_blocks + 12, 1);
                }
                else
                {
                    blk_bitmap_idx = dir->inode->i_sects[12] - part->sb->data_start_lba;
                    bitmap_set(&part->block_bitmap, blk_bitmap_idx, 0);
                    bitmap_sync(part, blk_bitmap_idx, BLOCK_BITMAP);
                    dir->inode->i_sects[12] = 0;
                }
            }
        }
        else
        {
            memset(found, 0, dire_siz);
            ide_write(part->my_disk, all_blocks[blk_idx], io_buf, 1);
        }

        // 更新i结点信息并同步到硬盘
        dir->inode->isiz -= dire_siz;
        memset(io_buf, 0, SECTOR_SIZE);
        inode_sync(part, dir->inode, io_buf);
        return true;
    }
    return false;
}

struct dir_entry *dir_read(struct dir *dir)
{
    struct dir_entry *dire = (struct dir_entry *)dir->dir_buf;
    struct inode *dir_inode = dir->inode;
    uint32_t all_blocks[140] = {0};
    uint32_t blk_cnt = 12;
    uint32_t blk_idx = 0;
    uint32_t dire_idx = 0;
    while (blk_idx < 12)
    {
        all_blocks[blk_idx] = dir->inode->i_sects[blk_idx];
        blk_idx++;
    }
    if (dir_inode->i_sects[12] != 0)
    {
        ide_read(cur_part->my_disk, dir_inode->i_sects[12], all_blocks + 12, 1);
        blk_cnt = 140;
    }
    blk_idx = 0;
    uint32_t cur_dire_pos = 0;
    uint32_t dire_siz = cur_part->sb->dir_entry_siz;
    uint32_t dires = SECTOR_SIZE / dire_siz;
    while (dir->dir_pos < dir_inode->isiz)
    {
        if (dir->dir_pos >= dir_inode->isiz)
        {
            return NULL;
        }
        if (all_blocks[blk_idx] == 0)
        {
            blk_idx++;
            continue;
        }
        memset(dire, 0, SECTOR_SIZE);
        ide_read(cur_part->my_disk, all_blocks[blk_idx], dire, 1);
        dire_idx = 0;
        // 遍历扇区内所有目录项
        while (dire_idx < dires)
        {
            if ((dire + dire_idx)->ftype)
            {
                if (cur_dire_pos < dir->dir_pos)
                {
                    cur_dire_pos += dire_siz;
                    dire_idx++;
                    continue;
                }
                // 之前位置的下一个 dir_pos记录的是对于所有目录项的偏移 不是某一块的偏移
                ASSERT(cur_dire_pos == dir->dir_pos);
                dir->dir_pos += dire_siz;
                return dire + dire_idx;
            }
            dire_idx++;
        }
        blk_idx++;
    }
    return NULL;
}

bool dir_is_empty(struct dir *dir)
{
    struct inode *dir_inode = dir->inode;
    return (dir_inode->isiz == cur_part->sb->dir_entry_siz * 2);
}

int32_t dir_remove(struct dir *parent, struct dir *child)
{
    struct inode *chld_i = child->inode;
    // 空目录只在inode->i_sectors[0]中有扇区 其他扇区都应该为空
    int32_t blk_idx = 1;
    while (blk_idx < 13)
    {
        ASSERT(chld_i->i_sects[blk_idx] == 0);
        blk_idx++;
    }
    void *io_buf = sys_malloc(SECTOR_SIZE << 1);
    if (io_buf == NULL)
    {
        printk("dir_remove: malloc for io_buf failed!!!\n");
        return -1;
    }
    memset(io_buf, 0, SECTOR_SIZE << 1);
    // 在父目录中删除对应目录项
    del_dire(cur_part, parent, chld_i->ino, io_buf);
    // 回收目录所占用的inode以及扇区 并同步
    inode_release(cur_part, chld_i->ino);
    sys_free(io_buf);
    return 0;
}