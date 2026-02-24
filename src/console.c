#include "console.h"
#include "vga.h"

#define LINE_MAX 128
#define HIST_MAX 16

static char linebuf[LINE_MAX];
static size_t len = 0;
static size_t cur = 0;
static volatile int line_ready = 0;

static uint16_t prompt_row = 0;
static uint16_t prompt_col = 0;

static char history[HIST_MAX][LINE_MAX];
static size_t hist_count = 0;
static size_t hist_pos = 0;

static int sw_cursor_on = 0;
static uint16_t sw_row = 0, sw_col = 0;
static char sw_saved_ch = ' ';
static uint8_t sw_saved_attr = 0x07;

static void set_cursor_to_prompt_plus(size_t offset);
static void redraw_line(void);
static void clear_line_buffer(void);
static void load_from_history(size_t index);

static uint8_t invert_attr(uint8_t a);
static void sw_cursor_hide(void);
static void sw_cursor_show_at(uint16_t row, uint16_t col);
static void place_sw_cursor(void);

static size_t last_drawn_len = 0;

static volatile int cancel_requested = 0;
static volatile int waiting_for_line = 0;

static void history_push(const char* s) {
    if (!s || s[0] == '\0') return;

    size_t slot = hist_count < HIST_MAX ? hist_count : (hist_count % HIST_MAX);

    size_t i = 0;
    while (i < LINE_MAX - 1 && s[i]) {
        history[slot][i] = s[i];
        i++;
    }
    history[slot][i] = '\0';

    hist_count++;
    hist_pos = hist_count;
}

void console_init(void) {
    len = 0;
    cur = 0;
    line_ready = 0;
    hist_count = 0;
    hist_pos = 0;
    linebuf[0] = '\0';
    prompt_row = 0;
    prompt_col = 0;
    sw_cursor_on = 0;
}

void console_request_cancel(void) {
    cancel_requested = 1;

    len = 0;
    cur = 0;
    linebuf[0] = '\0';
    last_drawn_len = 0;

    vga_puts("^C\n");

    if (waiting_for_line) {
        line_ready = 1;
        sw_cursor_on = 0;
    }
}

int console_cancel_requested(void) {
    return cancel_requested != 0;
}

void console_clear_cancel(void) {
    cancel_requested = 0;
}

void console_begin_input(void) {
    sw_cursor_hide();
    sw_cursor_on = 0;


    vga_get_cursor(&prompt_row, &prompt_col);
    sw_cursor_on = 0;
    set_cursor_to_prompt_plus(cur);
    place_sw_cursor();
}

static void set_cursor_to_prompt_plus(size_t offset) {
    uint16_t row = prompt_row;
    uint16_t col = (uint16_t)(prompt_col + offset);

    row += (uint16_t)(col / 80);
    col = (uint16_t)(col % 80);

    vga_set_cursor(row, col);
}

static void place_sw_cursor(void) {
    uint16_t row = prompt_row;
    uint16_t col = (uint16_t)(prompt_col + cur);

    row += (uint16_t)(col / 80);
    col = (uint16_t)(col % 80);

    sw_cursor_show_at(row, col);
}

static void redraw_line(void) {
    sw_cursor_hide();

    set_cursor_to_prompt_plus(0);

    for (size_t i = 0; i < len; i++) {
        vga_putc(linebuf[i]);
    }

    size_t extra = 1;
    if (last_drawn_len > len) extra += (last_drawn_len - len);

    for (size_t i = 0; i < extra; i++) {
        vga_putc(' ');
    }

    last_drawn_len = len;

    set_cursor_to_prompt_plus(cur);
    place_sw_cursor();
}

static void clear_line_buffer(void) {
    len = 0;
    cur = 0;
    linebuf[0] = '\0';
    redraw_line();
}


static void load_from_history(size_t index) {
    len = 0;
    cur = 0;

    size_t i = 0;
    while (i < LINE_MAX - 1) {
        char c = history[index][i];
        if (c == '\0') break;
        linebuf[i] = c;
        i++;
    }

    linebuf[i] = '\0';
    len = i;
    cur = i;

    redraw_line();
}

void console_on_key(char c) {
    sw_cursor_hide();

    if (line_ready) return;

if (c == '\n') {
    vga_putc('\n');
    linebuf[len] = '\0';
    history_push(linebuf);
    line_ready = 1;
    last_drawn_len = 0;
    return;
}


    if (c == '\b') {
        if (cur > 0) {
            for (size_t i = cur - 1; i + 1 < len; i++) {
                linebuf[i] = linebuf[i + 1];
            }
            len--;
            cur--;
            linebuf[len] = '\0';
            redraw_line();
        } else {
            place_sw_cursor();
        }
        return;
    }

    if (c == '\t') {
        place_sw_cursor();
        return;
    }

    if (c >= 32 && c <= 126) {
        if (len < LINE_MAX - 1) {
            for (size_t i = len; i > cur; i--) {
                linebuf[i] = linebuf[i - 1];
            }
            linebuf[cur] = c;
            len++;
            cur++;
            linebuf[len] = '\0';
            redraw_line();
        } else {
            place_sw_cursor();
        }
        return;
    }

    place_sw_cursor();
}

void console_cursor_left(void) {
    if (line_ready) return;
    if (cur > 0) {
        sw_cursor_hide();
        cur--;
        set_cursor_to_prompt_plus(cur);
        place_sw_cursor();
    }
}

void console_cursor_right(void) {
    if (line_ready) return;
    if (cur < len) {
        sw_cursor_hide();
        cur++;
        set_cursor_to_prompt_plus(cur);
        place_sw_cursor();
    }
}

void console_history_up(void) {

    if (line_ready) return;
    if (hist_count == 0) return;

    size_t base = hist_count > HIST_MAX ? (hist_count - HIST_MAX) : 0;

    if (hist_pos < hist_count) {

        const char* current_hist = history[hist_pos % HIST_MAX];

        int equal = 1;
        size_t i = 0;
        while (linebuf[i] || current_hist[i]) {
            if (linebuf[i] != current_hist[i]) {
                equal = 0;
                break;
            }
            i++;
        }

        if (!equal) {
            hist_pos = hist_count;
        }
    }

    if (hist_pos == hist_count) {
        hist_pos = hist_count - 1;
    }
    else {
        if (hist_pos == 0) return;
        if (hist_pos <= base) return;
        hist_pos--;
    }

    sw_cursor_hide();
    load_from_history(hist_pos % HIST_MAX);
}

void console_history_down(void) {
    if (line_ready) return;
    if (hist_count == 0) return;

    size_t base = hist_count > HIST_MAX ? (hist_count - HIST_MAX) : 0;

    sw_cursor_hide();

    if (hist_pos >= hist_count) {
        clear_line_buffer();
        return;
    }

    hist_pos++;

    if (hist_pos >= hist_count) {
        clear_line_buffer();
        hist_pos = hist_count;
        return;
    }

    if (hist_pos < base) hist_pos = base;

    load_from_history(hist_pos % HIST_MAX);
}

void console_readline(char* out, size_t max) {
    sw_cursor_hide();

    if (!out || max == 0) return;

waiting_for_line = 1;
while (!line_ready) {
    __asm__ volatile ("hlt");
}
waiting_for_line = 0;

    size_t i = 0;
    while (i + 1 < max && i < len) {
        out[i] = linebuf[i];
        i++;
    }
    out[i] = '\0';

    len = 0;
    cur = 0;
    linebuf[0] = '\0';
    line_ready = 0;
    hist_pos = hist_count;
    sw_cursor_on = 0;
}

static uint8_t invert_attr(uint8_t a) {
    uint8_t fg = a & 0x0F;
    uint8_t bg = (a >> 4) & 0x0F;
    return (uint8_t)((fg << 4) | bg);
}

static void sw_cursor_hide(void) {
    if (!sw_cursor_on) return;
    vga_write_cell(sw_row, sw_col, sw_saved_ch, sw_saved_attr);
    sw_cursor_on = 0;
}

static void sw_cursor_show_at(uint16_t row, uint16_t col) {
    sw_cursor_hide();

    sw_row = row;
    sw_col = col;

    vga_read_cell(row, col, &sw_saved_ch, &sw_saved_attr);

    uint8_t inv = invert_attr(sw_saved_attr);

    if (sw_saved_ch == 0 || sw_saved_ch == ' ') {
        vga_write_cell(row, col, (char)0xDB, inv);
    } else {
        vga_write_cell(row, col, sw_saved_ch, inv);
    }

    sw_cursor_on = 1;
}
