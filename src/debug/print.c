#include "print.h"
#include "../vga.h"

void kprint(const char* s) { vga_puts(s); }
void kprint_char(char c) { vga_putc(c); }

void kprint_hex8(uint8_t v) {
    const char* hex = "0123456789ABCDEF";
    vga_putc(hex[(v >> 4) & 0xF]);
    vga_putc(hex[v & 0xF]);
}

void kprint_hex32(uint32_t v) {
    kprint("0x");
    kprint_hex8((v >> 24) & 0xFF);
    kprint_hex8((v >> 16) & 0xFF);
    kprint_hex8((v >> 8) & 0xFF);
    kprint_hex8(v & 0xFF);
}

void kprint_dec(uint32_t v) {
    char buf[11];
    int i = 0;

    if (v == 0) {
        vga_putc('0');
        return;
    }

    while (v > 0 && i < 10) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i--) {
        vga_putc(buf[i]);
    }
}
