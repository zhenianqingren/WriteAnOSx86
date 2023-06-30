#include "thread.h"
#include "stdint.h"
#include "memory.h"
#include "global.h"
#include "string.h"
#include "debug.h"
#include "interrupt.h"
#include "process.h"

#define PG_SIZE 4096

struct task_struct *main_thread; // 主线程PCB
struct list thread_ready_list;   // 就绪队列
struct list thread_all_list;     // 所有任务队列
struct list_elem *thread_tag;    // 保存队列中的线程节点
struct lock pid_lock;

struct task_struct *idle_thread; // idle线程

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

static pid_t alloc_pid(void)
{
    static pid_t next_pid = 0;
    lock_acquire(&pid_lock);
    next_pid++;
    lock_release(&pid_lock);
    return next_pid;
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
    pthread->pid = alloc_pid();
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
    lock_init(&pid_lock);
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