#include "isr.h"
#include "../../debug/print.h"
#include "../../debug/panic.h"

static const char* exception_messages[32] = {
    "Divide By Zero","Debug","Non Maskable Interrupt","Breakpoint","Overflow",
    "Bounds","Invalid Opcode","Device Not Available","Double Fault",
    "Coprocessor Segment Overrun","Invalid TSS","Segment Not Present","Stack Fault",
    "General Protection Fault","Page Fault","Reserved","x87 Floating-Point",
    "Alignment Check","Machine Check","SIMD Floating-Point","Reserved","Reserved",
    "Reserved","Reserved","Reserved","Reserved","Reserved","Reserved","Reserved",
    "Reserved","Reserved","Reserved"
};

void isr_handler(struct regs* r) {
    kprint("\n\n=== CPU EXCEPTION ===\n");
    kprint("Type: ");
    if (r->int_no < 32) kprint(exception_messages[r->int_no]);
    else kprint("Unknown");
    kprint("\nint_no=");
    kprint_dec(r->int_no);
    kprint(" err_code=");
    kprint_hex32(r->err_code);

    kprint("\nEIP=");   kprint_hex32(r->eip);
    kprint(" CS=");    kprint_hex32(r->cs);
    kprint(" EFLAGS=");kprint_hex32(r->eflags);

    kprint("\nSystem Halted.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
