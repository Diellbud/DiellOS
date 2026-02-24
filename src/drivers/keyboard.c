#include "keyboard.h"
#include "../arch/i386/io.h"
#include "../arch/i386/irq.h"
#include "../console.h"

static volatile int shift_down = 0;
static volatile int ctrl_down = 0;
static volatile int e0_prefix = 0;

static const char keymap[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char keymap_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void keyboard_callback(struct regs *r)
{
    (void)r;
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) {
        e0_prefix = 1;
        return;
    }

    if (sc & 0x80) {
        uint8_t make = sc & 0x7F;

        if (make == 0x2A || make == 0x36) shift_down = 0;
        if (make == 0x1D) ctrl_down = 0;

        e0_prefix = 0;
        return;
    }

    if (sc == 0x2A || sc == 0x36) {
        shift_down = 1;
        return;
    }

    if (sc == 0x1D) {
        ctrl_down = 1;
        return;
    }

    if (e0_prefix) {
        e0_prefix = 0;

        if (sc == 0x48) { console_history_up(); return; }
        if (sc == 0x50) { console_history_down(); return; }
        if (sc == 0x4B) { console_cursor_left(); return; }
        if (sc == 0x4D) { console_cursor_right(); return; }
    }

    if (ctrl_down && sc == 0x2E) {
        console_request_cancel();
        return;
    }

    char c = shift_down ? keymap_shift[sc] : keymap[sc];
    if (c) {
        console_on_key(c);
    }
}

void keyboard_init(void)
{
    irq_register_handler(1, keyboard_callback);
}