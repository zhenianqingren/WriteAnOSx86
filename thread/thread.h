#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "../lib/stdint.h"
#include "list.h"
#include "print.h"
#include "memory.h"
#include "../kernel/memory.h"
#include "bitmap.h"

#define T_MAGIC 0x19870916
// 自定义通用函数类型
typedef void thread_func(void *);

// 状态
enum task_status
{
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_WAITING,
    TASK_HANGING,
    TASK_DIED
};

/*
    中断栈intr_stack
    中断发生时保护上下文环境
    此栈在线程自己的内核栈中位置固定，所在页的顶端
*/
struct intr_stack
{
    uint32_t vec_no; // kernel.S宏VECTOR中push %1压入的中断号
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy; // 虽然esp指针也会压入，但是esp会不断变化
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

    // 以下由CPU从低特权级进入高特权级时压入
    uint32_t err_code;
    void (*eip)(void);
    uint32_t cs;
    uint32_t eflags;
    void *esp;
    uint32_t ss;
};

/**
 * 第二次上下保存，保存中断环境的上下文
 *
 */
struct thread_stack
{
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;
    // 这四个寄存器在被调函数结束后不应被改变，因此应提前保存，esp会由调用约定保存：入参顺序...
    /*
        线程首次执行，eip会指向待调用的函数kernel_thread
        其他时候指向的是返回地址
    */
    void (*eip)(thread_func *, void *); // 线程要执行的函数以及其参数/中断继续执行的地址
    void(*unused_retaddr);              // 未用到
    thread_func *function;              // kernel_thread调用函数名      参数1
    void *arg;                          // kernel_thread调用函数的参数  参数2
};

// PCB/TCB
struct task_struct
{
    uint32_t *self_kstack; // 每个内核级线程都有自己的内核栈
    pid_t pid;
    enum task_status status;
    char name[32];
    uint8_t priority;
    uint8_t ticks;          // 每次在处理器上执行的时间滴答数
    uint32_t elapsed_ticks; // 执行的总时间

    int32_t fd_table[MAX_FILES_OPEN_PER_PROC];
    struct list_elem general_tag;  // 用于线程在一般的队列中的结点
    struct list_elem all_list_tag; // 用于线程在thread_all_list中的结点 通过此地址可直接计算出task_struct的地址

    uint32_t *pgdir;                    // 进程页表的虚拟地址
    struct virtual_addr userprog_vaddr; // 用户进程的虚拟地址
    struct mem_block_desc u_block_desc[DESC_CNT];
    uint32_t cwd_ino;     // 进程所在的工作目录的ino编号
    pid_t ppid;           // 父进程pid
    int8_t exit_status;   // 退出时的状态
    uint32_t stack_magic; // 栈的边界标记，检测栈溢出
};

void init_thread(struct task_struct *pthread, char *name, int prio);
void thread_create(struct task_struct *pthread, thread_func func, void *arg);
struct task_struct *thread_start(char *name, int prio, thread_func function, void *func_arg);
void thread_init(void);
void thread_block(enum task_status stat);
void thread_unblock(struct task_struct *pthread);
struct task_struct *running_thread();
void thread_yield(void);
pid_t fork_pid(void);
void sys_ps(void);
void thread_exit(struct task_struct *pthread, bool schedule);
struct task_struct *pid2thread(pid_t pid);

extern struct list thread_ready_list; // 就绪队列
extern struct list thread_all_list;   // 所有任务队列
extern struct list_elem *thread_tag;  // 保存队列中的线程节点
#endif