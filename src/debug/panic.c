#include "panic.h"
#include "print.h"

void panic(const char* msg, const char* file, int line) {
    kprint("\n\n=== KERNEL PANIC ===\n");
    kprint(msg);
    kprint("\nAt: ");
    kprint(file);
    kprint(":");
    kprint_dec((unsigned)line);
    kprint("\nSystem Halted.\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
