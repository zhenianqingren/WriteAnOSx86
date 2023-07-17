#ifndef __FS__FS_H
#define __FS__FS_H

// 每个分区的最大文件数
#define MAX_FILES_PER_PART 4096
// 扇区字节大小
#define SECTOR_SIZE 512
// 块大小
#define BLOCK_SIZE SECTOR_SIZE
// 每个扇区的位数
#define BITS_PER_SECTOR (SECTOR_SIZE * 8)
// 路径最大长度
#define MAX_PATH_LEN 512

#include "../lib/stdint.h"
#include "ide.h"

/*文件类型*/
enum f_type
{
    FT_UNKNOWN,  // 不支持的文件类型
    FT_REGULAR,  // 普通文件
    FT_DIRECTORY // 目录文件
};
/*打开文件的选项*/
enum oflags
{
    O_RDONLY,     // 只读
    O_WRONLY,     // 只写
    O_RDWR,       // 读写
    O_CREATE = 4, // 创建
    PIPE = 64
};

/*文件读写位置偏移量*/
enum whence
{
    SEEK_SET = 1,
    SEEK_CUR,
    SEEK_END
};

void filesys_init();
extern struct partition *cur_part;

// 记录查找路径时已经走过的上级路径
struct path_search_record
{
    char path[MAX_PATH_LEN];
    struct dir *parent; // 文件或目录所在直接父目录
    enum f_type type;   // 找到的文件类型
};

// 文件属性结构体
struct stat
{
    uint32_t st_ino;      // inode编号
    uint32_t st_size;     // 尺寸
    enum f_type st_ftype; // 文件类型
};

int32_t sys_open(const char *pathname, uint8_t flags);
int32_t sys_close(int32_t locl_fd);
int32_t sys_write(int32_t fd, const void *buf, uint32_t count);
int32_t sys_read(int32_t fd, void *buf, uint32_t count);
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence);
int32_t sys_unlink(const char *pathname);
int32_t sys_mkdir(const char *dn);
struct dir *sys_opendir(const char *name);
int32_t sys_closedir(struct dir *dir);
struct dir_entry *sys_readdir(struct dir *dir);
void sys_rewinddir(struct dir *dir);
int32_t sys_chdir(const char *path);
int32_t sys_stat(const char *path, struct stat *buf);
int32_t sys_rmdir(const char *pathname);
char *sys_getcwd(char *buf, uint32_t size);
int32_t fd_locl2glob(uint32_t locl_fd);
#endif