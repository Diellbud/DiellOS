#pragma once
#include <stdint.h>
#include "idt.h"

typedef void (*irq_handler_t)(struct regs* r);

void irq_init(void);
void irq_register_handler(uint8_t irq, irq_handler_t handler);
void irq_unregister_handler(uint8_t irq);
