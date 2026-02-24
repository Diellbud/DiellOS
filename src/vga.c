#include "vga.h"
#include <stdint.h>

#define VGA_TEXT_BUFFER ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static uint16_t cursor_row = 0;
static uint16_t cursor_col = 0;

static uint8_t vga_attr = 0x07;

static inline uint16_t vga_entry(char c, uint8_t attr) {
    return (uint16_t)c | ((uint16_t)attr << 8);
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void vga_update_hw_cursor(void) {
    uint16_t pos = (uint16_t)(cursor_row * VGA_WIDTH + cursor_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_scroll_if_needed(void) {
    if (cursor_row < VGA_HEIGHT) return;

    for (uint16_t y = 1; y < VGA_HEIGHT; y++) {
        for (uint16_t x = 0; x < VGA_WIDTH; x++) {
            VGA_TEXT_BUFFER[(y - 1) * VGA_WIDTH + x] = VGA_TEXT_BUFFER[y * VGA_WIDTH + x];
        }
    }

    for (uint16_t x = 0; x < VGA_WIDTH; x++) {
        VGA_TEXT_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', vga_attr);
    }

    cursor_row = VGA_HEIGHT - 1;
}

void vga_clear(void) {
    cursor_row = 0;
    cursor_col = 0;

    for (uint16_t y = 0; y < VGA_HEIGHT; y++) {
        for (uint16_t x = 0; x < VGA_WIDTH; x++) {
            VGA_TEXT_BUFFER[y * VGA_WIDTH + x] = vga_entry(' ', vga_attr);
        }
    }

    vga_update_hw_cursor();
}

void vga_putc(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        vga_scroll_if_needed();
        vga_update_hw_cursor();
        return;
    }

    if (c == '\r') {
        cursor_col = 0;
        vga_update_hw_cursor();
        return;
    }

    if (c == '\t') {
        uint16_t next = (uint16_t)((cursor_col + 8) & ~(uint16_t)7);
        if (next >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
            vga_scroll_if_needed();
        } else {
            cursor_col = next;
        }
        vga_update_hw_cursor();
        return;
    }

    VGA_TEXT_BUFFER[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(c, vga_attr);
    cursor_col++;

    if (cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        cursor_row++;
        vga_scroll_if_needed();
    }

    vga_update_hw_cursor();
}

void vga_puts(const char* s) {
    while (*s) {
        vga_putc(*s++);
    }
}

void vga_backspace(void) {
    if (cursor_col > 0) {
        cursor_col--;
    } else if (cursor_row > 0) {
        cursor_row--;
        cursor_col = VGA_WIDTH - 1;
    } else {
        return;
    }

    VGA_TEXT_BUFFER[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(' ', vga_attr);
    vga_update_hw_cursor();
}


void vga_get_cursor(uint16_t* row, uint16_t* col) {
    if (row) *row = cursor_row;
    if (col) *col = cursor_col;
}

void vga_set_cursor(uint16_t row, uint16_t col) {
    cursor_row = row;
    cursor_col = col;
    vga_update_hw_cursor();
}

void vga_get_attr(uint8_t* attr) {
    if (attr) *attr = vga_attr;
}

void vga_read_cell(uint16_t row, uint16_t col, char* ch, uint8_t* attr) {
    uint16_t v = VGA_TEXT_BUFFER[row * VGA_WIDTH + col];
    if (ch) *ch = (char)(v & 0xFF);
    if (attr) *attr = (uint8_t)((v >> 8) & 0xFF);
}

void vga_write_cell(uint16_t row, uint16_t col, char ch, uint8_t attr) {
    VGA_TEXT_BUFFER[row * VGA_WIDTH + col] = vga_entry(ch, attr);
}
