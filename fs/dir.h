#ifndef __FS__DIR_H
#define __FS__DIR_H

#include "inode.h"
#include "../lib/stdint.h"
#include "fs.h"
#define MAX_FN_LEN 16

/*目录结构 在内存中创建 并不会写入硬盘*/
struct dir
{
    struct inode *inode;
    uint32_t dir_pos;     // 记录在目录内的偏移
    uint8_t dir_buf[512]; // 目录的数据缓存 存储返回的目录项
};

extern struct dir root_dir;
/*目录项结构*/
struct dir_entry
{
    char fn[MAX_FN_LEN]; // 文件名
    uint32_t ino;        // inode编号
    enum f_type ftype;
};

void open_root_dir(struct partition *part);
struct dir *dir_open(struct partition *part, uint32_t ino);
bool search_dire(struct partition *part, struct dir *pdir, const char *name, struct dir_entry *dire);
void dir_close(struct dir *dir);
void create_dire(char *fn, uint32_t ino, uint8_t ftype, struct dir_entry *pdire);
bool sync_dire(struct dir *parent, struct dir_entry *pdire, void *io_buf);
const char *path_parse(const char *path_name, char *name_store);
int32_t path_depth(const char *path_name);
bool del_dire(struct partition *part, struct dir *dir, uint32_t ino, void *io_buf);
struct dir_entry *dir_read(struct dir *dir);
bool dir_is_empty(struct dir *dir);
int32_t dir_remove(struct dir *parent, struct dir *child);
#endif