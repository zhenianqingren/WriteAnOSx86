#include "exec.h"
#include "global.h"
#include "../kernel/memory.h"
#include "syscall.h"
#include "thread.h"
#include "fs.h"
#include "../lib/stdio.h"

extern void intr_exit(void);
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

// 32bit ELF头
// Elf32位的头
struct Elf32_Ehdr
{
    unsigned char e_ident[16]; // 魔数
    Elf32_Half e_type;         // 目标文件类型
    Elf32_Half e_machine;      // 体系结构 EM_386 3
    Elf32_Word e_version;      // 版本 1
    Elf32_Addr e_entry;        // 入口地址
    Elf32_Off e_phoff;         // Programme Header Off
    Elf32_Off e_shoff;         // Segment Header Off
    Elf32_Word e_flags;        // 0x0
    Elf32_Half e_ehsize;       // Elfsize
    Elf32_Half e_phentsize;    // Programme Header Size
    Elf32_Half e_phnum;        // num of Programme Header
    Elf32_Half e_shentsize;    // Segment Header Size
    Elf32_Half e_shnum;        // Segment Header Number
    Elf32_Half e_shstmdx;
};

// 程序头表
struct Elf32_Phdr
{
    Elf32_Word p_type;
    Elf32_Off p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
};

// 段类型
enum segment_type
{
    PT_NULL,
    PT_LOAD,    // 可加载程序段
    PT_DYNAMIC, // 动态加载信息
    PT_INTERP,  // 动态加载器名称
    PT_NOTE,    // 辅助信息
    PT_SHLIB,   // 保留
    PT_PHDR     // 程序头表
};

// 将文件描述符fd指向的文件中，偏移为offset，
// 大小为filesz的段加载到虚拟地址为vaddr的内存
static bool segment_load(int32_t fd, uint32_t offset, uint32_t filesz, uint32_t vaddr)
{
    uint32_t vaddr_first_page = vaddr & 0xfffff000;
    // vaddr所在的页框
    uint32_t siz_in_first_page = PG_SIZE - (vaddr & 0x00000fff);
    // 加载到内存后，文件在第一个页框中占用的字节大小
    uint32_t occupy_pages = 0;
    // 若一个页框容不下该段
    if (filesz > siz_in_first_page)
    {
        uint32_t left = filesz - siz_in_first_page;
        occupy_pages = DIV_ROUND_UP(left, PG_SIZE) + 1;
    }
    else
    {
        occupy_pages = 1;
    }
    // 为进程分配内存
    uint32_t page_idx = 0;
    uint32_t vaddr_page = vaddr_first_page;
    while (page_idx < occupy_pages)
    {
        uint32_t *pde = pde_ptr(vaddr_page);
        uint32_t *pte = pte_ptr(vaddr_page);
        if (!(*pde & 0x00000001) || !(*pte & 0x00000001))
        {
            if (get_one_page(PF_USER, vaddr_page) == NULL)
            {
                return false;
            }
        }
        // 如果已经分配，利用现有物理页直接覆盖进程体
        vaddr_page += PG_SIZE;
        page_idx++;
    }
    sys_lseek(fd, offset, SEEK_SET);
    sys_read(fd, (void *)vaddr, filesz);

    printf("segment load finish!\n");
    return true;
}

// 从文件系统加载用户程序pathname
// 成功返回程序起始地址，失败返回-1
static int32_t load(const char *pathname)
{
    int32_t ret = -1;
    struct Elf32_Ehdr elf_header;
    struct Elf32_Phdr prog_header;
    memset(&elf_header, 0, sizeof(struct Elf32_Ehdr));

    int32_t fd = sys_open(pathname, O_RDONLY);
    if (fd == -1)
    {
        return -1;
    }
    if (sys_read(fd, &elf_header, sizeof(struct Elf32_Ehdr)) != sizeof(struct Elf32_Ehdr))
    {
        ret = -1;
        goto done;
    }
    // 校验elf头
    if (memcmp(elf_header.e_ident, "\177ELF\1\1\1", 7) || elf_header.e_type != 2 || elf_header.e_machine != 3 || elf_header.e_version != 1 || elf_header.e_phnum > 1024 || elf_header.e_phentsize != sizeof(struct Elf32_Phdr))
    {
        ret = -1;
        goto done;
    }

    Elf32_Off phdr_offset = elf_header.e_phoff;
    Elf32_Half phdr_siz = elf_header.e_phentsize;
    // 遍历所有程序头
    uint32_t prog_idx = 0;
    while (prog_idx < elf_header.e_phnum)
    {
        memset(&prog_header, 0, phdr_siz);
        // 将文件的指针定位到程序头
        sys_lseek(fd, phdr_offset, SEEK_SET);
        // 只获取程序头
        if (sys_read(fd, &prog_header, phdr_siz) != phdr_siz)
        {
            ret = -1;
            goto done;
        }
        // 如果是可加载段就调用segment_load加载到内存
        if (PT_LOAD == prog_header.p_type)
        {
            if (!segment_load(fd, prog_header.p_offset, prog_header.p_filesz, prog_header.p_vaddr))
            {
                ret = -1;
                goto done;
            }
        }

        phdr_offset += phdr_siz;
        prog_idx++;
    }
    ret = elf_header.e_entry;

done:
    sys_close(fd);
    return ret;
}

// 用path指向的程序替换当前进程
int32_t sys_execv(const char *path, const char *argv[])
{
    uint32_t argc = 0;
    while (argv[argc])
    {
        argc++;
    }
    int32_t entryp = load(path);
    if (entryp == -1)
    {
        return -1;
    }

    struct task_struct *cur = running_thread();
    // 修改进程名
    memcpy(cur->name, path, 32);
    cur->name[32] = 0;

    struct intr_stack *stack0 = (struct intr_stack *)((uint32_t)cur + PG_SIZE - sizeof(struct intr_stack));
    // 参数传递给用户进程
    stack0->ebx = (int32_t)argv;
    stack0->ecx = argc;
    stack0->eip = (void *)entryp;
    // 使新用户进程的栈地址为最高用户空间地址
    stack0->esp = (void *)0xc0000000;
    // exec不同于fork，为了使新进程更快被执行，直接从中断返回
    printf("change ready!\n");
    asm volatile("movl %0,%%esp;jmp intr_exit" ::"g"(stack0)
                 : "memory");
    return 0;
}