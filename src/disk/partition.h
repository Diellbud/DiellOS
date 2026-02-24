#pragma once
#include <stdint.h>

typedef struct {
    int      present;
    uint8_t  type;
    uint32_t lba_start;
    uint32_t lba_count;
    uint8_t  bootable;
} part_info_t;

int part_read_table(part_info_t out[4]);
int part_get(int index, part_info_t* out);