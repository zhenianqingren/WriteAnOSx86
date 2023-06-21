#ifndef __KERNEL_BITMAP_H
#define __KERNEL_BITMAP_H
#include "global.h"

struct bitmap
{
    uint32_t btmp_bytes_len;
    uint8_t *bits;
};

void bitmap_init(struct bitmap* btmp);
int bitmap_scan_test(struct bitmap* btmp,uint32_t bit_idx);
uint32_t  bitmap_scan(struct bitmap* btmp,uint32_t cnt);
void bitmap_set(struct bitmap* btmp,uint32_t bit_idx,int8_t value);
#endif