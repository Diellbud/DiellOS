#pragma once
#include <stdint.h>

int fat16_mount(uint32_t part_lba_start);
void fat16_ls_root(void);
void fat16_cat(const char* user_name); 