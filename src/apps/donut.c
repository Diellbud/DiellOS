#include "donut.h"
#include <stdint.h>
#include "../console.h"

#define VGA_BASE     ((volatile uint16_t*)0xB8000)
#define SCREEN_W     80
#define SCREEN_H     25
#define RENDER_W     80
#define RENDER_H     22
#define START_ROW    1

#define FIX_SHIFT    14
#define FIX_ONE      (1 << FIX_SHIFT)

#define THETA_STEP   7
#define PHI_STEP     3

#define K2_Z         5
#define K1_X         30
#define K1_Y         24

#define ZOOM_Q14     4681

static const char shades[] = ".,-~:;=!*#$@";
#define SHADE_N ((int)(sizeof(shades) - 1))

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

static void vga_fill(char ch, uint8_t attr) {
    uint16_t cell = ((uint16_t)attr << 8) | (uint16_t)(uint8_t)ch;
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        VGA_BASE[i] = cell;
    }
}

static void donut_frame(uint8_t A, uint8_t B) {
    static char out[SCREEN_W * SCREEN_H];
    static uint16_t zbuf[SCREEN_W * SCREEN_H];

    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        out[i] = ' ';
        zbuf[i] = 0;
    }

    int32_t sinA = fsin(A), cosA = fcos(A);
    int32_t sinB = fsin(B), cosB = fcos(B);

    const int cx = SCREEN_W / 2;
    const int cy = START_ROW + (RENDER_H / 2);

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

            int xp = cx + (int)(px * K1_X / 32);
            int yp = cy - (int)(py * K1_Y / 32);

            if (xp < 0 || xp >= SCREEN_W) continue;
            if (yp < START_ROW || yp >= (START_ROW + RENDER_H)) continue;

            int idx = yp * SCREEN_W + xp;

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

                out[idx] = shades[lum];
            }
        }
    }

    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        VGA_BASE[i] = (uint16_t)(0x0700 | (uint8_t)out[i]);
    }

    VGA_BASE[0 * SCREEN_W + 0] = (uint16_t)(0x0F00 | 'D');
    VGA_BASE[0 * SCREEN_W + 1] = (uint16_t)(0x0F00 | 'o');
    VGA_BASE[0 * SCREEN_W + 2] = (uint16_t)(0x0F00 | 'n');
    VGA_BASE[0 * SCREEN_W + 3] = (uint16_t)(0x0F00 | 'u');
    VGA_BASE[0 * SCREEN_W + 4] = (uint16_t)(0x0F00 | 't');
}

void donut_run(uint32_t duration_ticks) {
    (void)duration_ticks;

    uint8_t A = 0, B = 0;

    console_clear_cancel();
    vga_fill(' ', 0x07);

    while (!console_cancel_requested()) {
        donut_frame(A, B);

        A = (uint8_t)(A + 4);
        B = (uint8_t)(B + 2);

        delay_spin(1500000);
    }

    console_clear_cancel();
    vga_fill(' ', 0x07);
}