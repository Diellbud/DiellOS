#pragma once
#include <stdint.h>

void vga_clear(void);
void vga_puts(const char* s);
void vga_putc(char c);
void vga_backspace(void);
void vga_get_cursor(uint16_t* row, uint16_t* col);
void vga_set_cursor(uint16_t row, uint16_t col);
void vga_read_cell(uint16_t row, uint16_t col, char* ch, uint8_t* attr);
void vga_write_cell(uint16_t row, uint16_t col, char ch, uint8_t attr);
void vga_get_attr(uint8_t* attr);

