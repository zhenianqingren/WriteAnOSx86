#include "fork.h"
#include "process.h"
#include "global.h"
#include "file.h"
#include "string.h"

extern void intr_exit(void);

static int32_t copy_pcb(struct task_struct *chld, struct task_struct *parent)
{
    // 复制PCB所在的整个页
    memcpy(chld, parent, PG_SIZE);
    // 根据情况单独修改
    chld->pid = fork_pid();
    chld->elapsed_ticks = 0;
    chld->status = TASK_READY;
    chld->ticks = chld->priority;
    chld->ppid = parent->pid;
    chld->general_tag.prev = NULL;
    chld->general_tag.next = NULL;
    chld->all_list_tag.prev = NULL;
    chld->all_list_tag.next = NULL;
    block_desc_init(chld->u_block_desc);
    // 复制父进程的虚拟地址池的位图
    uint32_t btmp_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
    void *vaddr_btmp = get_kernel_pages(btmp_pg_cnt);
    // 将子进程的虚拟地址池指向自己的位图
    memcpy(vaddr_btmp, chld->userprog_vaddr.vaddr_bitmap.bits, btmp_pg_cnt * PG_SIZE);
    chld->userprog_vaddr.vaddr_bitmap.bits = vaddr_btmp;
    ASSERT(strlen(chld->name) < 11);
    strcat(chld->name, "_fork");
    return 0;
}

// 复制进程体以及用户栈
static void copy_body(struct task_struct *chld, struct task_struct *parent, void *buf_page)
{
    uint8_t *vaddr_btmp = parent->userprog_vaddr.vaddr_bitmap.bits;
    uint32_t btmp_bytes_len = parent->userprog_vaddr.vaddr_bitmap.btmp_bytes_len;
    uint32_t vaddr_start = parent->userprog_vaddr.vaddr_start;
    uint32_t idx_byte = 0;
    uint32_t idx_bit = 0;
    uint32_t prog_vaddr = 0;

    // 在父进程空间中查找已有数据的页
    while (idx_byte < btmp_bytes_len)
    {
        if (vaddr_btmp[idx_byte])
        {
            idx_bit = 0;
            while (idx_bit < 8)
            {
                if ((1 << idx_bit) & vaddr_btmp[idx_byte])
                {
                    prog_vaddr = vaddr_start + (idx_byte * 8 + idx_bit) * PG_SIZE;
                    // 通过内核空间做中转(3~4GB被映射到所有进程) 将父进程用户空间数据复制到子进程
                    // 将父进程在用户空间中的数据复制到内核缓冲区buf_page,目的是切换子进程的页表后还能访问父进程数据
                    memcpy(buf_page, (void *)prog_vaddr, PG_SIZE);
                    // 将页表切换到子进程,避免下面申请内存的函数将pte以及pde安装在父进程页表中
                    page_dir_activate(chld);
                    // 申请虚拟地址prog_vaddr
                    get_a_page_without_opvaddrbtmp(PF_USER, prog_vaddr);
                    // 从内核缓冲区将父进程数据复制到子进程
                    memcpy((void *)prog_vaddr, buf_page, PG_SIZE);
                    // 恢复父进程页表 取其他数据
                    page_dir_activate(parent);
                }
                idx_bit++;
            }
        }
        idx_byte++;
    }
}

// 为子进程构建thread_stack
static int32_t build_chld_stack(struct task_struct *chld)
{
    struct intr_stack *stack0 = (struct intr_stack *)((uint32_t)chld + PG_SIZE - sizeof(struct intr_stack));
    // 修改子进程返回值为0
    stack0->eax = 0;
    // 为switch_to构建thread_stack
    uint32_t *ret_addr = (uint32_t *)stack0 - 1;
    uint32_t *ebp_ptr = (uint32_t *)stack0 - 5;

    *ret_addr = (uint32_t)intr_exit;
    chld->self_kstack = ebp_ptr;
    return 0;
}

// 更新inode打开数
static void update_iopen_cnt(struct task_struct *pthread)
{
    int32_t locl_fd = 3;
    int32_t glob_fd = 0;
    while (locl_fd < MAX_FILES_OPEN_PER_PROC)
    {
        glob_fd = pthread->fd_table[locl_fd];
        ASSERT(glob_fd < MAX_FILES_OPEN);
        if (glob_fd != -1)
        {
            file_table[glob_fd].fd_inode->iopen_cnt++;
        }
        locl_fd++;
    }
}

// 拷贝父进程本身所占资源给子进程
static int32_t copy_process(struct task_struct *chld, struct task_struct *parent)
{
    // 内核缓冲区 作为父进程用户空间的数据复制到子进程用户空间的中转
    void *buf_page = get_kernel_pages(1);
    if (buf_page == NULL)
    {
        return -1;
    }
    // PCB 虚拟地址位图 内核栈
    if (copy_pcb(chld, parent) == -1)
    {
        return -1;
    }
    // 为子进程创建页表 仅包含内核空间
    chld->pgdir = create_page_dir();
    if (chld->pgdir == NULL)
    {
        return -1;
    }
    // 复制父进程进程体以及用户栈给子进程
    copy_body(chld, parent, buf_page);
    // 构建子进程thread_stack以及修改返回值pid
    build_chld_stack(chld);
    // 更新文件inode的打开数
    update_iopen_cnt(chld);

    mfree_page(PF_KERNEL, buf_page, 1);
    return 0;
}

// fork子进程 内核线程不可直接调用
pid_t sys_fork(void)
{
    struct task_struct *parent = running_thread();
    struct task_struct *chld = get_kernel_pages(1);
    if (chld == NULL)
    {
        return -1;
    }
    ASSERT(INTR_OFF == intr_get_status() && parent->pgdir != NULL);
    if (copy_process(chld, parent) == -1)
    {
        return -1;
    }
    // 添加到就绪进程队列和所有进程队列 子进程由调试器安排执行
    ASSERT(!elem_find(&thread_ready_list, &chld->general_tag));
    list_append(&thread_ready_list, &chld->general_tag);
    ASSERT(!elem_find(&thread_all_list, &chld->all_list_tag));
    list_append(&thread_all_list, &chld->all_list_tag);

    return chld->pid;
}