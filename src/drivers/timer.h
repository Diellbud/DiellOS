#pragma once
#include <stdint.h>

void timer_init(uint32_t hz);
uint32_t timer_ticks(void);
uint32_t timer_seconds(void);
