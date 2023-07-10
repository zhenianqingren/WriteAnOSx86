#include "ide.h"
#include "kio.h"
#include "debug.h"
#include "timer.h"
#include "io.h"

// 定义硬盘各寄存器端口号
#define reg_data(channel) (channel->port_base + 0)
#define reg_error(channel) (channel->port_base + 1)
#define reg_sect_cnt(channel) (channel->port_base + 2)
#define reg_lba_l(channel) (channel->port_base + 3)
#define reg_lba_m(channel) (channel->port_base + 4)
#define reg_lba_h(channel) (channel->port_base + 5)
#define reg_dev(channel) (channel->port_base + 6)
#define reg_status(channel) (channel->port_base + 7)
#define reg_cmd(channel) (reg_status(channel))
#define reg_alt_status(channel) (channel->port_base + 0x206)
#define reg_ctl(channel) reg_alt_status(channel)

// reg_alt_status寄存器的一些关键位
#define BIT_STAT_BSY 0X80  // 硬盘忙
#define BIT_STAT_DRDY 0X40 // 驱动器准备好啦
#define BIT_STAT_DRQ 0x8   // 数据传输准备好了

// device寄存器的一些关键位
#define BIT_DEV_MBS 0XA0
#define BIT_DEV_LBA 0X40
#define BIT_DEV_DEV 0X10

// 一些硬盘操作的指令
#define CMD_IDENTIFY 0XEC     // identify指令
#define CMD_READ_SECTOR 0X20  // 读扇区指令
#define CMD_WRITE_SECTOR 0X30 // 写扇区指令

#define max_lba ((80 * 1024 * 1024 / 512) - 1) // 定义可读写的最大扇区数 调试用

uint8_t channel_cnt;            // 通道数
struct ide_channel channels[2]; // 模拟器主板的两个ide通道

// 记录总扩展分区的起始lba，初始为0
int32_t ext_lba_base = 0;

// 记录硬盘主分区和逻辑分区的下标
uint8_t p_no = 0, l_no = 0;

// 分区队列
struct list partition_list;

// 分区表项
struct partition_table_entry
{
    uint8_t bootable;      // 可引导
    uint8_t start_head;    // 起始磁头号
    uint8_t start_sec;     // 起始扇区号
    uint8_t start_chs;     // 起始柱面号
    uint8_t fs_type;       // 分区类型
    uint8_t end_head;      // 结束磁头号
    uint8_t end_sec;       // 结束扇区号
    uint8_t end_chs;       // 结束柱面号
    uint32_t start_lba;    // 本分区起始扇区lba地址
    uint32_t sec_cnt;      // 本分区的扇区数目
} __attribute__((packed)); // 保证此结构是16字节大小

// MBR或EBR所在扇区
struct boot_sector
{
    uint8_t other[446];
    struct partition_table_entry ptable[4];
    uint16_t signature; // 结束标志0x55和0xaa
} __attribute__((packed));

// 将dst中len个相邻字节交换位置后存入buf
static void swapb(const char *dst, char *buf, uint32_t len)
{
    uint32_t idx;
    for (idx = 0; idx < len; idx += 2)
    {
        buf[idx + 1] = *dst++;
        buf[idx] = *dst++;
    }
    buf[idx] = '\0';
}

// 选择读写的硬盘
static void select_disk(struct disk *hd)
{
    uint8_t reg_device = BIT_DEV_MBS | BIT_DEV_LBA;
    if (hd->dev_no == 1)
    {
        // 如果是从盘
        reg_device |= BIT_DEV_DEV;
    }

    outb(reg_dev(hd->my_channel), reg_device);
}

// 向通道channel发送命令
static void cmd_out(struct ide_channel *channel, uint8_t cmd)
{
    /**
     * 只要向硬盘发出了命令便将此标记标置为true
     * 硬盘中断处理程序需要根据它来判断
     */
    channel->expect_int = true;
    outb(reg_cmd(channel), cmd);
}

// 等待30秒 等待期间判断是否可以传输数据
static bool busy_wait(struct disk *hd)
{
    struct ide_channel *channel = hd->my_channel;
    uint16_t time_limit = 30 * 1000;

    while (time_limit -= 10 > 0)
    {
        if (!(inb(reg_status(channel)) & BIT_STAT_BSY))
        {
            return (inb(reg_status(channel)) & BIT_STAT_DRQ);
        }
        else
        {
            mtime_sleep(10);
        }
    }
    return false;
}

// 硬盘读入sec_cnt个扇区的数据到buf
static void read_from_sector(struct disk *hd, void *buf, uint8_t sec_cnt)
{
    uint32_t siz_in_byte;
    if (sec_cnt == 0)
    {
        // 类型转换可能丢失高位的1
        siz_in_byte = 256 * 512;
    }
    else
    {
        siz_in_byte = sec_cnt * 512;
    }

    // 以word为单位的I/O，要将bytes数除以2
    insw(reg_data(hd->my_channel), buf, siz_in_byte / 2);
}

// 获得硬盘参数信息
static void identify(struct disk *hd)
{
    char id_info[512];
    select_disk(hd);
    cmd_out(hd->my_channel, CMD_IDENTIFY);
    sema_down(&hd->my_channel->disk_done);

    if (!busy_wait(hd))
    {
        char err[64];
        sprintf(err, "%s read sector failed!\n", hd->name);
        PANIC(err);
    }

    read_from_sector(hd, id_info, 1);

    char buf[64];
    uint8_t sn_start = 10 * 2; // 序列号起始字节地址
    uint8_t sn_len = 20;
    uint8_t md_start = 27 * 2; // 型号起始字节地址
    uint8_t md_len = 40;

    // 硬盘中的数据传送以16字节为单位，若要打印出可阅读的字节，需要两两呼唤位置
    swapb(&id_info[sn_start], buf, sn_len);
    printk("disk %s info:\n     SN: %s\n", hd->name, buf);
    memset(buf, 0, sizeof(buf));
    swapb(&id_info[md_start], buf, md_len);
    printk("     MODULE: %s\n", buf);
    uint32_t sectors = *(uint32_t *)&id_info[60 * 2];
    printk("     SECTORS: %d\n", sectors);
    printk("     CAPACITY: %dMB\n", sectors * 512 / 1024 / 1024);
}

// 扫描硬盘hd中地址为ext_lba的扇区中的所有分区
static void partition_scan(struct disk *hd, uint32_t ext_lba)
{
    struct boot_sector *bs = sys_malloc(sizeof(struct boot_sector)); // 防止递归调用爆栈
    ide_read(hd, ext_lba, bs, 1);
    uint8_t part_idx = 0;
    struct partition_table_entry *p = bs->ptable;

    while (part_idx++ < 4)
    {
        if (p->fs_type == 0x5)
        {
            // 如果是扩展分区
            if (ext_lba_base != 0)
            {
                // 子扩展分区的start_lba是相对于主引导扇区中的总扩展分区地址，是EBR的起始地址
                partition_scan(hd, p->start_lba + ext_lba_base);
            }
            else
            {
                // 主引导记录所在扇区
                ext_lba_base = p->start_lba;
                partition_scan(hd, p->start_lba);
            }
        }
        else if (p->fs_type != 0)
        {
            if (ext_lba == 0)
            {
                /**
                 * 有效分区类型全是主分区
                 */
                hd->prim_parts[p_no].start_lba = ext_lba + p->start_lba;
                hd->prim_parts[p_no].sec_cnt = p->sec_cnt;
                hd->prim_parts[p_no].my_disk = hd;
                list_append(&partition_list, &hd->prim_parts[p_no].part_tag);
                sprintf(hd->prim_parts[p_no].name, "%s%d", hd->name, p_no + 1);
                p_no++;
                ASSERT(p_no < 4);
            }
            else
            {
                /**
                 * 有效分区类型是逻辑分区
                 */
                hd->logic_parts[l_no].start_lba = ext_lba + p->start_lba; // EBR的起始地址+相对于EBR的偏移扇区，是每个子扩展分区的逻辑分区中起始有效地址
                hd->logic_parts[l_no].sec_cnt = p->sec_cnt;
                hd->logic_parts[l_no].my_disk = hd;
                list_append(&partition_list, &hd->logic_parts[l_no].part_tag);
                sprintf(hd->logic_parts[l_no].name, "%s%d", hd->name, l_no + 5);
                l_no++;
                if (l_no >= 8)
                    return;
            }
        }
        p++;
    }
    sys_free(bs);
}

static bool partition_info(struct list_elem *pelem, int arg)
{
    struct partition *part = elem2entry(struct partition, part_tag, pelem);
    printk("    %s start_lba:0x%x, sec_cnt:0x%x\n", part->name, part->start_lba, part->sec_cnt);

    // 只是为了让主调函数继续向下遍历元素
    return false;
}

void ide_init()
{
    printk("ide_init start\n");

    memset(channels, 0, sizeof(struct ide_channel) * 2);

    uint8_t hd_cnt = *((uint8_t *)(0x475)); // 获取硬盘数量
    ASSERT(hd_cnt > 0);
    channel_cnt = DIV_ROUND_UP(hd_cnt, 2);
    // 计算有几个ide通道

    struct ide_channel *channel;
    uint8_t channel_no = 0;
    uint8_t dev_no = 0;
    list_init(&partition_list);
    while (channel_no < channel_cnt)
    {
        channel = &channels[channel_no];
        sprintf(channel->name, "ide%d", channel_no);

        switch (channel_no)
        {
        case 0:
            channel->port_base = 0x1f0;  // ide0通道的起始端口是0x1f0
            channel->irq_no = 0x20 + 14; // 中断号IRQ14
            break;
        case 1:
            channel->port_base = 0x170;
            channel->irq_no = 0x20 + 15; // IRQ15，响应ide1通道上面的硬盘中断
        default:
            break;
        }

        channel->expect_int = false;
        lock_init(&channel->lock);
        // 信号量初始化为0
        // 使得向硬盘控制器请求数据后能被立刻阻塞
        // 等待数据准备完毕硬盘发出中断请求，由中断处理程序调用sema_up唤醒线程
        sema_init(&channel->disk_done, 0);
        register_handler(channel->irq_no, intr_hd_handler);

        while (dev_no < 2)
        {
            struct disk *hd = &channel->devices[dev_no];
            hd->my_channel = channel;
            hd->dev_no = dev_no;
            sprintf(hd->name, "sd%c", 'a' + channel_no * 2 + dev_no);
            identify(hd);

            if (dev_no != 0) // 内核本身的硬盘hd60M.img不做处理
            {
                partition_scan(hd, 0);
            }
            p_no = 0;
            l_no = 0;
            dev_no++;
        }
        dev_no = 0;
        channel_no++;
    }
    // 打印所有分区信息
    list_traversal(&partition_list, partition_info, (int)NULL);
    printk("ide_init done\n");
}

// 向硬盘控制器写入起始扇区地址以及要读写的扇区数
static void select_sector(struct disk *hd, uint32_t lba, uint8_t sec_cnt)
{
    ASSERT(lba <= max_lba);
    struct ide_channel *channel = hd->my_channel;

    // 写入要读写的扇区数
    outb(reg_sect_cnt(channel), sec_cnt);

    // 写入读写的逻辑扇区地址 lba_low
    outb(reg_lba_l(channel), lba & 0xff);

    // 写入读写的逻辑扇区地址 lba_mid
    outb(reg_lba_m(channel), (lba >> 8) & 0xff);

    // 写入读写的逻辑扇区地址 lba_high
    outb(reg_lba_h(channel), (lba >> 16) & 0xff);

    // 写入device寄存器中保存的lba [27:24]
    outb(reg_dev(channel), BIT_DEV_MBS | BIT_DEV_LBA | (hd->dev_no == 1 ? BIT_DEV_DEV : 0) | lba >> 24);
}

// 将buf中sec_cnt扇区的数据写入硬盘
static void write2sector(struct disk *hd, void *buf, uint8_t sec_cnt)
{
    uint32_t siz_in_byte;
    if (sec_cnt == 0)
    {
        // 类型转换可能丢失高位的1
        siz_in_byte = 256 * 512;
    }
    else
    {
        siz_in_byte = sec_cnt * 512;
    }

    outsw(reg_data(hd->my_channel), buf, siz_in_byte / 2);
}

// 从硬盘读取sec_cnt个扇区到buf
void ide_read(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt)
{
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);

    lock_acquire(&hd->my_channel->lock);

    // 1. 选择操作磁盘
    select_disk(hd);

    uint32_t secs_op;
    uint32_t secs_done = 0;

    while (secs_done < sec_cnt)
    {
        if ((secs_done + 256) <= sec_cnt)
        {
            secs_op = 256;
        }
        else
        {
            secs_op = sec_cnt - secs_done;
        }
        // 2. 选择待读入的扇区数以及起始扇区号
        select_sector(hd, lba + secs_done, secs_op);

        // 3. 选择要执行的命令: 读/写
        cmd_out(hd->my_channel, CMD_READ_SECTOR);

        // 4. 等待硬盘完成读操作后唤醒自己 首先阻塞
        sema_down(&hd->my_channel->disk_done);

        // 5. 检测硬盘状态
        if (!busy_wait(hd))
        {
            char err[64];
            sprintf(err, "%s read sector %d failed!\n", hd->name, lba);
            PANIC(err);
        }

        // 6. 取出硬盘数据
        read_from_sector(hd, (void *)((uint32_t)buf + secs_done * 512), secs_op);
        secs_done += secs_op;
    }
    lock_release(&hd->my_channel->lock);
}

// 从buf写sec_cnt个扇区到硬盘
void ide_write(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt)
{
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);

    lock_acquire(&hd->my_channel->lock);

    // 1. 选择操作磁盘
    select_disk(hd);

    uint32_t secs_op;
    uint32_t secs_done = 0;

    while (secs_done < sec_cnt)
    {
        if ((secs_done + 256) <= sec_cnt)
        {
            secs_op = 256;
        }
        else
        {
            secs_op = sec_cnt - secs_done;
        }
        // 2. 选择待读入的扇区数以及起始扇区号
        select_sector(hd, lba + secs_done, secs_op);

        // 3. 选择要执行的命令: 读/写
        cmd_out(hd->my_channel, CMD_WRITE_SECTOR);

        // 4. 检测硬盘状态
        if (!busy_wait(hd))
        {
            char err[64];
            sprintf(err, "%s write sector %d failed!\n", hd->name, lba);
            PANIC(err);
        }

        // 5. 将数据写入到磁盘
        write2sector(hd, (void *)((uint32_t)buf + secs_done * 512), secs_op);
        // 6. 等待响应写入操作，阻塞自己
        sema_down(&hd->my_channel->disk_done);
        secs_done += secs_op;
    }
    lock_release(&hd->my_channel->lock);
}

// 硬盘中断处理程序
void intr_hd_handler(uint8_t irq_no)
{
    ASSERT(irq_no == 0x2e || irq_no == 0x2f);
    uint8_t ch_no = irq_no - 0x2e;
    struct ide_channel *channel = &channels[ch_no];

    ASSERT(channel->irq_no == irq_no);

    if (channel->expect_int)
    {
        channel->expect_int = false;
        sema_up(&channel->disk_done);
        // 读取硬盘状态使得硬盘认为中断已经被处理，可以继续进行新的读写
        inb(reg_status(channel));
    }
}