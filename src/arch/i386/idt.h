#pragma once
#include <stdint.h>

void idt_init(void);

void idt_set_gate_public(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);


struct regs {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};
