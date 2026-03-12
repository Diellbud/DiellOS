#include "donut.h"
#include <stdint.h>
#include "../console.h"
#include "../vga.h"

#define MAX_SCREEN_W 160
#define MAX_SCREEN_H 64
#define MAX_GFX_W    256
#define MAX_GFX_H    192

#define FIX_SHIFT    14
#define FIX_ONE      (1 << FIX_SHIFT)

#define THETA_STEP   3
#define PHI_STEP     2
#define FRAME_COUNT  128

#define K2_Z         5
#define ZOOM_Q14     5500

static const char shades[] = ".,-~:;=!*#$@";
#define SHADE_N ((int)(sizeof(shades) - 1))

#define FRAME_CELLS (MAX_SCREEN_W * MAX_SCREEN_H)
#define GFX_FRAME_CELLS (MAX_GFX_W * MAX_GFX_H)

static uint16_t g_char_prev[FRAME_CELLS];
static int g_char_prev_init = 0;

static const int16_t sin_q[65] = {
    0, 402, 803, 1205, 1605, 2005, 2404, 2801, 3196, 3589, 3980, 4369, 4755,
    5139, 5519, 5896, 6269, 6639, 7004, 7366, 7723, 8075, 8423, 8765, 9102,
    9434, 9759, 10079, 10393, 10701, 11002, 11297, 11585, 11866, 12139, 12406,
    12664, 12914, 13156, 13389, 13614, 13830, 14036, 14234, 14422, 14601, 14770,
    14929, 15079, 15219, 15349, 15469, 15579, 15678, 15767, 15846, 15913, 15970,
    16017, 16053, 16078, 16093, 16097, 16091, 16384
};

static int32_t fsin(uint8_t a) {
    uint8_t quad = (a >> 6) & 3;
    uint8_t idx  = a & 63;
    int32_t v  = (int32_t)sin_q[idx];
    int32_t v2 = (int32_t)sin_q[64 - idx];
    switch (quad) {
        case 0: return v;
        case 1: return v2;
        case 2: return -v;
        default: return -v2;
    }
}

static int32_t fcos(uint8_t a) {
    return fsin((uint8_t)(a + 64));
}

static inline int32_t fmul(int32_t a, int32_t b) {
    int32_t lo, hi;
    __asm__ volatile (
        "imull %3"
        : "=a"(lo), "=d"(hi)
        : "a"(a), "rm"(b)
        : "cc"
    );
    uint32_t ulo = (uint32_t)lo;
    uint32_t uhi = (uint32_t)hi;
    uint32_t r = (ulo >> FIX_SHIFT) | (uhi << (32 - FIX_SHIFT));
    return (int32_t)r;
}

static void delay_spin(uint32_t iters) {
    for (uint32_t i = 0; i < iters; i++) {
        __asm__ volatile ("pause");
    }
}

static void screen_fill(char ch, uint8_t attr) {
    uint16_t cols = vga_cols();
    uint16_t rows = vga_rows();
    for (uint16_t y = 0; y < rows; y++) {
        for (uint16_t x = 0; x < cols; x++) {
            vga_write_cell(y, x, ch, attr);
        }
    }
}

static void frame_copy_to_screen(const uint16_t* frame, uint16_t cols, uint16_t rows) {
    for (uint16_t y = 0; y < rows; y++) {
        for (uint16_t x = 0; x < cols; x++) {
            uint16_t cell = frame[y * cols + x];
            vga_write_cell(y, x, (char)(cell & 0xFF), (uint8_t)((cell >> 8) & 0xFF));
        }
    }
}

static void frame_copy_to_screen_graphics(const uint8_t* lumframe, uint16_t cols, uint16_t rows) {
    uint16_t pixel_w = vga_pixel_width();
    uint16_t pixel_h = vga_pixel_height();
    uint16_t cell_w = cols ? (uint16_t)(pixel_w / cols) : 0;
    uint16_t cell_h = rows ? (uint16_t)(pixel_h / rows) : 0;

    if (cell_w == 0 || cell_h == 0) return;

    for (uint16_t y = 0; y < rows; y++) {
        for (uint16_t x = 0; x < cols; x++) {
            uint8_t lum = lumframe[y * cols + x];
            uint32_t shade = 0;

            if (lum != 0) {
                uint32_t c = 18u + (uint32_t)lum * 18u;
                if (c > 255u) c = 255u;
                shade = (c << 16) | ((c + 12u > 255u ? 255u : c + 12u) << 8) | c;
            }

            vga_fill_rect((uint16_t)(x * cell_w), (uint16_t)(y * cell_h), cell_w, cell_h, shade);
        }
    }
}

static void frame_copy_to_screen_dots(const uint8_t* lumframe, uint16_t cols, uint16_t rows, int scanlines) {
    uint16_t pixel_w = vga_pixel_width();
    uint16_t pixel_h = vga_pixel_height();
    uint16_t cell_w = cols ? (uint16_t)(pixel_w / cols) : 0;
    uint16_t cell_h = rows ? (uint16_t)(pixel_h / rows) : 0;

    if (cell_w == 0 || cell_h == 0) return;

    for (uint16_t y = 0; y < rows; y++) {
        for (uint16_t x = 0; x < cols; x++) {
            uint8_t lum = lumframe[y * cols + x];
            uint16_t px = (uint16_t)(x * cell_w);
            uint16_t py = (uint16_t)(y * cell_h);

            vga_fill_rect(px, py, cell_w, cell_h, 0x000000);

            if (lum == 0) continue;

            uint16_t dot_w = (uint16_t)(cell_w > 2 ? (scanlines ? cell_w : cell_w - 1u) : 1u);
            uint16_t dot_h = (uint16_t)(scanlines ? (cell_h > 2 ? cell_h / 2u : 1u)
                                                  : (cell_h > 2 ? cell_h - 1u : 1u));
            uint16_t ox = (uint16_t)((cell_w - dot_w) / 2u);
            uint16_t oy = (uint16_t)((cell_h - dot_h) / 2u);
            uint32_t c = 28u + (uint32_t)lum * 16u;
            if (c > 255u) c = 255u;

            vga_fill_rect((uint16_t)(px + ox), (uint16_t)(py + oy), dot_w, dot_h,
                          (c << 16) | (c << 8) | c);
        }
    }
}

static void frame_copy_to_screen_braille(const uint8_t* lumframe, uint16_t cols, uint16_t rows) {
    uint16_t pixel_w = vga_pixel_width();
    uint16_t pixel_h = vga_pixel_height();
    uint16_t cell_cols = (uint16_t)((cols + 1u) / 2u);
    uint16_t cell_rows = (uint16_t)((rows + 3u) / 4u);
    uint16_t cell_w = cell_cols ? (uint16_t)(pixel_w / cell_cols) : 0;
    uint16_t cell_h = cell_rows ? (uint16_t)(pixel_h / cell_rows) : 0;

    if (cell_w == 0 || cell_h == 0) return;

    for (uint16_t cy = 0; cy < cell_rows; cy++) {
        for (uint16_t cx = 0; cx < cell_cols; cx++) {
            uint16_t px = (uint16_t)(cx * cell_w);
            uint16_t py = (uint16_t)(cy * cell_h);
            uint16_t dot_w = (uint16_t)(cell_w / 3u ? cell_w / 3u : 1u);
            uint16_t dot_h = (uint16_t)(cell_h / 5u ? cell_h / 5u : 1u);
            uint16_t x_gap = (uint16_t)((cell_w > (uint16_t)(dot_w * 2u))
                                        ? (cell_w - (uint16_t)(dot_w * 2u)) / 3u
                                        : 1u);
            uint16_t y_gap = (uint16_t)((cell_h > (uint16_t)(dot_h * 4u))
                                        ? (cell_h - (uint16_t)(dot_h * 4u)) / 5u
                                        : 1u);

            vga_fill_rect(px, py, cell_w, cell_h, 0x000000);

            for (uint16_t sy = 0; sy < 4; sy++) {
                for (uint16_t sx = 0; sx < 2; sx++) {
                    uint16_t src_x = (uint16_t)(cx * 2u + sx);
                    uint16_t src_y = (uint16_t)(cy * 4u + sy);
                    uint8_t lum;
                    uint32_t c;

                    if (src_x >= cols || src_y >= rows) continue;

                    lum = lumframe[src_y * cols + src_x];
                    if (lum == 0) continue;

                    c = 84u + (uint32_t)lum * 18u;
                    if (c > 255u) c = 255u;

                    vga_fill_rect((uint16_t)(px + x_gap + sx * (x_gap + dot_w)),
                                  (uint16_t)(py + y_gap + sy * (y_gap + dot_h)),
                                  dot_w, dot_h,
                                  (c << 16) | (c << 8) | c);
                }
            }
        }
    }
}

static void smooth_lumframe(uint8_t* dst, const uint8_t* src, uint16_t w, uint16_t h) {
    for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
        dst[i] = src[i];
    }

    for (uint16_t y = 1; y + 1 < h; y++) {
        for (uint16_t x = 1; x + 1 < w; x++) {
            uint32_t idx = (uint32_t)y * w + x;
            if (src[idx] != 0) continue;

            uint32_t sum = 0;
            uint32_t count = 0;

            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    uint8_t v = src[(uint32_t)(y + dy) * w + (uint32_t)(x + dx)];
                    if (v == 0) continue;
                    sum += v;
                    count++;
                }
            }

            if (count >= 5) {
                uint8_t fill = (uint8_t)(sum / count);
                dst[idx] = fill > 1 ? (uint8_t)(fill - 1) : fill;
            }
        }
    }
}

static void frame_copy_to_screen_chars(const uint16_t* frame, uint16_t cols, uint16_t rows) {
    uint16_t max_cols = vga_cols();
    uint16_t max_rows = vga_rows();
    uint16_t start_x = max_cols > cols ? (uint16_t)((max_cols - cols) / 2u) : 0;
    uint16_t start_y = max_rows > rows ? (uint16_t)((max_rows - rows) / 2u) : 0;

    if (!g_char_prev_init) {
        for (uint32_t i = 0; i < FRAME_CELLS; i++) {
            g_char_prev[i] = 0xFFFFu;
        }
        g_char_prev_init = 1;
    }

    for (uint16_t y = 0; y < rows && (uint16_t)(start_y + y) < max_rows && y < MAX_SCREEN_H; y++) {
        for (uint16_t x = 0; x < cols && (uint16_t)(start_x + x) < max_cols && x < MAX_SCREEN_W; x++) {
            uint16_t cell = frame[y * cols + x];
            if (g_char_prev[y * MAX_SCREEN_W + x] == cell) continue;
            g_char_prev[y * MAX_SCREEN_W + x] = cell;
            vga_write_cell((uint16_t)(start_y + y), (uint16_t)(start_x + x),
                           (char)(cell & 0xFF), (uint8_t)((cell >> 8) & 0xFF));
        }
    }
}

static void frame_copy_to_screen_chars_reset(void) {
    for (uint32_t i = 0; i < FRAME_CELLS; i++) {
        g_char_prev[i] = 0xFFFFu;
    }
    g_char_prev_init = 1;
}

static void donut_render_frame(uint8_t A, uint8_t B, uint16_t* frame, uint8_t* lumframe,
                               uint16_t screen_w, uint16_t screen_h, int title_bar) {
    static uint16_t zbuf[GFX_FRAME_CELLS];
    uint16_t render_h = title_bar && screen_h > 3 ? (uint16_t)(screen_h - 3u) : screen_h;
    uint16_t start_row = title_bar && screen_h > render_h ? 1u : 0u;

    for (uint32_t i = 0; i < (uint32_t)screen_w * screen_h; i++) {
        frame[i] = (uint16_t)(0x0700 | ' ');
        zbuf[i] = 0;
        lumframe[i] = 0;
    }

    int32_t sinA = fsin(A), cosA = fcos(A);
    int32_t sinB = fsin(B), cosB = fcos(B);

    const int cx = screen_w / 2;
    const int cy = start_row + (render_h / 2);

    for (int t = 0; t < 256; t += THETA_STEP) {
        uint8_t theta = (uint8_t)t;
        int32_t sint = fsin(theta), cost = fcos(theta);

        int32_t circlex = (2 * FIX_ONE) + cost;
        int32_t circley = sint;

        for (int p = 0; p < 256; p += PHI_STEP) {
            uint8_t phi = (uint8_t)p;
            int32_t sinp = fsin(phi), cosp = fcos(phi);

            int32_t x0 = fmul(circlex, cosp);
            int32_t y0 = circley;
            int32_t z0 = fmul(circlex, sinp);

            int32_t y1 = fmul(y0, cosA) - fmul(z0, sinA);
            int32_t z1 = fmul(y0, sinA) + fmul(z0, cosA);
            int32_t x1 = x0;

            int32_t x2 = fmul(x1, cosB) - fmul(y1, sinB);
            int32_t y2 = fmul(x1, sinB) + fmul(y1, cosB);
            int32_t z2 = z1;

            int32_t z = z2 + (K2_Z * FIX_ONE);
            if (z <= (FIX_ONE / 2)) continue;

            int32_t ooz = (1 << (FIX_SHIFT + 14)) / z;
            if (ooz <= 0) continue;

            int32_t px = (x2 * ooz) >> (FIX_SHIFT + 7);
            int32_t py = (y2 * ooz) >> (FIX_SHIFT + 8);

            px = fmul(px, ZOOM_Q14);
            py = fmul(py, ZOOM_Q14);

            int xp = cx + (int)(px * (int32_t)(screen_w * 3 / 8) / 32);
            int yp = cy - (int)(py * (int32_t)(render_h * 3 / 8) / 32);

            if (xp < 0 || xp >= screen_w) continue;
            if (yp < start_row || yp >= (start_row + render_h)) continue;

            int idx = yp * screen_w + xp;

            int32_t L =
                fmul(cosp, fmul(cost, sinB)) -
                fmul(cosA, fmul(cost, sinp)) -
                fmul(sinA, sint) +
                fmul(cosB, (fmul(cosA, sint) - fmul(cost, fmul(sinA, sinp))));

            L += (FIX_ONE / 2);
            if (L <= 0) continue;

            uint16_t zval = (uint16_t)(ooz >> 4);
            if (zval > zbuf[idx]) {
                zbuf[idx] = zval;

                int lum = (int)(L >> 10);
                if (lum < 0) lum = 0;
                if (lum >= SHADE_N) lum = SHADE_N - 1;

                if (lum == SHADE_N - 1) {
                    if (zval < 200) {
                    } else {
                        lum = SHADE_N - 2;
                    }
                }

                frame[idx] = (uint16_t)(0x0700 | (uint8_t)shades[lum]);
                lumframe[idx] = (uint8_t)(lum + 1);
            }
        }
    }

    if (title_bar && screen_w >= 5) {
        frame[0] = (uint16_t)(0x0F00 | 'D');
        frame[1] = (uint16_t)(0x0F00 | 'o');
        frame[2] = (uint16_t)(0x0F00 | 'n');
        frame[3] = (uint16_t)(0x0F00 | 'u');
        frame[4] = (uint16_t)(0x0F00 | 't');
    }
}

void donut_run(uint32_t duration_ticks, donut_mode_t mode) {
    (void)duration_ticks;

    int frame_index = 0;
    static uint16_t frame[FRAME_CELLS];
    static uint8_t lumframe[FRAME_CELLS];
    static uint16_t gfx_frame[GFX_FRAME_CELLS];
    static uint8_t gfx_lumframe[GFX_FRAME_CELLS];
    static uint8_t gfx_smooth_lumframe[GFX_FRAME_CELLS];
    uint16_t screen_w = vga_cols();
    uint16_t screen_h = vga_rows();
    int graphics_mode = vga_is_framebuffer() && mode != DONUT_MODE_CHARS;
    uint16_t gfx_w = 0;
    uint16_t gfx_h = 0;
    int framebuffer = vga_is_framebuffer();
    int char_framebuffer_mode = framebuffer && mode == DONUT_MODE_CHARS;

    if (screen_w > MAX_SCREEN_W) screen_w = MAX_SCREEN_W;
    if (screen_h > MAX_SCREEN_H) screen_h = MAX_SCREEN_H;

    if (graphics_mode) {
        gfx_w = (uint16_t)(vga_pixel_width() / 4u);
        gfx_h = (uint16_t)(vga_pixel_height() / 4u);
        if (gfx_w > MAX_GFX_W) gfx_w = MAX_GFX_W;
        if (gfx_h > MAX_GFX_H) gfx_h = MAX_GFX_H;
        if (gfx_w < 48) gfx_w = 48;
        if (gfx_h < 36) gfx_h = 36;
    }

    console_clear_cancel();
    if (char_framebuffer_mode) {
        vga_desktop_enable(0);
        frame_copy_to_screen_chars_reset();
    }
    if (!graphics_mode) {
        screen_fill(' ', 0x07);
    }

    while (!console_cancel_requested()) {
        if (graphics_mode) {
            donut_render_frame((uint8_t)(frame_index * 4), (uint8_t)(frame_index * 2),
                               gfx_frame, gfx_lumframe, gfx_w, gfx_h, 0);
            vga_batch_begin();
            if (mode == DONUT_MODE_DOTS) {
                frame_copy_to_screen_braille(gfx_lumframe, gfx_w, gfx_h);
            } else if (mode == DONUT_MODE_SCAN) {
                frame_copy_to_screen_dots(gfx_lumframe, gfx_w, gfx_h, 1);
            } else {
                smooth_lumframe(gfx_smooth_lumframe, gfx_lumframe, gfx_w, gfx_h);
                frame_copy_to_screen_graphics(gfx_smooth_lumframe, gfx_w, gfx_h);
            }
            vga_batch_end();
        } else {
            donut_render_frame((uint8_t)(frame_index * 4), (uint8_t)(frame_index * 2),
                               frame, lumframe, screen_w, screen_h, 1);
            frame_copy_to_screen_chars(frame, screen_w, screen_h);
        }
        frame_index++;
        if (frame_index >= FRAME_COUNT) {
            frame_index = 0;
        }
        delay_spin(1500000);
    }

    console_clear_cancel();
    screen_fill(' ', 0x07);
    if (char_framebuffer_mode) {
        vga_desktop_enable(1);
    }
}
