#include "bitmap.h"
#include "stdint.h"
#include "string.h"
#include "print.h"
#include "interrupt.h"
#include "debug.h"

#define BITMAP_MASK 1

/*初始化位图*/
void bitmap_init(struct bitmap *btmp)
{
    memset(btmp->bits, 0, btmp->btmp_bytes_len);
}

/*第bit_idx若为1 则返回true*/
int bitmap_scan_test(struct bitmap *btmp, uint32_t bit_idx)
{
    uint32_t byte_idx = bit_idx / 8;
    uint32_t bit_odd = bit_idx % 8;
    return (btmp->bits[byte_idx] & (BITMAP_MASK << bit_odd));
}

/*在位图中连续申请cnt个位，若成功返回下标，否则返回-1*/
uint32_t bitmap_scan(struct bitmap *btmp, uint32_t cnt)
{
    uint32_t idx_byte = 0;
    /*找到第几个字节开始有未分配页*/
    while (idx_byte < btmp->btmp_bytes_len && btmp->bits[idx_byte] == 0xff)
    {
        idx_byte++;
    }

    ASSERT(idx_byte < btmp->btmp_bytes_len);
    if (idx_byte == btmp->btmp_bytes_len)
    {
        return -1;
    }

    /*找到字节内的偏移量*/
    uint32_t idx_bit = 0;
    while ((BITMAP_MASK << idx_bit) & btmp->bits[idx_byte])
    {
        idx_bit++;
    }

    uint32_t bit_idx_start = idx_byte * 8 + idx_bit;
    if (cnt == 1)
    {
        return bit_idx_start;
    }
    /*剩余可扫描数*/
    uint32_t bit_left = btmp->btmp_bytes_len - bit_idx_start;
    uint32_t next = bit_idx_start + 1;
    uint32_t count = 1; // 已扫描到bit_idx_start
    bit_idx_start = -1;
    while (bit_left-- > 0)
    {
        if (bitmap_scan_test(btmp, next))
        {
            /*如果发现有已分配的，下一个重新计数*/
            count = 0;
        }
        else
        {
            count++;
        }

        if (count == cnt)
        {
            bit_idx_start = next - cnt + 1;
            break;
        }
        next++;
    }
    return bit_idx_start;
}

/*将位图btmp的bit_idx位设置为value*/
void bitmap_set(struct bitmap *btmp, uint32_t bit_idx, int8_t value)
{
    ASSERT((value == 0) || (value == 1));
    uint32_t byte_idx = bit_idx / 8;
    uint32_t bit_odd = bit_idx % 8;

    if (value)
    {
        btmp->bits[byte_idx] |= (BITMAP_MASK << bit_odd);
    }
    else
    {
        btmp->bits[byte_idx] &= ~(BITMAP_MASK << bit_odd);
    }
}