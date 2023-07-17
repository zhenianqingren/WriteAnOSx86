#include "process_ctl.h"
#include "../lib/stdint.h"
#include "thread.h"
#include "fs.h"
#include "memory.h"
#include "debug.h"
#include "kio.h"
#include "pipe.h"
/**
 * 释放用户资源
 * 页表中对应的物理页
 * 虚拟内存池占用的物理页框
 * 关闭打开的文件
 */

static void release_presource(struct task_struct *pthread)
{
    uint32_t *pgdir = pthread->pgdir;
    uint16_t upde = 768;
    uint16_t pde_idx = 0;
    uint32_t pde = 0;
    uint32_t *v_pde_ptr = NULL;

    uint16_t upte = 1024;
    uint16_t pte_idx = 0;
    uint32_t pte = 0;
    uint32_t *v_pte_ptr = NULL;

    uint32_t *firt_pte = NULL;
    uint32_t pg_phy_addr = 0;

    // 回收页表中用户空间的页框
    while (pde_idx < upde)
    {
        v_pde_ptr = pgdir + pde_idx;
        pde = *v_pde_ptr;
        if (pde & 0x1)
        {
            // 该页目录项下可能有页表项
            firt_pte = pte_ptr(pde_idx * 0x400000);
            // 一个页表表示的内存是4MB
            pte_idx = 0;
            while (pte_idx < upte)
            {
                v_pte_ptr = firt_pte + pte_idx;
                pte = *v_pte_ptr;
                if (pte & 0x1)
                {
                    pg_phy_addr = pte & 0xfffff000;
                    pfree(pg_phy_addr);
                }
                pte_idx++;
            }
            pg_phy_addr = pde & 0xfffff000;
            pfree(pg_phy_addr);
        }
        pde_idx++;
    }

    // 回收虚拟地址池所占的物理内存
    uint32_t btmp_cnt = pthread->userprog_vaddr.vaddr_bitmap.btmp_bytes_len / PG_SIZE;
    uint8_t *usr_btmp = pthread->userprog_vaddr.vaddr_bitmap.bits;
    mfree_page(PF_KERNEL, usr_btmp, btmp_cnt);

    // 关闭进程打开的文件
    uint8_t fdi = 3;
    while (fdi < MAX_FILES_OPEN_PER_PROC)
    {
        if (pthread->fd_table[fdi] != -1)
        {
            sys_close(fdi);
        }
        fdi++;
    }
}

// 查找某进程的父进程是否是ppid
static bool find_chld(struct list_elem *e, pid_t ppid)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, e);
    if (pthread->ppid == ppid)
    {
        return true;
    }
    return false;
}

// 查找状态为TASK_HANGING的任务
static bool find_hanging_chld(struct list_elem *e, pid_t ppid)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, e);
    if (pthread->ppid == ppid && pthread->status == TASK_HANGING)
    {
        return true;
    }
    return false;
}

// 使init成为一个子进程的父进程
static bool init_handle_chld(struct list_elem *e, pid_t pid)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, e);
    if (pthread->ppid == pid)
    {
        pthread->ppid = 3;
    }
    return false;
}

pid_t sys_wait(int16_t *status)
{
    struct task_struct *parent = running_thread();
    while (1)
    {
        // 优先处理已经是挂起状态的任务
        struct list_elem *childe = list_traversal(&thread_all_list, find_hanging_chld, parent->pid);
        if (childe != NULL)
        {
            struct task_struct *child = elem2entry(struct task_struct, all_list_tag, childe);
            *status = child->exit_status;
            // thread_exit后pcb会被回收，提前获取pid
            pid_t cpid = child->pid;
            printk("parent %d will handle hanging child %d\n", parent->pid, child->pid);
            thread_exit(child, false);
            return cpid;
        }
        // 判断是否有子进程
        childe = list_traversal(&thread_all_list, find_chld, parent->pid);
        if (childe == NULL)
        {
            return -1;
        }
        else
        {
            printk("parent %d will block to handle child\n", parent->pid);
            thread_block(TASK_WAITING);
        }
    }
    return -1;
}

void sys_exit(int32_t status)
{
    struct task_struct *child = running_thread();
    child->exit_status = status;
    if (child->ppid == -1)
    {
        PANIC("sys_exit: child without legal parent!\n");
    }
    // 将child的所有子进程的父亲改为init
    list_traversal(&thread_all_list, init_handle_chld, child->pid);

    printk("child %d ready to exit\n", child->pid);
    release_presource(child);

    // 如果父进程正在等待子进程退出
    struct task_struct *parent = pid2thread(child->ppid);

    if (parent->status == TASK_WAITING)
    {
        printk("child %d exit with parent %d\n", child->pid, parent->pid);
        thread_unblock(parent);
    }
    // 将自己挂起等待父进程回收
    thread_block(TASK_HANGING);

    // 永远不会执行至此
}