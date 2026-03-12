#include "vga.h"

#include <stdint.h>

#include "boot/multiboot.h"

#define VGA_TEXT_BUFFER ((volatile uint16_t*)0xB8000)
#define VGA_TEXT_COLS 80
#define VGA_TEXT_ROWS 25

#define FB_CELL_W 8
#define FB_CELL_H 16
#define FB_GLYPH_W 5
#define FB_GLYPH_H 7

#define VGA_MAX_COLS 160
#define VGA_MAX_ROWS 64
#define FB_CURSOR_W 12
#define FB_CURSOR_H 18

typedef enum {
    VGA_BACKEND_TEXT = 0,
    VGA_BACKEND_FRAMEBUFFER = 1
} vga_backend_t;

static vga_backend_t g_backend = VGA_BACKEND_TEXT;

static uint16_t g_cols = VGA_TEXT_COLS;
static uint16_t g_rows = VGA_TEXT_ROWS;
static uint16_t g_cursor_row = 0;
static uint16_t g_cursor_col = 0;
static uint8_t g_attr = 0x07;

static char g_chars[VGA_MAX_ROWS][VGA_MAX_COLS];
static uint8_t g_attrs[VGA_MAX_ROWS][VGA_MAX_COLS];

static uint8_t* g_fb = 0;
static uint32_t g_fb_pitch = 0;
static uint32_t g_fb_width = 0;
static uint32_t g_fb_height = 0;
static uint8_t g_fb_bpp = 0;
static int g_desktop_enabled = 0;
static int g_fb_batch = 0;
static int g_mouse_visible = 0;
static uint16_t g_mouse_x = 0;
static uint16_t g_mouse_y = 0;
static uint32_t g_mouse_saved[FB_CURSOR_W * FB_CURSOR_H];
static uint16_t g_mouse_saved_w = 0;
static uint16_t g_mouse_saved_h = 0;

static const uint16_t mouse_shape[FB_CURSOR_H] = {
    0x800, 0xC00, 0xE00, 0xF00, 0xF80, 0xFC0, 0xFE0, 0xFFF, 0xE380,
    0xC180, 0x0180, 0x00C0, 0x00C0, 0x0060, 0x0060, 0x0030, 0x0000, 0x0000
};

static inline uint16_t vga_entry(char ch, uint8_t attr) {
    return (uint16_t)(uint8_t)ch | ((uint16_t)attr << 8);
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static uint32_t palette_color(uint8_t idx) {
    static const uint32_t palette[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
        0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
        0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
    };
    return palette[idx & 0x0F];
}

static uint32_t desktop_color_at(uint32_t x, uint32_t y) {
    uint32_t r = 10u + (12u * y) / (g_fb_height ? g_fb_height : 1u);
    uint32_t g = 26u + (54u * y) / (g_fb_height ? g_fb_height : 1u);
    uint32_t b = 42u + (78u * y) / (g_fb_height ? g_fb_height : 1u);

    if (((x / 32u) + (y / 24u)) & 1u) {
        r += 4u;
        g += 6u;
        b += 8u;
    }

    if ((y % 48u) == 0u) {
        r += 10u;
        g += 12u;
        b += 14u;
    }

    return (r << 16) | (g << 8) | b;
}

static void fb_putpixel(uint32_t x, uint32_t y, uint32_t rgb) {
    if (!g_fb || x >= g_fb_width || y >= g_fb_height) return;

    uint8_t* p = g_fb + y * g_fb_pitch + x * (g_fb_bpp / 8u);
    p[0] = (uint8_t)(rgb & 0xFFu);
    p[1] = (uint8_t)((rgb >> 8) & 0xFFu);
    p[2] = (uint8_t)((rgb >> 16) & 0xFFu);
    if (g_fb_bpp == 32) {
        p[3] = 0;
    }
}

static uint32_t fb_getpixel(uint32_t x, uint32_t y) {
    if (!g_fb || x >= g_fb_width || y >= g_fb_height) return 0;

    uint8_t* p = g_fb + y * g_fb_pitch + x * (g_fb_bpp / 8u);
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static void fb_fill_rect_raw(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb) {
    if (!g_fb || w == 0 || h == 0) return;
    if (x >= g_fb_width || y >= g_fb_height) return;
    if (x + w > g_fb_width) w = g_fb_width - x;
    if (y + h > g_fb_height) h = g_fb_height - y;

    for (uint32_t py = 0; py < h; py++) {
        for (uint32_t px = 0; px < w; px++) {
            fb_putpixel(x + px, y + py, rgb);
        }
    }
}

static void fb_mouse_hide(void) {
    if (!g_mouse_visible || !g_fb) return;

    for (uint16_t py = 0; py < g_mouse_saved_h; py++) {
        for (uint16_t px = 0; px < g_mouse_saved_w; px++) {
            fb_putpixel(g_mouse_x + px, g_mouse_y + py,
                        g_mouse_saved[py * FB_CURSOR_W + px]);
        }
    }

    g_mouse_visible = 0;
}

static void fb_mouse_show(void) {
    if (!g_fb || !g_desktop_enabled) return;

    g_mouse_saved_w = (uint16_t)((g_mouse_x + FB_CURSOR_W <= g_fb_width) ?
                                 FB_CURSOR_W : (g_fb_width - g_mouse_x));
    g_mouse_saved_h = (uint16_t)((g_mouse_y + FB_CURSOR_H <= g_fb_height) ?
                                 FB_CURSOR_H : (g_fb_height - g_mouse_y));

    for (uint16_t py = 0; py < g_mouse_saved_h; py++) {
        for (uint16_t px = 0; px < g_mouse_saved_w; px++) {
            g_mouse_saved[py * FB_CURSOR_W + px] = fb_getpixel(g_mouse_x + px, g_mouse_y + py);
        }
    }

    for (uint16_t py = 0; py < g_mouse_saved_h; py++) {
        uint16_t bits = mouse_shape[py];
        for (uint16_t px = 0; px < g_mouse_saved_w; px++) {
            if ((bits & (0x800u >> px)) == 0) continue;
            fb_putpixel(g_mouse_x + px, g_mouse_y + py, 0xF2F6FF);
            if (px > 0 && (bits & (0x800u >> (px - 1u))) == 0) {
                fb_putpixel(g_mouse_x + px - 1u, g_mouse_y + py, 0x203040);
            }
        }
    }

    g_mouse_visible = 1;
}

static char glyph_normalize(char ch) {
    if (ch >= 'a' && ch <= 'z') return (char)(ch - 'a' + 'A');
    return ch;
}

static uint8_t glyph_row_bits(char ch, uint8_t row) {
    ch = glyph_normalize(ch);

    switch (ch) {
        case 'A': { static const uint8_t r[7] = {14, 17, 17, 31, 17, 17, 17}; return r[row]; }
        case 'B': { static const uint8_t r[7] = {30, 17, 17, 30, 17, 17, 30}; return r[row]; }
        case 'C': { static const uint8_t r[7] = {14, 17, 16, 16, 16, 17, 14}; return r[row]; }
        case 'D': { static const uint8_t r[7] = {30, 17, 17, 17, 17, 17, 30}; return r[row]; }
        case 'E': { static const uint8_t r[7] = {31, 16, 16, 30, 16, 16, 31}; return r[row]; }
        case 'F': { static const uint8_t r[7] = {31, 16, 16, 30, 16, 16, 16}; return r[row]; }
        case 'G': { static const uint8_t r[7] = {14, 17, 16, 23, 17, 17, 14}; return r[row]; }
        case 'H': { static const uint8_t r[7] = {17, 17, 17, 31, 17, 17, 17}; return r[row]; }
        case 'I': { static const uint8_t r[7] = {31, 4, 4, 4, 4, 4, 31}; return r[row]; }
        case 'J': { static const uint8_t r[7] = {7, 2, 2, 2, 2, 18, 12}; return r[row]; }
        case 'K': { static const uint8_t r[7] = {17, 18, 20, 24, 20, 18, 17}; return r[row]; }
        case 'L': { static const uint8_t r[7] = {16, 16, 16, 16, 16, 16, 31}; return r[row]; }
        case 'M': { static const uint8_t r[7] = {17, 27, 21, 21, 17, 17, 17}; return r[row]; }
        case 'N': { static const uint8_t r[7] = {17, 17, 25, 21, 19, 17, 17}; return r[row]; }
        case 'O': { static const uint8_t r[7] = {14, 17, 17, 17, 17, 17, 14}; return r[row]; }
        case 'P': { static const uint8_t r[7] = {30, 17, 17, 30, 16, 16, 16}; return r[row]; }
        case 'Q': { static const uint8_t r[7] = {14, 17, 17, 17, 21, 18, 13}; return r[row]; }
        case 'R': { static const uint8_t r[7] = {30, 17, 17, 30, 20, 18, 17}; return r[row]; }
        case 'S': { static const uint8_t r[7] = {15, 16, 16, 14, 1, 1, 30}; return r[row]; }
        case 'T': { static const uint8_t r[7] = {31, 4, 4, 4, 4, 4, 4}; return r[row]; }
        case 'U': { static const uint8_t r[7] = {17, 17, 17, 17, 17, 17, 14}; return r[row]; }
        case 'V': { static const uint8_t r[7] = {17, 17, 17, 17, 17, 10, 4}; return r[row]; }
        case 'W': { static const uint8_t r[7] = {17, 17, 17, 21, 21, 21, 10}; return r[row]; }
        case 'X': { static const uint8_t r[7] = {17, 17, 10, 4, 10, 17, 17}; return r[row]; }
        case 'Y': { static const uint8_t r[7] = {17, 17, 10, 4, 4, 4, 4}; return r[row]; }
        case 'Z': { static const uint8_t r[7] = {31, 1, 2, 4, 8, 16, 31}; return r[row]; }
        case '0': { static const uint8_t r[7] = {14, 17, 19, 21, 25, 17, 14}; return r[row]; }
        case '1': { static const uint8_t r[7] = {4, 12, 4, 4, 4, 4, 14}; return r[row]; }
        case '2': { static const uint8_t r[7] = {14, 17, 1, 2, 4, 8, 31}; return r[row]; }
        case '3': { static const uint8_t r[7] = {30, 1, 1, 14, 1, 1, 30}; return r[row]; }
        case '4': { static const uint8_t r[7] = {2, 6, 10, 18, 31, 2, 2}; return r[row]; }
        case '5': { static const uint8_t r[7] = {31, 16, 16, 30, 1, 1, 30}; return r[row]; }
        case '6': { static const uint8_t r[7] = {14, 16, 16, 30, 17, 17, 14}; return r[row]; }
        case '7': { static const uint8_t r[7] = {31, 1, 2, 4, 8, 8, 8}; return r[row]; }
        case '8': { static const uint8_t r[7] = {14, 17, 17, 14, 17, 17, 14}; return r[row]; }
        case '9': { static const uint8_t r[7] = {14, 17, 17, 15, 1, 1, 14}; return r[row]; }
        case '.': { static const uint8_t r[7] = {0, 0, 0, 0, 0, 12, 12}; return r[row]; }
        case ',': { static const uint8_t r[7] = {0, 0, 0, 0, 12, 12, 8}; return r[row]; }
        case ':': { static const uint8_t r[7] = {0, 12, 12, 0, 12, 12, 0}; return r[row]; }
        case ';': { static const uint8_t r[7] = {0, 12, 12, 0, 12, 12, 8}; return r[row]; }
        case '!': { static const uint8_t r[7] = {4, 4, 4, 4, 4, 0, 4}; return r[row]; }
        case '?': { static const uint8_t r[7] = {14, 17, 1, 2, 4, 0, 4}; return r[row]; }
        case '-': { static const uint8_t r[7] = {0, 0, 0, 31, 0, 0, 0}; return r[row]; }
        case '_': { static const uint8_t r[7] = {0, 0, 0, 0, 0, 0, 31}; return r[row]; }
        case '+': { static const uint8_t r[7] = {0, 4, 4, 31, 4, 4, 0}; return r[row]; }
        case '=': { static const uint8_t r[7] = {0, 31, 0, 31, 0, 0, 0}; return r[row]; }
        case '/': { static const uint8_t r[7] = {1, 2, 2, 4, 8, 8, 16}; return r[row]; }
        case '\\': { static const uint8_t r[7] = {16, 8, 8, 4, 2, 2, 1}; return r[row]; }
        case '|': { static const uint8_t r[7] = {4, 4, 4, 4, 4, 4, 4}; return r[row]; }
        case '(': { static const uint8_t r[7] = {2, 4, 8, 8, 8, 4, 2}; return r[row]; }
        case ')': { static const uint8_t r[7] = {8, 4, 2, 2, 2, 4, 8}; return r[row]; }
        case '[': { static const uint8_t r[7] = {14, 8, 8, 8, 8, 8, 14}; return r[row]; }
        case ']': { static const uint8_t r[7] = {14, 2, 2, 2, 2, 2, 14}; return r[row]; }
        case '<': { static const uint8_t r[7] = {2, 4, 8, 16, 8, 4, 2}; return r[row]; }
        case '>': { static const uint8_t r[7] = {8, 4, 2, 1, 2, 4, 8}; return r[row]; }
        case '*': { static const uint8_t r[7] = {0, 17, 10, 31, 10, 17, 0}; return r[row]; }
        case '#': { static const uint8_t r[7] = {10, 31, 10, 10, 31, 10, 0}; return r[row]; }
        case '@': { static const uint8_t r[7] = {14, 17, 23, 21, 23, 16, 14}; return r[row]; }
        case '$': { static const uint8_t r[7] = {4, 15, 20, 14, 5, 30, 4}; return r[row]; }
        case '%': { static const uint8_t r[7] = {24, 25, 2, 4, 8, 19, 3}; return r[row]; }
        case '^': { static const uint8_t r[7] = {4, 10, 17, 0, 0, 0, 0}; return r[row]; }
        case '&': { static const uint8_t r[7] = {12, 18, 20, 8, 21, 18, 13}; return r[row]; }
        case '\'': { static const uint8_t r[7] = {4, 4, 8, 0, 0, 0, 0}; return r[row]; }
        case '"': { static const uint8_t r[7] = {10, 10, 0, 0, 0, 0, 0}; return r[row]; }
        case ' ': return 0;
        default: { static const uint8_t r[7] = {31, 1, 2, 4, 0, 4, 0}; return r[row]; }
    }
}

static void fb_draw_cell(uint16_t row, uint16_t col, char ch, uint8_t attr) {
    uint8_t bg_idx = (uint8_t)((attr >> 4) & 0x0F);
    uint32_t bg = palette_color(bg_idx);
    uint32_t fg = palette_color(attr & 0x0F);
    uint32_t x0 = (uint32_t)col * FB_CELL_W;
    uint32_t y0 = (uint32_t)row * FB_CELL_H;

    for (uint32_t py = 0; py < FB_CELL_H; py++) {
        for (uint32_t px = 0; px < FB_CELL_W; px++) {
            if (g_desktop_enabled && bg_idx == 0) {
                fb_putpixel(x0 + px, y0 + py, desktop_color_at(x0 + px, y0 + py));
            } else {
                fb_putpixel(x0 + px, y0 + py, bg);
            }
        }
    }

    if (ch == ' ' || ch == '\0') return;

    for (uint8_t gy = 0; gy < FB_GLYPH_H; gy++) {
        uint8_t bits = glyph_row_bits(ch, gy);
        for (uint8_t gx = 0; gx < FB_GLYPH_W; gx++) {
            if ((bits & (1u << (FB_GLYPH_W - 1u - gx))) == 0) continue;

            uint32_t px = x0 + 1u + gx;
            uint32_t py = y0 + 1u + (uint32_t)gy * 2u;
            fb_putpixel(px, py, fg);
            fb_putpixel(px, py + 1u, fg);
            if (px + 1u < x0 + FB_CELL_W - 1u) {
                fb_putpixel(px + 1u, py, fg);
                fb_putpixel(px + 1u, py + 1u, fg);
            }
        }
    }
}

static void refresh_cell(uint16_t row, uint16_t col) {
    if (row >= g_rows || col >= g_cols) return;

    if (g_backend == VGA_BACKEND_TEXT) {
        VGA_TEXT_BUFFER[row * g_cols + col] = vga_entry(g_chars[row][col], g_attrs[row][col]);
        return;
    }

    if (!g_fb_batch) fb_mouse_hide();
    fb_draw_cell(row, col, g_chars[row][col], g_attrs[row][col]);
    if (!g_fb_batch) fb_mouse_show();
}

static void refresh_all(void) {
    if (g_backend == VGA_BACKEND_FRAMEBUFFER) {
        g_fb_batch++;
        fb_mouse_hide();
    }
    for (uint16_t y = 0; y < g_rows; y++) {
        for (uint16_t x = 0; x < g_cols; x++) {
            refresh_cell(y, x);
        }
    }
    if (g_backend == VGA_BACKEND_FRAMEBUFFER) {
        g_fb_batch--;
        fb_mouse_show();
    }
}

static void vga_update_hw_cursor(void) {
    if (g_backend != VGA_BACKEND_TEXT) return;

    uint16_t pos = (uint16_t)(g_cursor_row * g_cols + g_cursor_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void blank_row(uint16_t row) {
    for (uint16_t x = 0; x < g_cols; x++) {
        g_chars[row][x] = ' ';
        g_attrs[row][x] = g_attr;
    }
}

static void vga_scroll_if_needed(void) {
    if (g_cursor_row < g_rows) return;

    for (uint16_t y = 1; y < g_rows; y++) {
        for (uint16_t x = 0; x < g_cols; x++) {
            g_chars[y - 1][x] = g_chars[y][x];
            g_attrs[y - 1][x] = g_attrs[y][x];
        }
    }

    blank_row((uint16_t)(g_rows - 1u));
    g_cursor_row = (uint16_t)(g_rows - 1u);
    refresh_all();
}

void vga_init(const multiboot_info_t* mb) {
    g_backend = VGA_BACKEND_TEXT;
    g_cols = VGA_TEXT_COLS;
    g_rows = VGA_TEXT_ROWS;
    g_cursor_row = 0;
    g_cursor_col = 0;
    g_attr = 0x07;
    g_desktop_enabled = 0;
    g_mouse_visible = 0;
    g_mouse_x = 0;
    g_mouse_y = 0;

    if (mb && (mb->flags & MB_INFO_FRAMEBUFFER) &&
        mb->framebuffer_type == MB_FRAMEBUFFER_TYPE_RGB &&
        (mb->framebuffer_bpp == 32 || mb->framebuffer_bpp == 24) &&
        mb->framebuffer_addr != 0) {
        uint32_t cols = mb->framebuffer_width / FB_CELL_W;
        uint32_t rows = mb->framebuffer_height / FB_CELL_H;

        if (cols >= 40 && cols <= VGA_MAX_COLS && rows >= 20 && rows <= VGA_MAX_ROWS) {
            g_backend = VGA_BACKEND_FRAMEBUFFER;
            g_cols = (uint16_t)cols;
            g_rows = (uint16_t)rows;
            g_fb = (uint8_t*)(uintptr_t)mb->framebuffer_addr;
            g_fb_pitch = mb->framebuffer_pitch;
            g_fb_width = mb->framebuffer_width;
            g_fb_height = mb->framebuffer_height;
            g_fb_bpp = mb->framebuffer_bpp;
            g_desktop_enabled = 1;
        }
    }

    for (uint16_t y = 0; y < VGA_MAX_ROWS; y++) {
        for (uint16_t x = 0; x < VGA_MAX_COLS; x++) {
            g_chars[y][x] = ' ';
            g_attrs[y][x] = g_attr;
        }
    }

    vga_clear();
}

void vga_clear(void) {
    g_cursor_row = 0;
    g_cursor_col = 0;

    for (uint16_t y = 0; y < g_rows; y++) {
        blank_row(y);
    }

    refresh_all();
    vga_update_hw_cursor();
}

void vga_putc(char ch) {
    if (ch == '\n') {
        g_cursor_col = 0;
        g_cursor_row++;
        vga_scroll_if_needed();
        vga_update_hw_cursor();
        return;
    }

    if (ch == '\r') {
        g_cursor_col = 0;
        vga_update_hw_cursor();
        return;
    }

    if (ch == '\t') {
        uint16_t next = (uint16_t)((g_cursor_col + 8u) & ~(uint16_t)7u);
        if (next >= g_cols) {
            g_cursor_col = 0;
            g_cursor_row++;
            vga_scroll_if_needed();
        } else {
            g_cursor_col = next;
        }
        vga_update_hw_cursor();
        return;
    }

    if (g_cursor_row >= g_rows || g_cursor_col >= g_cols) {
        vga_scroll_if_needed();
    }

    g_chars[g_cursor_row][g_cursor_col] = ch;
    g_attrs[g_cursor_row][g_cursor_col] = g_attr;
    refresh_cell(g_cursor_row, g_cursor_col);

    g_cursor_col++;
    if (g_cursor_col >= g_cols) {
        g_cursor_col = 0;
        g_cursor_row++;
        vga_scroll_if_needed();
    }

    vga_update_hw_cursor();
}

void vga_puts(const char* s) {
    while (s && *s) {
        vga_putc(*s++);
    }
}

void vga_backspace(void) {
    if (g_cursor_col > 0) {
        g_cursor_col--;
    } else if (g_cursor_row > 0) {
        g_cursor_row--;
        g_cursor_col = (uint16_t)(g_cols - 1u);
    } else {
        return;
    }

    g_chars[g_cursor_row][g_cursor_col] = ' ';
    g_attrs[g_cursor_row][g_cursor_col] = g_attr;
    refresh_cell(g_cursor_row, g_cursor_col);
    vga_update_hw_cursor();
}

uint16_t vga_cols(void) {
    return g_cols;
}

uint16_t vga_rows(void) {
    return g_rows;
}

void vga_get_cursor(uint16_t* row, uint16_t* col) {
    if (row) *row = g_cursor_row;
    if (col) *col = g_cursor_col;
}

void vga_set_cursor(uint16_t row, uint16_t col) {
    if (g_rows == 0 || g_cols == 0) return;

    if (row >= g_rows) row = (uint16_t)(g_rows - 1u);
    if (col >= g_cols) col = (uint16_t)(g_cols - 1u);
    g_cursor_row = row;
    g_cursor_col = col;
    vga_update_hw_cursor();
}

void vga_get_attr(uint8_t* attr) {
    if (attr) *attr = g_attr;
}

int vga_is_framebuffer(void) {
    return g_backend == VGA_BACKEND_FRAMEBUFFER;
}

uint16_t vga_pixel_width(void) {
    return (uint16_t)g_fb_width;
}

uint16_t vga_pixel_height(void) {
    return (uint16_t)g_fb_height;
}

void vga_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb) {
    if (g_backend != VGA_BACKEND_FRAMEBUFFER) return;
    if (!g_fb_batch) fb_mouse_hide();
    fb_fill_rect_raw(x, y, w, h, rgb);
    if (!g_fb_batch) fb_mouse_show();
}

void vga_desktop_enable(int enabled) {
    g_desktop_enabled = enabled != 0;
    if (g_backend == VGA_BACKEND_FRAMEBUFFER) {
        vga_clear();
    }
}

void vga_mouse_set(uint16_t x, uint16_t y, int visible) {
    if (g_backend != VGA_BACKEND_FRAMEBUFFER) return;

    fb_mouse_hide();

    if (g_fb_width > 0 && x >= g_fb_width) x = (uint16_t)(g_fb_width - 1u);
    if (g_fb_height > 0 && y >= g_fb_height) y = (uint16_t)(g_fb_height - 1u);

    g_mouse_x = x;
    g_mouse_y = y;

    if (visible) {
        fb_mouse_show();
    }
}

void vga_batch_begin(void) {
    if (g_backend != VGA_BACKEND_FRAMEBUFFER) return;
    if (g_fb_batch == 0) fb_mouse_hide();
    g_fb_batch++;
}

void vga_batch_end(void) {
    if (g_backend != VGA_BACKEND_FRAMEBUFFER) return;
    if (g_fb_batch == 0) return;
    g_fb_batch--;
    if (g_fb_batch == 0) fb_mouse_show();
}

void vga_read_cell(uint16_t row, uint16_t col, char* ch, uint8_t* attr) {
    if (row >= g_rows || col >= g_cols) {
        if (ch) *ch = ' ';
        if (attr) *attr = g_attr;
        return;
    }

    if (ch) *ch = g_chars[row][col];
    if (attr) *attr = g_attrs[row][col];
}

void vga_write_cell(uint16_t row, uint16_t col, char ch, uint8_t attr) {
    if (row >= g_rows || col >= g_cols) return;

    g_chars[row][col] = ch;
    g_attrs[row][col] = attr;
    refresh_cell(row, col);
}
