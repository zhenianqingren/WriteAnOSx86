#include "fs.h"
#include "./dir.h"
#include "./inode.h"
#include "super_block.h"
#include "kio.h"
#include "file.h"
#include "debug.h"
#include "console.h"

extern struct ide_channel channels[2];
extern struct list partition_list;
struct partition *cur_part; // 默认情况下操作哪一个分区

/*在分区链表中找到名为part_name的分区，并将其指针赋给cur_part*/
static bool mount_partition(struct list_elem *pelem, int arg)
{
    char *name = (char *)arg;
    struct partition *part = elem2entry(struct partition, part_tag, pelem);
    if (strcmp(name, part->name)) // 返回0代表相同
        return false;

    cur_part = part;
    struct disk *hd = cur_part->my_disk;

    /*sb_buf存储从硬盘读入的超级块*/
    struct super_block *sb_block = sys_malloc(SECTOR_SIZE);
    /*在内存中创建cur_part的超级块*/
    cur_part->sb = sys_malloc(sizeof(struct super_block));
    if (cur_part->sb == NULL)
    {
        PANIC("alloc memory failed!!!\n");
    }

    memset(sb_block, 0, SECTOR_SIZE);
    memset(cur_part->sb, 0, sizeof(struct super_block));
    /*读入超级块*/
    ide_read(hd, cur_part->start_lba + 1, sb_block, 1);
    /*拷贝至分区的超级块*/
    memcpy(cur_part->sb, sb_block, SECTOR_SIZE);
    /*将硬盘上的块位图读入内存*/
    cur_part->block_bitmap.bits = (uint8_t *)sys_malloc(sb_block->block_bitmap_sects * SECTOR_SIZE);
    if (cur_part->block_bitmap.bits == NULL)
    {
        PANIC("alloc memory failed!!!\n");
    }
    cur_part->block_bitmap.btmp_bytes_len = sb_block->block_bitmap_sects * SECTOR_SIZE;
    /*从硬盘上读入块位图到分区的block_bitmap.bits*/
    ide_read(hd, sb_block->block_bitmap_lba, cur_part->block_bitmap.bits, sb_block->block_bitmap_sects);
    /*将硬盘上的inode位图读入到内存*/
    cur_part->inode_bitmap.bits = (uint8_t *)sys_malloc(sb_block->inode_bitmap_sects * SECTOR_SIZE);
    if (cur_part->inode_bitmap.bits == NULL)
    {
        PANIC("alloc memory failed!!!\n");
    }
    cur_part->inode_bitmap.btmp_bytes_len = sb_block->inode_bitmap_sects * SECTOR_SIZE;

    ide_read(hd, sb_block->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_block->inode_bitmap_sects);

    list_init(&cur_part->open_inodes);
    printk("mount %s done!\n", part->name);
    return true;
}

// 格式化分区 即初始化分区的元信息 创建文件系统
static void partition_format(struct disk *hd, struct partition *part)
{
    uint32_t boot_sects = 1;
    uint32_t super_block_sects = 1;
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);
    uint32_t inode_table_sects = DIV_ROUND_UP(sizeof(struct inode) * MAX_FILES_PER_PART, SECTOR_SIZE);
    uint32_t used_sects = boot_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
    uint32_t free_sects = part->sec_cnt - used_sects;

    /*简单处理块位图占用的扇区数 有点绕*/
    uint32_t block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
    // block_bitmap_len是除去块位图之后可用块的数量
    uint32_t block_bitmap_len = free_sects - block_bitmap_sects;
    // 此时计算的才是真正块位图占用的扇区数
    block_bitmap_sects = DIV_ROUND_UP(block_bitmap_len, BITS_PER_SECTOR);

    /*超级块初始化*/
    struct super_block sb;
    sb.magic = 0x19590318;
    sb.sec_cnt = part->sec_cnt;
    sb.inode_cnt = MAX_FILES_PER_PART;
    // 就是这个分区起始的逻辑扇区地址 主分区/逻辑分区
    sb.part_lba_base = part->start_lba;
    // 第0块是引导块 第1块是超级块
    sb.block_bitmap_lba = sb.part_lba_base + 2;
    sb.block_bitmap_sects = block_bitmap_sects;

    sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
    sb.inode_bitmap_sects = inode_bitmap_sects;

    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_table_sects = inode_table_sects;

    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
    sb.root_dir_no = 0;
    sb.dir_entry_siz = sizeof(struct dir_entry);

    // 打印信息
    printk("%s info:\n", part->name);
    printk("    magic:0x%x\n", sb.magic);
    printk("    part_lba_base:0x%x\n", sb.part_lba_base);
    printk("    all sectors:0x%x\n", sb.sec_cnt);
    printk("    inode count:0x%x\n", sb.inode_cnt);
    printk("    part lba base:0x%x\n", sb.part_lba_base);
    printk("    block bitmap lba:0x%x\n", sb.block_bitmap_lba);
    printk("    block bitmap sectors:0x%x\n", sb.block_bitmap_sects);
    printk("    inode bitmap lba:0x%x\n", sb.inode_bitmap_lba);
    printk("    inode bitmap sectors:0x%x\n", sb.inode_bitmap_sects);
    printk("    inode table lba:0x%x\n", sb.inode_table_lba);
    printk("    inode table sectors:0x%x\n", sb.inode_table_sects);
    printk("    data start lba:0x%x\n", sb.data_start_lba);
    printk("    root dir no:0x%x\n", sb.root_dir_no);
    printk("    dir entry siz:0x%x\n", sb.dir_entry_siz);

    /*将超级块写入本分区的第二个块*/
    ide_write(hd, sb.part_lba_base + 1, &sb, 1);
    printk("    super block lba:0x%x\n", sb.part_lba_base + 1);

    /*找出数据量最大的元信息，用其尺寸做数据缓冲区*/
    uint32_t buf_siz = (sb.block_bitmap_sects > sb.inode_bitmap_sects ? (sb.block_bitmap_sects > sb.inode_table_sects ? sb.block_bitmap_sects : sb.inode_table_sects) : (sb.inode_bitmap_sects > sb.inode_table_sects ? sb.inode_bitmap_sects : sb.inode_table_sects));
    buf_siz *= 512;

    uint8_t *buf = (uint8_t *)sys_malloc(buf_siz);
    memset(buf, 0, buf_siz);
    /*初始化块位图并写入sb.block_lba_base*/
    buf[0] |= 0x01; // 第0个块先预留给根目录
    /*得出位图所在的扇区中不足一个扇区的部分，即最后一个*/
    uint32_t byte_idx = block_bitmap_len / 8;
    uint32_t bit_idx = block_bitmap_len % 8;
    uint32_t last_siz = SECTOR_SIZE - (byte_idx % SECTOR_SIZE);

    /*1. 先将位图最后一个字节到其所在的扇区的结束全部置1 超出的部分标记为已用*/
    memset(&buf[byte_idx], 0xff, last_siz);
    /*2. 再将上一步中覆盖的最后一个字节的有效位重新置0*/
    uint32_t idx = 0;
    while (idx <= bit_idx)
    {
        buf[byte_idx] &= ~(1 << idx++);
    }
    /*3. 将空闲块位图写入硬盘*/
    ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);

    /*4. 将inode位图写入硬盘*/
    memset(buf, 0, buf_siz);
    buf[0] |= 0x01; // 第0个inode分配给根目录
                    /**
                     * inode的数量与分区最大文件数目相同 都是4096
                     * 因此inode_bitmap的大小正好是1个扇区
                     */
    ide_write(hd, sb.inode_bitmap_lba, buf, 1);

    /*5. 初始化inode table并写入硬盘 只有根目录的inode信息*/
    memset(buf, 0, buf_siz);
    struct inode *i = (struct inode *)buf;
    i->isiz = sb.dir_entry_siz * 2; //.和..
    i->ino = 0;
    i->i_sects[0] = sb.data_start_lba;
    ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);

    /*6. 将根目录写入sb.data_start_lba .和..*/
    memset(buf, 0, buf_siz);
    struct dir_entry *dire = (struct dir_entry *)buf;
    dire->fn[0] = '.';
    dire->ftype = FT_DIRECTORY;
    dire->ino = 0;

    dire++;
    dire->fn[0] = '.';
    dire->fn[1] = '.';
    dire->ino = 0;
    dire->ftype = FT_DIRECTORY;

    ide_write(hd, sb.data_start_lba, buf, 1);
    printk("    root dir lba:0x%x\n", sb.data_start_lba);
    printk("    %s format done\n", part->name);

    sys_free(buf);
}

// 在磁盘上搜索文件系统 若没有格式化分区则创建文件系统
void filesys_init()
{
    uint8_t channel_no = 0;
    uint8_t dev_no;
    uint8_t part_idx = 0;

    /*sb_buf存储从硬盘读入的超级块*/
    struct super_block *sb_buf = sys_malloc(SECTOR_SIZE);
    if (sb_buf == NULL)
    {
        PANIC("alloc memory failed!!!\n");
    }
    printk("searching filesystem......\n");

    while (channel_no < 2)
    {
        dev_no = 0;
        while (dev_no < 2)
        {
            if (dev_no == 0)
            {
                // 跨过硬盘hd60M.img
                dev_no++;
                continue;
            }
            struct disk *hd = &channels[channel_no].devices[dev_no];
            struct partition *part = hd->prim_parts;
            while (part_idx < 12)
            {
                if (part_idx == 4)
                {
                    // 开始处理逻辑分区
                    part = hd->logic_parts;
                }
                if (part->sec_cnt != 0)
                {
                    // 如果分区存在
                    memset(sb_buf, 0, SECTOR_SIZE);
                    // 读入分区的超级块
                    ide_read(hd, part->start_lba + 1, sb_buf, 1);
                    if (sb_buf->magic == 0x19590318)
                    {
                        printk("%s has filesystem\n", part->name);
                    }
                    else
                    {
                        printk("formatting %s's partition %s unknown\n", hd->name, part->name);
                        partition_format(hd, part);
                    }
                }
                part++;
                part_idx++;
            }
            dev_no++;
        }
        channel_no++;
    }
    sys_free(sb_buf);

    char default_part[8] = "sdb1";
    /*挂载分区*/
    list_traversal(&partition_list, mount_partition, (int)default_part);

    // 将当前分区根目录打开
    open_root_dir(cur_part);
    // 初始化文件表
    uint32_t fd_idx = 0;
    while (fd_idx < MAX_FILES_OPEN)
    {
        file_table[fd_idx++].fd_inode = NULL;
    }
}

static int searchf(const char *pathname, struct path_search_record *record)
{
    if (!strcmp("/", pathname) || !strcmp("/.", pathname) || !strcmp("/..", pathname))
    {
        record->parent = &root_dir;
        record->path[0] = 0;
        record->type = FT_DIRECTORY;
        return 0;
    }

    uint32_t plen = strlen(pathname);
    ASSERT(pathname[0] == '/' && plen > 1 && plen < MAX_PATH_LEN);
    const char *subpath = pathname;
    struct dir *parent = &root_dir;
    struct dir_entry dire;
    // /a/b/c -> a b c
    char name[MAX_FN_LEN];
    record->parent = parent;
    record->type = FT_UNKNOWN;
    uint32_t parent_ino = 0;
    memset(name, 0, MAX_FN_LEN);
    subpath = path_parse(subpath, name);
    while (name[0])
    {
        ASSERT(strlen(record->path) < 512);
        // 记录父目录
        strcat(record->path, "/");
        strcat(record->path, name);
        if (search_dire(cur_part, parent, name, &dire))
        {
            if (FT_DIRECTORY == dire.ftype)
            {
                parent_ino = parent->inode->ino;
                dir_close(parent);
                parent = dir_open(cur_part, dire.ino);
                record->parent = parent;
            }
            else
            {
                record->type = FT_REGULAR;
                return dire.ino;
            }
            memset(name, 0, MAX_FN_LEN);
            if (subpath)
            {
                memset(name, 0, MAX_FN_LEN);
                subpath = path_parse(subpath, name);
            }
        }
        else
        {
            return -1;
        }
    }
    dir_close(record->parent);
    // 保存被查找目录的直接父目录
    record->parent = dir_open(cur_part, parent_ino);
    record->type = FT_DIRECTORY;
    // 返回目录inode号
    return dire.ino;
}

int32_t fcreate(struct dir *parent, char *fn, uint8_t flag);
int32_t sys_open(const char *pathname, uint8_t flags)
{
    if (pathname[strlen(pathname) - 1] == '/')
    {
        printk("can not open a directory %s\n", pathname);
        return -1;
    }

    ASSERT(flags <= 7);
    int32_t fd = -1;
    struct path_search_record record;
    memset(&record, 0, sizeof(struct path_search_record));

    // 记录目录深度
    uint32_t depth = path_depth(pathname);
    // 检查文件是否存在
    int32_t ino = searchf(pathname, &record);
    bool found = ino != -1 ? true : false;
    if (record.type == FT_DIRECTORY)
    {
        printk("cannot open directory , use opendir() instead!!!\n");
        dir_close(record.parent);
        return -1;
    }
    uint32_t searched = path_depth(record.path);
    // 是否所有中间的目录都被访问到
    if (depth != searched)
    {
        /*某个中间目录并不存在*/
        printk("cannot access %s: Not a directory , subpath %s not existed!!!\n", pathname, record.path);
        dir_close(record.parent);
        return -1;
    }
    if (!found && !(flags & O_CREATE))
    {
        /**并未找到并且不是要创建文件
         */
        printk("in path %s , file %s is not exist!!!\n", record.path, (strrchr(record.path, '/') + 1));
        dir_close(record.parent);
        return -1;
    }
    else if (found && (flags & O_CREATE))
    {
        // 若要创建的文件已经存在
        printk("%s has already exist!\n", pathname);
        dir_close(record.parent);
        return -1;
    }

    switch (flags & O_CREATE)
    {
    case O_CREATE:
        printk("creating file\n");
        fd = fcreate(record.parent, (char *)(strrchr(pathname, '/') + 1), flags);
        dir_close(record.parent);
        break;
    default:
        // 其余情况均为打开已存在的文件
        fd = fopen(ino, flags);
    }

    return fd;
}

// 文件描述符转化为文件表下标
static int32_t fd_locl2glob(uint32_t locl_fd)
{
    struct task_struct *cur = running_thread();
    int32_t glob = cur->fd_table[locl_fd];
    ASSERT(glob >= 0 && glob < MAX_FILES_OPEN);
    return glob;
}

// 关闭文件描述符指向的文件
int32_t sys_close(int32_t locl_fd)
{
    int32_t ret = -1;
    if (locl_fd > 2)
    {
        int32_t fd = fd_locl2glob(locl_fd);
        ret = fclose(&file_table[fd]);
        running_thread()->fd_table[locl_fd] = -1;
    }
    return ret;
}

// 将buf中连续count个字节写入fd 成功返回描述符数 失败返回-1
int32_t sys_write(int32_t fd, const void *buf, uint32_t count)
{
    if (fd < 0)
    {
        printk("sys_write: fd error!!!\n");
        return -1;
    }
    if (fd == stdout)
    {
        console_put_str((char *)buf);
        return count;
    }
    fd = fd_locl2glob(fd);
    struct file *f = &file_table[fd];
    if (f->fd_flag & O_WRONLY || f->fd_flag & O_RDWR)
    {
        uint32_t bwritten = fwrite(f, buf, count);
        return bwritten;
    }
    else
    {
        printk("error flags: not O_WRONLY or O_RDWR!!!\n");
    }
    return -1;
}

int32_t sys_read(int32_t fd, void *buf, uint32_t count)
{
    if (fd < 0)
    {
        printk("sys_read: fd error!!!\n");
        return -1;
    }
    ASSERT(buf != NULL);
    fd = fd_locl2glob(fd);
    return fread(&file_table[fd], buf, count);
}

// 重置文件的读写指针
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence)
{
    if (fd < 0)
    {
        printk("sys_lseek: fd error!!!\n");
        return -1;
    }
    ASSERT(whence > 0 && whence < 4);
    fd = fd_locl2glob(fd);
    struct file *f = &file_table[fd];
    int32_t newp = 0;
    int32_t fsiz = (int32_t)f->fd_inode->isiz;
    switch (whence)
    {
    case SEEK_SET:
        newp = offset;
        break;
    case SEEK_CUR:
        newp = (int32_t)f->fd_pos + offset;
        break;
    case SEEK_END:
        newp = fsiz + offset;
        break;
    default:
        break;
    }
    if (newp < 0 || newp >= fsiz)
    {
        return -1;
    }
    f->fd_pos = newp;
    return newp;
}

// 删除文件(非目录) 成功返回0 失败返回-1
int32_t sys_unlink(const char *pathname)
{
    ASSERT(strlen(pathname) < MAX_PATH_LEN);
    // 先检查待删除的文件是否存在
    struct path_search_record record;
    memset(&record, 0, sizeof(struct path_search_record));
    int32_t ino = searchf(pathname, &record);
    if (ino == -1)
    {
        printk("file %s not found!!!\n", pathname);
        dir_close(record.parent);
        return -1;
    }
    if (record.type == FT_DIRECTORY)
    {
        printk("cannot delete a directory with unlink(), use rmdir() instead!!!\n");
        dir_close(record.parent);
        return -1;
    }
    // 检查是否在已打开的文件列表中
    uint32_t fd_idx = 0;
    while (fd_idx < MAX_FILES_OPEN)
    {
        if (file_table[fd_idx].fd_inode != NULL && file_table[fd_idx].fd_inode->ino == ino)
        {
            break;
        }
        fd_idx++;
    }
    if (fd_idx < MAX_FILES_OPEN)
    {
        dir_close(record.parent);
        printk("file %s is in use!!!\n", pathname);
        return -1;
    }
    ASSERT(fd_idx == MAX_FILES_OPEN);
    void *io_buf = (void *)sys_malloc(SECTOR_SIZE << 1);
    if (io_buf == NULL)
    {
        dir_close(record.parent);
        printk("sys_unlink: malloc failed!!!\n");
        return -1;
    }
    memset(io_buf, 0, 1024);
    struct dir *parent = record.parent;
    del_dire(cur_part, parent, ino, io_buf);
    inode_release(cur_part, ino);
    dir_close(parent);
    sys_free(io_buf);
    return 0;
}

// 创建目录dn 成功返回0 失败返回-1
int32_t sys_mkdir(const char *dn)
{
    uint8_t rollback = 0;
    void *io_buf = sys_malloc(SECTOR_SIZE << 1);
    if (io_buf == NULL)
    {
        printk("sys_mkdir: malloc for io_buf failed!!!\n");
        return -1;
    }
    struct path_search_record record;
    memset(&record, 0, sizeof(struct path_search_record));
    int32_t ino = searchf(dn, &record);
    if (ino != -1)
    {
        printk("sys_mkdir: file or directory exist!!!\n");
        rollback = 1;
        goto _rollback;
    }
    else
    {
        // 没有找到 是仅仅要创建的未找到 还是中间目录不存在
        uint32_t depth = path_depth(dn);
        uint32_t searched = path_depth((const char *)record.path);
        if (depth != searched)
        {
            printk("sys_mkdir: cannot access %s: not a directory,subpath %s not exist!!!\n", dn, record.path);
            rollback = 1;
            goto _rollback;
        }
    }

    struct dir *parent = record.parent;
    // 目录名称后可能会有 '/' 最好直接用record.path
    char *name = strrchr(record.path, '/') + 1;
    ino = inode_bitmap_alloc(cur_part);
    if (ino == -1)
    {
        printk("sys_mkdir: failed allocate inode!!!\n");
        rollback = 1;
        goto _rollback;
    }

    struct inode inode;
    inode_init(ino, &inode);

    uint32_t blk_bitmap_idx;
    int32_t blk_lba = -1;
    blk_lba = block_bitmap_alloc(cur_part);
    if (blk_lba == -1)
    {
        printk("sys_mkdir: fail to allocate block to new directory!!!\n");
        rollback = 2;
        goto _rollback;
    }

    inode.i_sects[0] = blk_lba;
    blk_bitmap_idx = blk_lba - cur_part->sb->data_start_lba;
    ASSERT(blk_bitmap_idx != 0);
    bitmap_sync(cur_part, blk_bitmap_idx, BLOCK_BITMAP);
    // 将当前目录项"."和".."写入目录
    memset(io_buf, 0, 1024);
    struct dir_entry *dire = (struct dir_entry *)io_buf;
    // 初始化"."和".."
    memcpy(dire->fn, ".", 1);
    dire->ino = ino;
    dire->ftype = FT_DIRECTORY;
    dire++;
    memcpy(dire->fn, "..", 2);
    dire->ino = parent->inode->ino;
    dire->ftype = FT_DIRECTORY;

    ide_write(cur_part->my_disk, inode.i_sects[0], io_buf, 1);
    inode.isiz = (cur_part->sb->dir_entry_siz << 1);
    // 在父目录中添加自己的目录项
    struct dir_entry self;
    memset(&self, 0, sizeof(struct dir_entry));
    create_dire(name, ino, FT_DIRECTORY, &self);
    memset(io_buf, 0, 1024);

    if (!sync_dire(parent, &self, io_buf))
    {
        printk("sys_mkdir: synchronize directory entry to disk failed!!!\n");
        rollback = 2;
        goto _rollback;
    }

    // 同步父目录的inode
    memset(io_buf, 0, 1024);
    inode_sync(cur_part, parent->inode, io_buf);
    printk("parent: NO.%d , size:%d\n", parent->inode->ino, parent->inode->isiz);

    // 同步新创建的inode
    memset(io_buf, 0, 1024);
    inode_sync(cur_part, &inode, io_buf);
    printk("inode: NO.%d created , blk_lba:%d size:%d\n", ino, blk_lba,inode.isiz);

    // 同步inode位图
    bitmap_sync(cur_part, ino, INODE_BITMAP);

    sys_free(io_buf);

    dir_close(record.parent);
    return 0;

_rollback:
    switch (rollback)
    {
    case 2:
        bitmap_set(&cur_part->inode_bitmap, ino, 0);
    case 1:
        dir_close(record.parent);
        break;
    default:
        break;
    }
    sys_free(io_buf);
    return -1;
}

struct dir *sys_opendir(const char *name)
{
    ASSERT(strlen(name) < MAX_FN_LEN);
    if (!strcmp(name, "/") || !strcmp(name, "/."))
    {
        return &root_dir;
    }
    // 检查待打开的目录是否存在
    struct path_search_record record;
    memset(&record, 0, sizeof(struct path_search_record));
    int32_t ino = searchf(name, &record);
    struct dir *ret = NULL;
    if (ino == -1)
    {
        printk("In %s, subpath %s not exist!!!\n", name, record.path);
    }
    else
    {
        if (record.type == FT_REGULAR)
        {
            printk("%s is regular file!!!\n", name);
        }
        else if (record.type == FT_DIRECTORY)
        {
            ret = dir_open(cur_part, ino);
        }
    }
    dir_close(record.parent);
    return ret;
}

int32_t sys_closedir(struct dir *dir)
{
    int32_t ret = -1;
    if (dir != NULL)
    {
        dir_close(dir);
        ret = 0;
    }
    return ret;
}

struct dir_entry *sys_readdir(struct dir *dir)
{
    ASSERT(dir != NULL);
    return dir_read(dir);
}

void sys_rewinddir(struct dir *dir)
{
    dir->dir_pos = 0;
}

// 删除空目录 成功返回0 失败时返回-1
int32_t sys_rmdir(const char *pathname)
{
    struct path_search_record record;
    memset(&record, 0, sizeof(struct path_search_record));
    int32_t ino = searchf(pathname, &record);
    ASSERT(ino != 0);
    int ret = -1;
    if (ino == -1)
    {
        printk("In %s, subpath %s not exist!!!\n", pathname, record.path);
    }
    else
    {
        if (record.type == FT_REGULAR)
        {
            printk("%s is regular file!!!\n", pathname);
        }
        else
        {
            struct dir *dir = dir_open(cur_part, ino);
            if (!dir_is_empty(dir))
            {
                printk("not allowed to delete a non-empty directory!!!\n");
            }
            else
            {
                if (!dir_remove(record.parent, dir))
                {
                    ret = 0;
                }
            }
            dir_close(dir);
        }
    }
    dir_close(record.parent);
    return ret;
}

// 获取父目录的inode编号
static uint32_t get_parent_dir_inode_nr(uint32_t chld_i_nr, void *io_buf)
{
    struct inode *chld_dir_i = iopen(cur_part, chld_i_nr);
    // 目录中的目录项".."中包括父目录inode编号，".."位于第0块
    uint32_t blk_lba = chld_dir_i->i_sects[0];
    ASSERT(blk_lba >= cur_part->sb->data_start_lba);
    iclose(chld_dir_i);
    ide_read(cur_part->my_disk, blk_lba, io_buf, 1);
    struct dir_entry *dire = (struct dir_entry *)io_buf;
    // 第0个目录项是"." 第一个目录项是".."
    ASSERT(dire[1].ino < 4096 && dire[1].ftype == FT_DIRECTORY);
    return dire[1].ino;
}

// 在inode编号为pino的目录中查找inode编号为cino的子目录的名字 将名字存入缓冲区path 成功返回0 失败返回-1
static int32_t get_chld_dn(uint32_t pino, uint32_t cino, char *path, void *io_buf)
{
    struct inode *parent = iopen(cur_part, pino);
    uint8_t blk_idx = 0;
    uint32_t all_blocks[140] = {0};
    uint32_t blk_cnt = 12;
    while (blk_idx < 12)
    {
        all_blocks[blk_idx] = parent->i_sects[blk_idx];
        blk_idx++;
    }
    if (parent->i_sects[12])
    {
        ide_read(cur_part->my_disk, parent->i_sects[12], all_blocks + 12, 1);
        blk_cnt = 140;
    }
    iclose(parent);
    struct dir_entry *dire = (struct dir_entry *)io_buf;
    uint32_t dire_siz = cur_part->sb->dir_entry_siz;
    uint32_t dires = SECTOR_SIZE / dire_siz;
    blk_idx = 0;
    while (blk_idx < blk_cnt)
    {
        if (all_blocks[blk_idx])
        {
            ide_read(cur_part->my_disk, all_blocks[blk_idx], io_buf, 1);
            uint8_t dire_idx = 0;
            while (dire_idx < dires)
            {
                if ((dire + dire_idx)->ino == cino)
                {
                    strcat(path, "/");
                    strcat(path, (dire + dire_idx)->fn);
                    return 0;
                }
                dire_idx++;
            }
        }
        blk_idx++;
    }
    return -1;
}

// 将当前工作目录的绝对路径写入到buf size是buf的大小
// 当buf为NULL时 由操作系统分配存储工作路径的空间并返回地址
// 失败返回NULL
char *sys_getcwd(char *buf, uint32_t size)
{
    // 确保buf不为空 若用户进程提供的buf为NULL 系统调用getcwd中要为用户进程通过malloc分配内存
    ASSERT(buf != NULL);
    void *io_buf = sys_malloc(SECTOR_SIZE);
    if (io_buf == NULL)
    {
        return NULL;
    }
    memset(io_buf, 0, SECTOR_SIZE);
    struct task_struct *cur = running_thread();
    int32_t parent_ino = 0;
    int32_t chld_ino = cur->cwd_ino;
    ASSERT(chld_ino >= 0 && chld_ino < 4096);
    // 若当前目录是根目录 直接返回"/"
    if (chld_ino == 0)
    {
        buf[0] = '/';
        buf[1] = 0;
        return buf;
    }
    memset(buf, 0, size);
    char full_path[MAX_PATH_LEN] = {0};
    // 自下而上逐层找父目录
    while (chld_ino)
    {
        parent_ino = get_parent_dir_inode_nr(chld_ino, io_buf);
        memset(io_buf, 0, SECTOR_SIZE);
        if (get_chld_dn(parent_ino, chld_ino, full_path, io_buf) == -1)
        {
            sys_free(io_buf);
            return NULL;
        }
        chld_ino = parent_ino;
    }
    ASSERT(strlen(full_path) <= size);
    // 至此full_path的目录是反着的
    char *last_slash;
    while ((last_slash = strrchr(full_path, '/')))
    {
        uint16_t len = strlen(buf);
        strcpy(buf + len, last_slash);
        // 将full_path截短
        *last_slash = 0;
    }

    sys_free(io_buf);
    return buf;
}

// 更改当前工作目录为绝对路径path 成功返回0 失败返回-1
int32_t sys_chdir(const char *path)
{
    int32_t ret = -1;
    struct path_search_record record;
    memset(&record, 0, sizeof(struct path_search_record));
    int32_t ino = searchf(path, &record);
    if (ino != -1)
    {
        if (record.type == FT_DIRECTORY)
        {
            running_thread()->cwd_ino = ino;
            ret = 0;
        }
        else
        {
            printk("sys_chdir: %s is regular file or other!\n", path);
        }
    }
    dir_close(record.parent);
    return ret;
}

// 在buf中填充文件结构相关信息 成功时返回0 失败时返回-1
int32_t sys_stat(const char *path, struct stat *buf)
{
    // 若直接查看根目录 '/'
    if (!strcmp(path, "/") || !strcmp(path, "/.") || !strcmp(path, "/.."))
    {
        buf->st_ftype = FT_DIRECTORY;
        buf->st_ino = 0;
        buf->st_size = root_dir.inode->isiz;
        return 0;
    }

    int32_t ret = -1;
    struct path_search_record record;
    memset(&record, 0, sizeof(struct path_search_record));
    int32_t ino = searchf(path, &record);
    if (ino != -1)
    {
        struct inode *obj = iopen(cur_part, ino);
        buf->st_size = obj->isiz;
        buf->st_ftype = record.type;
        buf->st_ino = ino;
        iclose(obj);
        ret = 0;
    }
    else
    {
        printk("sys_stat: %s not found\n", path);
    }
    dir_close(record.parent);
    return ret;
}