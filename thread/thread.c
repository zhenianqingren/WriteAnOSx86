#include "thread.h"
#include "stdint.h"
#include "memory.h"
#include "global.h"
#include "string.h"
#include "debug.h"
#include "interrupt.h"
#include "process.h"
#include "file.h"
#include "kio.h"
#include "../lib/stdio.h"

#define PG_SIZE 4096

struct task_struct *main_thread; // 主线程PCB
struct list thread_ready_list;   // 就绪队列
struct list thread_all_list;     // 所有任务队列
struct list_elem *thread_tag;    // 保存队列中的线程节点

struct task_struct *idle_thread; // idle线程

// pid的位图
uint8_t pid_bitmap_bits[128];

// pid池
struct pid_pool
{
    struct bitmap pid_bitmap; // pid位图
    uint32_t pid_start;       // 起始pid
    struct lock pid_lock;     // pid锁
} pid_pool;

static void pid_pool_init(void)
{
    pid_pool.pid_start = 1;
    pid_pool.pid_bitmap.bits = pid_bitmap_bits;
    pid_pool.pid_bitmap.btmp_bytes_len = 128;
    bitmap_init(&pid_pool.pid_bitmap);
    lock_init(&pid_pool.pid_lock);
}

static pid_t allocate_pid(void)
{
    lock_acquire(&pid_pool.pid_lock);
    int32_t idx = bitmap_scan(&pid_pool.pid_bitmap, 1);
    bitmap_set(&pid_pool.pid_bitmap, idx, 1);
    lock_release(&pid_pool.pid_lock);
    return pid_pool.pid_start + idx;
}

void release_pid(pid_t pid)
{
    lock_acquire(&pid_pool.pid_lock);

    int32_t idx = pid - pid_pool.pid_start;
    bitmap_set(&pid_pool.pid_bitmap, idx, 0);

    lock_release(&pid_pool.pid_lock);
}

// 系统空闲时运行的线程
static void idle(void *arg)
{
    while (1)
    {
        thread_block(TASK_BLOCKED);
        // 执行hlt必须保证在开中断的情况下
        asm volatile("sti;hlt" ::
                         : "memory");
    }
}

extern void switch_to(struct task_struct *cur, struct task_struct *next);

struct task_struct *running_thread()
{
    uint32_t esp;
    asm("mov %%esp,%0"
        : "=g"(esp));
    // 取起始地址
    return (struct task_struct *)(esp & 0xfffff000);
}

// 由kernel_thread执行
static void kernel_thread(thread_func *func, void *arg)
{
    /*
        执行前开中断，避免时钟中断被屏蔽而无法调度
    */
    intr_enable();
    func(arg);
}
/*
    初始化线程基本信息
*/
void init_thread(struct task_struct *pthread, char *name, int prio)
{
    memset(pthread, 0, sizeof(*pthread));
    strcpy(pthread->name, name);

    if (pthread == main_thread)
    {
        pthread->status = TASK_RUNNING;
    }
    else
    {
        pthread->status = TASK_READY;
    }

    pthread->priority = prio;
    /*
        内核线程栈的基本栈顶地址是自己TCB所在的页的位置
    */
    pthread->self_kstack = (uint32_t *)((uint32_t)pthread + PG_SIZE);
    pthread->stack_magic = T_MAGIC;
    pthread->ticks = prio;
    pthread->elapsed_ticks = 0;
    pthread->pgdir = NULL;
    pthread->cwd_ino = 0; // 根目录作为默认工作路径
    pthread->pid = allocate_pid();
    pthread->ppid = -1;
    pthread->fd_table[0] = 0;
    pthread->fd_table[1] = 1;
    pthread->fd_table[2] = 2;
    uint8_t i = 3;
    while (i < MAX_FILES_OPEN_PER_PROC)
    {
        pthread->fd_table[i] = -1;
        i++;
    }
}

/*
    初始化线程的上下文环境
*/
void thread_create(struct task_struct *pthread, thread_func func, void *arg)
{
    // 先预留中断保存上下文使用的栈空间
    pthread->self_kstack -= sizeof(struct intr_stack);
    // 再留出线程栈空间
    pthread->self_kstack -= sizeof(struct thread_stack);
    struct thread_stack *kt_stack = (struct thread_stack *)pthread->self_kstack;
    kt_stack->eip = kernel_thread;
    kt_stack->function = func;
    kt_stack->arg = arg;
    kt_stack->ebp = 0;
    kt_stack->ebx = 0;
    kt_stack->edi = 0;
    kt_stack->esi = 0;
}

/*
    创建线程
*/
struct task_struct *thread_start(char *name, int prio, thread_func function, void *func_arg)
{
    // 所有的PCB均位于内核空间
    struct task_struct *thread = get_kernel_pages(1);
    init_thread(thread, name, prio);
    thread_create(thread, function, func_arg);

    /*确保之前不在队列之中*/
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    /*加入就绪队列*/
    list_append(&thread_ready_list, &thread->general_tag);

    /*确保之前不在队列中*/
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));

    list_append(&thread_all_list, &thread->all_list_tag);

    //  此处通过ret执行了kernel_thread
    return thread;
}

// 主动让出cpu 令其他线程执行
void thread_yield(void)
{
    struct task_struct *cur = running_thread();
    enum intr_status old = intr_disable();

    ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
    list_append(&thread_ready_list, &cur->general_tag);
    cur->status = TASK_READY;
    scheduler();

    intr_set_status(old);
}

void scheduler()
{
    ASSERT(intr_get_status() == INTR_OFF);
    if (list_empty(&thread_ready_list))
    {
        thread_unblock(idle_thread);
    }

    struct task_struct *cur = running_thread();
    if (cur->status == TASK_RUNNING)
    {
        ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
        cur->ticks = cur->priority;
        // 只是时间片到了
        list_append(&thread_ready_list, &cur->general_tag);
        cur->status = TASK_READY;
    }
    else
    {
        /*
            若此时没有运行，处于等待状态，肯定不在就绪队列中
        */
    }

    ASSERT(!list_empty(&thread_ready_list));
    thread_tag = list_pop(&thread_ready_list);
    struct task_struct *next = elem2entry(struct task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING;
    // 先切换页表 不会影响0级栈 因为此段代码是内核代码 所有进程的高1GB地址空间都被映射到了内核
    process_activate(next);
    switch_to(cur, next);
}

static void make_main_thread(void)
{
    /*
        在loader.S中进入内核时，已将esp置为0xc009f000
        因此内核主线程PCB地址就为0xc009e000
    */
    main_thread = running_thread();
    init_thread(main_thread, "main", 31);
    // main就是正在执行这个函数的线程，肯定不在thread_ready_list之中
    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
    list_append(&thread_all_list, &main_thread->all_list_tag);
}

void thread_init(void)
{
    put_str("thread init start\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);

    pid_pool_init();
    make_main_thread();
    idle_thread = thread_start("idle", 10, idle, NULL);
    put_str("thread init done\n");
}

void thread_block(enum task_status stat)
{
    /*
        stat: BLOCKED WAITING HANGING不会被调度
    */
    ASSERT(stat == TASK_BLOCKED || stat == TASK_HANGING || stat == TASK_WAITING);
    enum intr_status old = intr_disable();
    struct task_struct *pthread = running_thread();
    pthread->status = stat;
    scheduler();
    intr_set_status(old);
}

void thread_unblock(struct task_struct *pthread)
{
    ASSERT(pthread->status == TASK_BLOCKED || pthread->status == TASK_WAITING || pthread->status == TASK_HANGING);
    enum intr_status old = intr_disable();
    if (pthread->status != TASK_READY)
    {
        ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));
        if (elem_find(&thread_ready_list, &pthread->general_tag))
        {
            PANIC("thread_unblock: blocked thread in ready list\n");
        }
        /*
            尽快调度
        */
        list_push(&thread_ready_list, &pthread->general_tag);
        pthread->status = TASK_READY;
    }
    intr_set_status(old);
}

pid_t fork_pid(void)
{
    return allocate_pid();
}

// 以填充空格的方式输出buf
static void pad_print(char *buf, int32_t buf_len, void *ptr, char format)
{
    memset(buf, 0, buf_len);
    uint8_t out_pad_0idx = 0;
    switch (format)
    {
    case 's':
        out_pad_0idx = sprintf(buf, "%s", ptr);
        break;
    case 'd':
        out_pad_0idx = sprintf(buf, "%d", *((int16_t *)ptr));
        break;
    case 'x':
        out_pad_0idx = sprintf(buf, "%x", *((uint32_t *)ptr));
        break;
    default:
        break;
    }
    while (out_pad_0idx < buf_len)
    {
        buf[out_pad_0idx] = ' ';
        out_pad_0idx++;
    }
    sys_write(stdout, buf, buf_len - 1);
}

void thread_exit(struct task_struct *pthread, bool schedule)
{
    // 保证scheduler在关中断的情况使用
    intr_disable();
    pthread->status = TASK_DIED;
    // 如果pthread不是当前线程，可能还在就绪队列中，将其删除
    if (elem_find(&thread_ready_list, &pthread->general_tag))
    {
        list_remove(&pthread->general_tag);
    }

    if (pthread->pgdir)
    {
        mfree_page(PF_KERNEL, pthread->pgdir, 1);
    }

    // 从all_list中去除此任务
    list_remove(&pthread->all_list_tag);
    // 回收PCB所在的页
    if (pthread != main_thread)
    {
        mfree_page(PF_KERNEL, pthread, 1);
    }

    // 归还pid
    release_pid(pthread->pid);
    if (schedule)
    {
        scheduler();
        PANIC("thread_exit: eip shouldn't point here!\n");
    }
}

// 比对任务的pid
static bool pcheck(struct list_elem *e, pid_t pid)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, e);
    if (pthread->pid == pid)
    {
        return true;
    }
    return false;
}

// 根据pid找PCB
struct task_struct *pid2thread(pid_t pid)
{
    struct list_elem *e = list_traversal(&thread_all_list, pcheck, pid);
    if (e == NULL)
    {
        return NULL;
    }
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, e);
    return pthread;
}

// 用于在list_traversal函数中的回调函数，用于针对线程队列的处理
static bool elem2thread_info(struct list_elem *elem, int arg)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, elem);
    char out_pad[16];
    pad_print(out_pad, 16, &pthread->pid, 'd');
    pad_print(out_pad, 16, &pthread->ppid, 'd');
    switch (pthread->status)
    {
    case 0:
        pad_print(out_pad, 16, "RUNNING", 's');
        break;
    case 1:
        pad_print(out_pad, 16, "READY", 's');
        break;
    case 2:
        pad_print(out_pad, 16, "BLOCKED", 's');
        break;
    case 3:
        pad_print(out_pad, 16, "WAITING", 's');
        break;
    case 4:
        pad_print(out_pad, 16, "HANGING", 's');
        break;
    case 5:
        pad_print(out_pad, 16, "DIED", 's');
        break;
    default:
        break;
    }
    pad_print(out_pad, 16, &pthread->ticks, 'x');
    ASSERT(strlen(pthread->name) < 32);
    memset(out_pad, 0, 16);
    memcpy(out_pad, pthread->name, strlen(pthread->name));
    strcat(out_pad, "\n");
    sys_write(stdout, out_pad, strlen(out_pad));
    return false; // 迎合主调函数list_traversal
}

void sys_ps(void)
{
    char *ps_title = "PID             PPID            STAT            TICKS           COMMAND\n";
    sys_write(stdout, ps_title, strlen(ps_title));
    list_traversal(&thread_all_list, elem2thread_info, 0);
}