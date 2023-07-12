#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "debug.h"
#include "string.h"
#include "global.h"
#include "../thread/sync.h"

#define PG_SIZE 4096

/*
0xc009f000是内核主线程栈顶 0xc009e000是内核主线程的pcb
一个页框大小的位图可表示128MB内存 位图地址安排在0xc009a000
0xc009a000 0xc009b000 0xc009c000 0xc009d000
最大支持4个页框的位图，表示512MB的内存
*/

#define MEM_BITMAP_BASE 0xc009a000
// 从内核地址空间0xc0000000跨过1MB作为内核申请堆空间的起始地址
#define K_HEAP_START 0xc0100000
#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)

/*内存池结构，管理内核内存池和用户内存池*/
struct pool
{
    struct bitmap pool_bitmap; // 位图结构
    uint32_t phy_addr_start;   // 管理的内存起始物理地址
    uint32_t pool_size;        // 内存池管理的字节大小
    struct lock lock;          // 对内存池进行互斥访问
};

struct arena
{
    struct mem_block_desc *desc; // 与此arena关联的mem_block_desc
    uint32_t cnt;                // large为true cnt代表页框数 large为false cnt代表空闲块数
    bool large;
};

struct mem_block_desc k_block_descs[DESC_CNT];

// 内核内存池和用户内存池
struct pool kernel_pool, user_pool; // 内核内存池和用户内存池
struct virtual_addr kernel_vaddr;   // 为内核分配虚拟地址

void block_desc_init(struct mem_block_desc *desc_array)
{
    uint16_t desc_idx, block_siz = 16;

    for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++)
    {
        desc_array[desc_idx].block_siz = block_siz;
        desc_array[desc_idx].blocks_per_arena = (PG_SIZE - sizeof(struct arena)) / block_siz;
        list_init(&desc_array[desc_idx].free_list);
        block_siz *= 2;
    }
}

static struct mem_block *arena2block(struct arena *a, uint32_t idx)
{
    return (struct mem_block *)((uint32_t)a + idx * a->desc->block_siz + sizeof(struct arena));
}

static struct arena *block2arena(struct mem_block *b)
{
    return (struct arena *)((uint32_t)b & 0xfffff000);
}

void *sys_malloc(uint32_t size)
{
    enum pool_flags PF;
    struct pool *mem_pool;
    uint32_t pool_siz;
    struct mem_block_desc *descs;
    struct task_struct *cur = running_thread();

    if (cur->pgdir == NULL)
    {
        /***
         * 如果是内核线程
         */
        PF = PF_KERNEL;
        pool_siz = kernel_pool.pool_size;
        mem_pool = &kernel_pool;
        descs = k_block_descs;
    }
    else
    {
        /**
         * 如果是用户线程
         */
        PF = PF_USER;
        pool_siz = user_pool.pool_size;
        mem_pool = &user_pool;
        descs = cur->u_block_desc;
    }

    // 申请的内存超过范围，返回NULL
    if (!(size > 0 && size < pool_siz))
    {
        return NULL;
    }

    struct arena *a;
    struct mem_block *b;
    lock_acquire(&mem_pool->lock);
    if (size > 1024)
    {
        /**
         * 如果size大于1024就分配页框
         */
        uint32_t pg_cnt = DIV_ROUND_UP(size + sizeof(struct arena), PG_SIZE);
        a = malloc_page(PF, pg_cnt);
        if (a != NULL)
        {
            memset(a, 0, pg_cnt * PG_SIZE);
            // 对于分配的页框 将desc置为NULL large置为true cnt置为页框数
            a->desc = NULL;
            a->cnt = pg_cnt;
            a->large = true;
            lock_release(&mem_pool->lock);
            return (void *)(a + 1); // 将剩下的内存返回
        }
        else
        {
            lock_release(&mem_pool->lock);
            return NULL; // 将剩下的内存返回
        }
    }
    else
    {
        // 若申请的内存小于等于1024 在各种规格的mem_block_desc中去适配
        uint8_t desc_idx;
        for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++)
        {
            if (size <= descs[desc_idx].block_siz)
            {
                break;
            }
        }
        /**
         * 若free_list中已经没有可用的block，就创建新的
         */
        if (list_empty(&descs[desc_idx].free_list))
        {
            a = malloc_page(PF, 1);
            if (a == NULL)
            {
                lock_release(&mem_pool->lock);
                return NULL;
            }
            memset(a, 0, PG_SIZE);
            a->desc = &descs[desc_idx];
            a->large = false;
            a->cnt = descs[desc_idx].blocks_per_arena;
            uint32_t block_idx;
            enum intr_status old = intr_disable();
            /**
             * 拆分新的arena内存块并将其添加到free_list
             */
            for (block_idx = 0; block_idx < descs[desc_idx].blocks_per_arena; block_idx++)
            {
                b = arena2block(a, block_idx);
                ASSERT(!elem_find(&a->desc->free_list, &b->free_elem));
                list_append(&a->desc->free_list, &b->free_elem);
            }
            intr_set_status(old);
        }

        /**
         * 开始分配
         */
        b = elem2entry(struct mem_block, free_elem, list_pop(&(descs[desc_idx].free_list)));
        memset(b, 0, descs[desc_idx].block_siz);
        a = block2arena(b);
        a->cnt--;
        lock_release(&mem_pool->lock);
        return ((void *)b);
    }
    return NULL;
}

/*
    在pf表示的虚拟内存池中申请pg_cnt个虚拟页
    成功返回起始虚拟地址，失败返回NULL
*/
static void *vaddr_get(enum pool_flags pf, uint32_t pg_cnt)
{
    uint32_t vaddr_start = 0;
    uint32_t bit_start_idx = -1;
    uint32_t cnt = 0;
    if (pf == PF_KERNEL)
    {
        bit_start_idx = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_start_idx == -1)
        {
            return NULL;
        }
        while (cnt < pg_cnt)
        {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_start_idx + cnt++, 1);
        }
        vaddr_start = kernel_vaddr.vaddr_start + bit_start_idx * PG_SIZE;
    }
    else
    {
        /*用户内存池*/
        struct task_struct *cur = running_thread();
        bit_start_idx = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_start_idx == -1)
        {
            return NULL;
        }
        while (cnt < pg_cnt)
        {
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_start_idx + cnt++, 1);
        }
        vaddr_start = cur->userprog_vaddr.vaddr_start + bit_start_idx * PG_SIZE;
        /**
         * (0xc0000000 - PG_SIZE作为用户的三级栈已经被分配)
         */
        ASSERT((uint32_t)vaddr_start < (0xc0000000 - PG_SIZE));
    }

    return (void *)vaddr_start;
}

/*得到虚拟地址vaddr对应的pte指针*/
uint32_t *pte_ptr(uint32_t vaddr)
{
    uint32_t *pte = (uint32_t *)(0xffc00000 +
                                 ((vaddr & 0xffc00000) >> 10) +
                                 PTE_IDX(vaddr) * 4);
    return pte;
}

/*得到虚拟地址vaddr对应的pde指针*/
uint32_t *pde_ptr(uint32_t vaddr)
{
    uint32_t *pde = (uint32_t *)(0xfffff000 +
                                 PDE_IDX(vaddr) * 4);
    return pde;
}

/*
    在m_pool指向的物理内存池中分配一个物理页，成功返回页框物理地址，失败返回NULL
*/
static void *palloc(struct pool *m_pool)
{
    uint32_t bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);
    if (bit_idx == -1)
    {
        return NULL;
    }
    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);
    uint32_t addr = m_pool->phy_addr_start + bit_idx * PG_SIZE;
    return (void *)addr;
}

/*
    页表中添加虚拟地址_vaddr与物理地址_page_phyaddr的映射
*/

static void page_table_add(void *_vaddr, void *_page_phyaddr)
{
    uint32_t vaddr = (uint32_t)_vaddr;
    uint32_t page_phyaddr = (uint32_t)_page_phyaddr;
    uint32_t *pde = pde_ptr(vaddr);
    uint32_t *pte = pte_ptr(vaddr);

    /*
        确保pde创建完毕后再执行*pte
    */
    // 首先判断页目录项中的P位为1，即页目录项指向的页表存在
    if (*pde & 0x1)
    {
        // 判断页表项指向的页表存不存在，从逻辑上讲此时要添加页表项的映射关系，不应该存在
        ASSERT(!(*pte & 0x1));
        if (!(*pte & 0x1))
        {
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        }
        else
        {
            // ASSERT会触发，不会执行到此
            PANIC("pte repeat!!!\n");
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        }
    }
    else
    {
        // 先创建页目录项再创建页表项
        uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool);
        *pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        /*
            先将分配的物理页的内容清0
        */
        memset((void *)((uint32_t)pte & 0xfffff000), 0, PG_SIZE);
        ASSERT(!(*pte & 0x1));
        *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
    }
}

/*分配pg_cnt个页，成功返回虚拟地址，失败返回NULL*/
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt)
{
    ASSERT(pg_cnt > 0 && pg_cnt < 3840);
    /*
        1. 通过vaddr_get在虚拟内存池中申请虚拟地址
        2. 通过palloc在物理内存池中申请物理页
        3. 通过page_table_add完成虚拟地址到物理地址的映射
    */
    void *vaddr_start = vaddr_get(pf, pg_cnt);
    if (vaddr_start == NULL)
    {
        return NULL;
    }

    uint32_t vaddr = (uint32_t)vaddr_start;
    uint32_t cnt = pg_cnt;
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    void *paddr = NULL;
    while (cnt-- > 0)
    {
        /*虚拟页地址连续，但是物理页地址可以不连续*/
        if ((paddr = palloc(mem_pool)) == NULL)
        {
            /*分配失败回滚，将来实现*/
            return NULL;
        }
        page_table_add((void *)vaddr, paddr);
        vaddr += PG_SIZE;
    }

    return vaddr_start;
}

/*
    在内核物理内存池中申请内存，成功返回虚拟地址，失败返回NULL
    中断开启前创建内核线程
*/
void *get_kernel_pages(uint32_t pg_cnt)
{
    void *vaddr = malloc_page(PF_KERNEL, pg_cnt);
    if (vaddr != NULL)
    {
        // 若不为空，将页框清0后返回
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    return vaddr;
}

/*
 *在用户空间申请内存，成功返回虚拟地址，失败返回NULL
 */
void *get_user_pages(uint32_t pg_cnt)
{
    lock_acquire(&user_pool.lock);
    void *vaddr = malloc_page(PF_USER, pg_cnt);
    if (vaddr != NULL)
    {
        // 若不为空，将页框清0后返回
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    lock_release(&user_pool.lock);
    return vaddr;
}

/**
 * 将虚拟地址映射到物理内存
 */
void *get_one_page(enum pool_flags pf, uint32_t vaddr)
{
    struct pool *mem_pool = (pf & PF_KERNEL) ? &kernel_pool : &user_pool;
    lock_acquire(&mem_pool->lock);
    struct task_struct *cur = running_thread();
    int32_t bit_idx = -1;

    if (cur->pgdir != NULL && pf == PF_USER)
    {
        /*
         *如果是用户进程申请用户空间内存
         */
        bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1);
        // 先将虚拟地址空间对应的位图置1
    }
    else if (cur->pgdir == NULL && pf == PF_KERNEL)
    {
        /**
         * 如果是内核线程申请内核空间内存
         */
        bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1);
    }
    else
    {
        PANIC("!!!not allowed behavior: process should apply corresponding page\n");
    }
    void *page_phyaddr = palloc(mem_pool);
    if (page_phyaddr == NULL)
    {
        return NULL;
    }
    page_table_add((void *)vaddr, page_phyaddr);
    lock_release(&mem_pool->lock);
    return (void *)vaddr;
}

/**
 * 得到虚拟地址映射的物理地址
 */

uint32_t addr_v2p(uint32_t vaddr)
{
    uint32_t *pte = pte_ptr(vaddr);
    // 去掉低12bit的页表属性 换成vaddr的低12bit
    return ((*pte & 0xfffff000) + (vaddr & 0xfff));
}

/*管理堆区*/
static void mem_pool_init(uint32_t all_mem)
{
    put_str("   mem pool init start\n");
    uint32_t pgt_siz = PG_SIZE * 256; // 页目录表+第0个和第768个页目录指向同一个页表+769~1022个页目录项指向254个页表=256
    uint32_t used_mem = pgt_siz + 0x100000;
    uint32_t free_mem = all_mem - used_mem;
    uint16_t all_free_pg = free_mem / PG_SIZE;

    uint16_t kernel_free_pg = all_free_pg / 2;
    uint16_t user_free_pg = all_free_pg - kernel_free_pg;

    /*Kernel Bit Map长度*/
    uint32_t kbm_len = kernel_free_pg / 8;
    /*User Bit Map长度*/
    uint32_t ubm_len = user_free_pg / 8;

    /*Kernel内存池起始地址*/
    uint32_t kp_start = used_mem;
    /*User内存池起始地址*/
    uint32_t up_start = kp_start + kernel_free_pg * PG_SIZE;

    kernel_pool.phy_addr_start = kp_start;
    user_pool.phy_addr_start = up_start;

    kernel_pool.pool_size = kernel_free_pg * PG_SIZE;
    user_pool.pool_size = user_free_pg * PG_SIZE;

    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_len;
    user_pool.pool_bitmap.btmp_bytes_len = ubm_len;

    /*
    位图是全局数据，长度不固定
    全局或静态数组需要编译器知道长度
    但我们需要根据总内存得知位图长度
    因此指定一块内存来生成位图
    */

    /*
    内核使用的最高地址是0xc009f000，这是主线程的栈地址
    内核内存池位图起始地址定在了MEM_BITMAP_BASE
    */
    kernel_pool.pool_bitmap.bits = (void *)MEM_BITMAP_BASE;
    /**
     * 用户内存池位图跟在内核内存池后
     *
     */
    user_pool.pool_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_len);

    lock_init(&kernel_pool.lock);
    lock_init(&user_pool.lock);

    put_str("   kernel pool bitmap start: 0x");
    put_int(MEM_BITMAP_BASE);
    put_char('\n');

    put_str("   kernel pool physical address: 0x");
    put_int(kernel_pool.phy_addr_start);
    put_char('\n');

    put_str("   user pool bitmap start: 0x");
    put_int((uint32_t)user_pool.pool_bitmap.bits);
    put_char('\n');

    put_str("   user pool physical address: 0x");
    put_int(user_pool.phy_addr_start);
    put_char('\n');

    /*
        位图置0
    */
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);

    /*
        初始化内核虚拟地址位图，按实际物理内存大小生成数组
    */

    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_len;

    /*
        位图的数组指向一块未使用过的内存，目前定位在用户内存池和内核内存池之外
    */

    kernel_vaddr.vaddr_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_len + ubm_len);
    kernel_vaddr.vaddr_start = K_HEAP_START;
    bitmap_init(&kernel_vaddr.vaddr_bitmap);
    put_str("   mem pool init done\n");
}

void mem_init()
{
    put_str("   mem init start\n");
    uint32_t mem_bytes_total = (*(uint32_t *)(0xb00));
    mem_pool_init(mem_bytes_total);
    block_desc_init(k_block_descs);
    put_str("   mem init done\n");
}

// 将物理地址回收到内存池 整页
void pfree(uint32_t pg_phy_addr)
{
    struct pool *mem_pool;
    uint32_t bit_idx;
    if (pg_phy_addr >= user_pool.phy_addr_start)
    {
        mem_pool = &user_pool;
        bit_idx = (pg_phy_addr - user_pool.phy_addr_start) / PG_SIZE;
    }
    else
    {
        mem_pool = &kernel_pool;
        bit_idx = (pg_phy_addr - kernel_pool.phy_addr_start) / PG_SIZE;
    }
    bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0);
}

// 去掉页表中的虚拟地址映射
static void pg_pte_remove(uint32_t vaddr)
{
    uint32_t *pte = (uint32_t *)pte_ptr(vaddr);
    *pte &= ~PG_P_1;
    // 更新TLB
    asm volatile("invlpg %0" ::"m"(vaddr)
                 : "memory");
}

// 在虚拟地址池中释放掉以_vaddr起始的连续pg_cnt个虚拟页
static void vaddr_remove(enum pool_flags pf, void *_vaddr, uint32_t pg_cnt)
{
    uint32_t bit_start_idx = 0;
    uint32_t vaddr = (uint32_t)_vaddr;
    uint32_t cnt = 0;
    struct bitmap *map;
    if (pf == PF_KERNEL)
    {
        // 内核虚拟地址池
        bit_start_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        map = &kernel_vaddr.vaddr_bitmap;
    }
    else
    {
        // 用户虚拟地址池
        struct task_struct *cur = running_thread();
        bit_start_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
        map = &cur->userprog_vaddr.vaddr_bitmap;
    }
    while (cnt < pg_cnt)
    {
        bitmap_set(map, bit_start_idx + cnt++, 0);
    }
}

/*释放以虚拟地址vaddr为起始的cnt个物理页框*/
void mfree_page(enum pool_flags pf, void *_vaddr, uint32_t pg_cnt)
{
    uint32_t pg_phy_addr;
    uint32_t vaddr = (uint32_t)_vaddr;
    uint32_t page_cnt = 0;
    ASSERT(pg_cnt >= 1 && vaddr % PG_SIZE == 0);

    pg_phy_addr = addr_v2p(vaddr);
    /**
     * 确保待释放的物理内存位于低端1MB+1KB大小的页目录+1KB大小的页表地址范围之外
     */
    ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= 0x102000);

    while (page_cnt < pg_cnt)
    {
        if (pf == PF_USER)
        {
            ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= user_pool.phy_addr_start);
        }
        else
        {
            ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= kernel_pool.phy_addr_start && pg_phy_addr < user_pool.phy_addr_start);
        }
        // 释放物理页
        pfree(pg_phy_addr);

        // 去掉虚拟地址与物理地址的映射
        pg_pte_remove(vaddr);

        vaddr += PG_SIZE;
        page_cnt++;
    }
    // 释放虚拟地址空间相应的位图
    vaddr_remove(pf, _vaddr, pg_cnt);
}

void sys_free(void *ptr)
{
    ASSERT(ptr != NULL);
    if (ptr == NULL)
        return;
    enum pool_flags PF;
    struct pool *mem_pool;

    if (running_thread()->pgdir == NULL)
    {
        PF = PF_KERNEL;
        mem_pool = &kernel_pool;
    }
    else
    {
        PF = PF_USER;
        mem_pool = &user_pool;
    }

    lock_acquire(&mem_pool->lock);
    struct mem_block *b = (struct mem_block *)ptr;
    struct arena *a = block2arena(b);
    ASSERT(a->large == 0 || a->large == 1);
    if (a->desc == NULL && a->large == true)
    {
        mfree_page(PF, a, a->cnt);
    }
    else
    {
        list_append(&a->desc->free_list, &b->free_elem);
        a->cnt++;
        if (a->cnt == a->desc->blocks_per_arena)
        {
            uint32_t block_idx;
            for (block_idx = 0; block_idx < a->desc->blocks_per_arena; block_idx++)
            {
                b = arena2block(a, block_idx);
                ASSERT(elem_find(&a->desc->free_list, &b->free_elem));
                list_remove(&b->free_elem);
            }
            mfree_page(PF, (void *)a, 1);
        }
    }
    lock_release(&mem_pool->lock);
}

// 安装一页大小的vaddr 但是并不操作虚拟位图
void *get_a_page_without_opvaddrbtmp(enum pool_flags pf, uint32_t vaddr)
{
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    lock_acquire(&mem_pool->lock);
    void *page_phyaddr = palloc(mem_pool);
    if (page_phyaddr == NULL)
    {
        lock_release(&mem_pool->lock);
        return NULL;
    }
    page_table_add((void *)vaddr, page_phyaddr);
    lock_release(&mem_pool->lock);
    return (void *)vaddr;
}

