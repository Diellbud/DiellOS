#pragma once

#include <stdint.h>

typedef struct multiboot_info multiboot_info_t;

void vga_init(const multiboot_info_t* mb);
void vga_clear(void);
void vga_puts(const char* s);
void vga_putc(char c);
void vga_backspace(void);

uint16_t vga_cols(void);
uint16_t vga_rows(void);

void vga_get_cursor(uint16_t* row, uint16_t* col);
void vga_set_cursor(uint16_t row, uint16_t col);
void vga_read_cell(uint16_t row, uint16_t col, char* ch, uint8_t* attr);
void vga_write_cell(uint16_t row, uint16_t col, char ch, uint8_t attr);
void vga_get_attr(uint8_t* attr);
int vga_is_framebuffer(void);
uint16_t vga_pixel_width(void);
uint16_t vga_pixel_height(void);
void vga_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb);
void vga_desktop_enable(int enabled);
void vga_mouse_set(uint16_t x, uint16_t y, int visible);
void vga_batch_begin(void);
void vga_batch_end(void);
