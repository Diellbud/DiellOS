#include "mouse.h"

#include "../arch/i386/io.h"
#include "../arch/i386/irq.h"
#include "../arch/i386/pic.h"
#include "../vga.h"

#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64
#define PS2_DATA_PORT    0x60

static volatile uint8_t packet[3];
static volatile uint8_t packet_index = 0;
static volatile uint16_t cursor_x = 0;
static volatile uint16_t cursor_y = 0;
static volatile uint8_t buttons = 0;

static void ps2_wait_write(void) {
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((inb(PS2_STATUS_PORT) & 0x02u) == 0) return;
    }
}

static void ps2_wait_read(void) {
    for (uint32_t i = 0; i < 100000u; i++) {
        if (inb(PS2_STATUS_PORT) & 0x01u) return;
    }
}

static void mouse_write(uint8_t value) {
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0xD4);
    ps2_wait_write();
    outb(PS2_DATA_PORT, value);
}

static uint8_t mouse_read(void) {
    ps2_wait_read();
    return inb(PS2_DATA_PORT);
}

static void mouse_callback(struct regs* r) {
    (void)r;

    uint8_t data = inb(PS2_DATA_PORT);

    if (packet_index == 0 && (data & 0x08u) == 0) {
        return;
    }

    packet[packet_index++] = data;
    if (packet_index < 3) return;
    packet_index = 0;

    int dx = (packet[0] & 0x10u) ? (int)packet[1] - 256 : (int)packet[1];
    int dy = (packet[0] & 0x20u) ? (int)packet[2] - 256 : (int)packet[2];

    int new_x = (int)cursor_x + dx;
    int new_y = (int)cursor_y - dy;

    uint16_t max_x = vga_is_framebuffer() ? vga_pixel_width() : (uint16_t)(vga_cols() * 8u);
    uint16_t max_y = vga_is_framebuffer() ? vga_pixel_height() : (uint16_t)(vga_rows() * 16u);

    if (max_x == 0) max_x = 1;
    if (max_y == 0) max_y = 1;

    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (new_x >= max_x) new_x = max_x - 1;
    if (new_y >= max_y) new_y = max_y - 1;

    cursor_x = (uint16_t)new_x;
    cursor_y = (uint16_t)new_y;
    buttons = (uint8_t)(packet[0] & 0x07u);

    vga_mouse_set(cursor_x, cursor_y, 1);
}

void mouse_init(void) {
    uint8_t status;

    irq_register_handler(12, mouse_callback);

    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0xA8);

    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0x20);
    status = mouse_read();
    status |= 0x02u;
    status &= (uint8_t)~0x20u;

    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0x60);
    ps2_wait_write();
    outb(PS2_DATA_PORT, status);

    mouse_write(0xF6);
    (void)mouse_read();
    mouse_write(0xF4);
    (void)mouse_read();

    cursor_x = vga_is_framebuffer() ? (uint16_t)(vga_pixel_width() / 2u) : 0;
    cursor_y = vga_is_framebuffer() ? (uint16_t)(vga_pixel_height() / 2u) : 0;
    vga_mouse_set(cursor_x, cursor_y, vga_is_framebuffer());

    pic_clear_mask(12);
    pic_clear_mask(2);
}

uint16_t mouse_x(void) {
    return cursor_x;
}

uint16_t mouse_y(void) {
    return cursor_y;
}

uint8_t mouse_buttons(void) {
    return buttons;
}
