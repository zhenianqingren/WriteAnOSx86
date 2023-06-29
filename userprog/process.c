#include "process.h"
#include "../kernel/memory.h"
/**
 * 构建用户进程初始上下文信息
 */
void start_process(void *filename_)
{
    void *func = filename_;
    struct task_struct *cur = running_thread();
    cur->self_kstack += sizeof(struct thread_stack);
    struct intr_stack *proc_stack = (struct intr_stack *)cur->self_kstack;
    proc_stack->eax = 0;
    proc_stack->ebp = 0;
    proc_stack->ebx = 0;
    proc_stack->ecx = 0;
    proc_stack->edi = 0;
    proc_stack->edx = 0;
    proc_stack->esi = 0;
    proc_stack->esp_dummy = 0;

    proc_stack->gs = 0;
    proc_stack->ds = SELECTOR_U_DATA;
    proc_stack->es = SELECTOR_U_DATA;
    proc_stack->fs = SELECTOR_U_DATA;

    proc_stack->eip = func;
    proc_stack->cs = SELECTOR_U_CODE;
    proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
    proc_stack->esp = (void *)((uint32_t)get_one_page(PF_USER, USER_STACK3_VADDR) + PG_SIZE);
    proc_stack->ss = SELECTOR_U_STACK;

    asm volatile("movl %0,%%esp;jmp intr_exit" ::"g"(proc_stack)
                 : "memory");
}

/**
 * 激活页目录表
 */
void page_dir_activate(struct task_struct *p_thread)
{
    /**
     * 执行此函数时调度的是线程，但为了防止不属于同一进程的线程地址空间冲突
     * 每次都要重新加载页表
     */
    // 如果是内核线程，页表地址是0x100000
    uint32_t page_dir_phy_addr = 0x100000;
    if (p_thread->pgdir != NULL)
    {
        // 如果不是内核线程
        page_dir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir);
    }
    // 更新页目录寄存器cr3
    asm volatile("movl %0,%%cr3" ::"r"(page_dir_phy_addr)
                 : "memory");
}

// 激活页表 更新tss中的esp0为进程的特权级0的栈
void process_activate(struct task_struct *p_thread)
{
    ASSERT(p_thread != NULL);
    page_dir_activate(p_thread);
    /**
     * 内核级线程特权级就是0 不需要更新esp0
     */
    if (p_thread->pgdir)
    {
        updata_tss_esp(p_thread);
    }
}

/**
 * 创建用户进程页目录表，其中每个用户进程的3~4GB地址空间都被映射到了内核
 * 因此相应的页目录表也应该复制
 */
uint32_t *create_page_dir(void)
{
    uint32_t *page_dir_vaddr = get_kernel_pages(1);
    if (page_dir_vaddr == NULL)
    {
        console_put_str("create_page_dir: get kernel page failed\n");
        return NULL;
    }
    /**
     * 复制页目录表 768~1024
     */
    memcpy((void *)((uint32_t)page_dir_vaddr + 0x300 * 4), (const void *)(0xfffff000 + 0x300 * 4), 1024);

    uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);
    page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;
    return page_dir_vaddr;
}

// 创建用户进程虚拟地址位图
void create_user_vaddr_bitmap(struct task_struct *user_prog)
{
    user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
    user_prog->userprog_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt);
    user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
    bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}
// 创建用户进程
void process_execute(void *filename, char *name)
{
    /**
     * 由内核维护PCB信息
     */

    // 1. 创建PCB
    struct task_struct *thread = get_kernel_pages(1);

    // 2. 初始化PCB 设置各属性值
    init_thread(thread, name, default_prio);

    // 3. 创建用户虚拟地址空间位图
    create_user_vaddr_bitmap(thread);

    // 4. 初始化上下文环境 首次被switch to后，进入start process调整上下文环境然后运行filename程序文件
    thread_create(thread, start_process, filename);

    // 5. 为用户进程创建页表，实现地址空间隔离
    thread->pgdir = create_page_dir();

    // 6. 堆区描述符数组
    block_desc_init(thread->u_block_desc);

    enum intr_status old = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);
    intr_set_status(old);
}