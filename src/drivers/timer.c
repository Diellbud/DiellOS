#include "timer.h"
#include "../arch/i386/io.h"
#include "../arch/i386/irq.h"
#include "../debug/print.h"

static volatile uint32_t ticks = 0;
static volatile uint32_t seconds = 0;
static uint32_t hz_local = 100;

static void timer_callback(struct regs* r) {
    (void)r;
    ticks++;

    if (ticks % hz_local == 0) {
        seconds++;
    }
}

uint32_t timer_ticks(void) { return ticks; }
uint32_t timer_seconds(void) { return seconds; }

void timer_init(uint32_t hz) {
    hz_local = hz ? hz : 100;
    irq_register_handler(0, timer_callback);

    uint32_t divisor = 1193182 / hz_local;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}
