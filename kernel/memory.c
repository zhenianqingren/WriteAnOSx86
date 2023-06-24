#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "debug.h"
#include "string.h"
#include "global.h"

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
};

// 内核内存池和用户内存池
struct pool kernel_pool, user_pool; // 内核内存池和用户内存池
struct virtual_addr kernel_vaddr;   // 为内核分配虚拟地址

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
    put_str("   mem init done\n");
}