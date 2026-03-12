#pragma once
#include <stdint.h>

typedef enum {
    DONUT_MODE_BLOCKS = 0,
    DONUT_MODE_DOTS = 1,
    DONUT_MODE_CHARS = 2,
    DONUT_MODE_SCAN = 3
} donut_mode_t;

void donut_run(uint32_t duration_ticks, donut_mode_t mode);
