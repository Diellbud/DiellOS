#include "irq.h"
#include "pic.h"
#include "io.h"
#include "../../debug/print.h"

static irq_handler_t irq_handlers[16] = {0};
static int irq_debug_unhandled = 0;

extern void idt_set_gate_public(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

void irq_handler(struct regs* r) {
    uint8_t int_no = (uint8_t)r->int_no;
    uint8_t irq = int_no - 32;

    if (irq < 16 && irq_handlers[irq]) {
        irq_handlers[irq](r);
    } else {
    if (irq_debug_unhandled && irq < 16) {
        kprint("\n[Unhandled IRQ ");
        kprint_dec(irq);
        kprint("]\n");
    }
}

    pic_send_eoi(irq);
}


void irq_register_handler(uint8_t irq, irq_handler_t handler) {
    if (irq < 16) irq_handlers[irq] = handler;
}

void irq_init(void) {
    pic_remap(0x20, 0x28);

    idt_set_gate_public(32, (uint32_t)irq0,  0x08, 0x8E);
    idt_set_gate_public(33, (uint32_t)irq1,  0x08, 0x8E);
    idt_set_gate_public(34, (uint32_t)irq2,  0x08, 0x8E);
    idt_set_gate_public(35, (uint32_t)irq3,  0x08, 0x8E);
    idt_set_gate_public(36, (uint32_t)irq4,  0x08, 0x8E);
    idt_set_gate_public(37, (uint32_t)irq5,  0x08, 0x8E);
    idt_set_gate_public(38, (uint32_t)irq6,  0x08, 0x8E);
    idt_set_gate_public(39, (uint32_t)irq7,  0x08, 0x8E);
    idt_set_gate_public(40, (uint32_t)irq8,  0x08, 0x8E);
    idt_set_gate_public(41, (uint32_t)irq9,  0x08, 0x8E);
    idt_set_gate_public(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate_public(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate_public(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate_public(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate_public(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate_public(47, (uint32_t)irq15, 0x08, 0x8E);

    for (uint8_t i = 0; i < 16; i++) pic_set_mask(i);

    pic_clear_mask(0);
    pic_clear_mask(1);
}

void irq_unregister_handler(uint8_t irq) {
    if (irq < 16) irq_handlers[irq] = 0;
}
