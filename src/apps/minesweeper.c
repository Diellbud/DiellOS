#include "minesweeper.h"

#include <stddef.h>
#include <stdint.h>

#include "../console.h"
#include "../debug/print.h"
#include "../drivers/timer.h"
#include "../lib/string.h"
#include "../vga.h"

#define MS_MAX_W 30
#define MS_MAX_H 16

#define GRID_TOP 2
#define GRID_LEFT 5

#define CELL_MINE      0x01
#define CELL_REVEALED  0x02
#define CELL_FLAGGED   0x04
#define CELL_QUESTION  0x08

typedef enum {
    MS_DIFF_EASY = 0,
    MS_DIFF_INTERMEDIATE = 1,
    MS_DIFF_HARD = 2,
    MS_DIFF_CUSTOM = 3
} ms_difficulty_t;

static uint8_t g_cells[MS_MAX_H][MS_MAX_W];
static int g_w = 9;
static int g_h = 9;
static int g_mines = 10;
static ms_difficulty_t g_difficulty = MS_DIFF_EASY;

static int g_game_over = 0;
static int g_won = 0;
static int g_first_open = 1;
static uint32_t g_rng_state = 0xC0FFEEu;

static int g_cursor_x = 0;
static int g_cursor_y = 0;

static uint32_t g_start_seconds = 0;
static uint32_t g_elapsed_on_end = 0;
static int g_timer_started = 0;

static uint32_t g_moves = 0;
static uint32_t g_open_actions = 0;
static uint32_t g_safe_open_actions = 0;
static uint32_t g_flag_place_actions = 0;
static uint32_t g_flags_used = 0;

static int has_mine(int x, int y);
static int is_revealed(int x, int y);
static int is_flagged(int x, int y);
static int is_question(int x, int y);
static void reveal_flood(int sx, int sy);
static void play_loss_animation(void);
static void play_win_animation(void);

static int starts_with(const char* s, const char* pfx) {
    while (*pfx) {
        if (*s++ != *pfx++) return 0;
    }
    return 1;
}

static const char* difficulty_name(ms_difficulty_t d) {
    if (d == MS_DIFF_EASY) return "easy";
    if (d == MS_DIFF_INTERMEDIATE) return "intermediate";
    if (d == MS_DIFF_HARD) return "hard";
    return "custom";
}

static void wait_ticks(uint32_t dt) {
    uint32_t start = timer_ticks();
    while ((timer_ticks() - start) < dt) {
        __asm__ volatile ("hlt");
    }
}

static void start_timer_if_needed(void) {
    if (g_timer_started) return;
    g_timer_started = 1;
    g_start_seconds = timer_seconds();
}

static uint32_t elapsed_seconds_now(void) {
    if (!g_timer_started) return 0;
    if (g_game_over) return g_elapsed_on_end;
    return timer_seconds() - g_start_seconds;
}

static void finish_game(int won) {
    g_game_over = 1;
    g_won = won;
    if (g_timer_started) {
        g_elapsed_on_end = timer_seconds() - g_start_seconds;
    } else {
        g_elapsed_on_end = 0;
    }
}

static void draw_banner(const char* msg, uint8_t attr) {
    int len = 0;
    while (msg[len]) len++;
    {
        const int row = GRID_TOP + g_h + 1;
        const int col = GRID_LEFT + ((g_w * 2) / 2) - (len / 2);
        for (int i = 0; i < len; i++) {
            vga_write_cell((uint16_t)row, (uint16_t)(col + i), msg[i], attr);
        }
    }
}

static void draw_end_grid_frame(int frame, int won) {
    uint8_t mine_attr = won ? ((frame & 1) ? 0x2F : 0xA0) : ((frame & 1) ? 0x0F : 0xF0);
    uint8_t bg_attr = won ? ((frame & 1) ? 0x20 : 0x02) : ((frame & 1) ? 0x70 : 0x07);
    char empty_ch = won ? ((frame & 1) ? '+' : '.') : ((frame & 1) ? '.' : ' ');

    for (int y = 0; y < g_h; y++) {
        for (int x = 0; x < g_w; x++) {
            int col = GRID_LEFT + (x * 2);
            if (has_mine(x, y)) {
                vga_write_cell((uint16_t)(GRID_TOP + y), (uint16_t)col, '*', mine_attr);
            } else {
                vga_write_cell((uint16_t)(GRID_TOP + y), (uint16_t)col, empty_ch, bg_attr);
            }
            vga_write_cell((uint16_t)(GRID_TOP + y), (uint16_t)(col + 1), ' ', bg_attr);
        }
    }
}

static void play_loss_animation(void) {
    for (int frame = 0; frame < 6; frame++) {
        uint8_t attr = (frame & 1) ? 0x0F : 0xF0;
        draw_end_grid_frame(frame, 0);
        draw_banner(" BOOM ", attr);
        wait_ticks(7);
    }
}

static void play_win_animation(void) {
    for (int frame = 0; frame < 6; frame++) {
        uint8_t attr = (frame & 1) ? 0x2F : 0xA0;
        draw_end_grid_frame(frame, 1);
        draw_banner(" CLEAR ", attr);
        wait_ticks(7);
    }
}

static uint32_t ms_rand_u32(void) {
    g_rng_state = g_rng_state * 1664525u + 1013904223u;
    return g_rng_state;
}

static void ms_seed_rng(void) {
    g_rng_state ^= timer_ticks() + 0x9E3779B9u;
    if (g_rng_state == 0) g_rng_state = 0x12345678u;
}

static int in_bounds(int x, int y) {
    return (x >= 0 && x < g_w && y >= 0 && y < g_h);
}

static int has_mine(int x, int y) {
    return (g_cells[y][x] & CELL_MINE) != 0;
}

static int is_revealed(int x, int y) {
    return (g_cells[y][x] & CELL_REVEALED) != 0;
}

static int is_flagged(int x, int y) {
    return (g_cells[y][x] & CELL_FLAGGED) != 0;
}

static int is_question(int x, int y) {
    return (g_cells[y][x] & CELL_QUESTION) != 0;
}

static int count_adjacent_mines(int x, int y) {
    int count = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            {
                int nx = x + dx;
                int ny = y + dy;
                if (in_bounds(nx, ny) && has_mine(nx, ny)) count++;
            }
        }
    }
    return count;
}

static int count_adjacent_flags(int x, int y) {
    int count = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            {
                int nx = x + dx;
                int ny = y + dy;
                if (in_bounds(nx, ny) && is_flagged(nx, ny)) count++;
            }
        }
    }
    return count;
}

static int count_adjacent_hidden_unflagged(int x, int y) {
    int count = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            {
                int nx = x + dx;
                int ny = y + dy;
                if (!in_bounds(nx, ny)) continue;
                if (is_revealed(nx, ny) || is_flagged(nx, ny)) continue;
                count++;
            }
        }
    }
    return count;
}

static void reveal_flood(int sx, int sy) {
    int qx[MS_MAX_W * MS_MAX_H];
    int qy[MS_MAX_W * MS_MAX_H];
    int head = 0;
    int tail = 0;

    qx[tail] = sx;
    qy[tail] = sy;
    tail++;

    while (head < tail) {
        int x = qx[head];
        int y = qy[head];
        head++;

        if (!in_bounds(x, y)) continue;
        if (is_revealed(x, y) || is_flagged(x, y)) continue;

        g_cells[y][x] &= (uint8_t)(~CELL_QUESTION);
        g_cells[y][x] |= CELL_REVEALED;

        if (count_adjacent_mines(x, y) != 0) continue;

        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                {
                    int nx = x + dx;
                    int ny = y + dy;
                    if (!in_bounds(nx, ny)) continue;
                    if (is_revealed(nx, ny) || is_flagged(nx, ny)) continue;
                    if (tail < (MS_MAX_W * MS_MAX_H)) {
                        qx[tail] = nx;
                        qy[tail] = ny;
                        tail++;
                    }
                }
            }
        }
    }
}

static int check_win(void) {
    int safe_cells = (g_w * g_h) - g_mines;
    int revealed_safe = 0;

    for (int y = 0; y < g_h; y++) {
        for (int x = 0; x < g_w; x++) {
            if (!has_mine(x, y) && is_revealed(x, y)) revealed_safe++;
        }
    }

    return revealed_safe == safe_cells;
}

static void clear_board_cells(void) {
    for (int y = 0; y < g_h; y++) {
        for (int x = 0; x < g_w; x++) {
            g_cells[y][x] = 0;
        }
    }
}

static int excluded_from_first_open(int x, int y, int ox, int oy) {
    int dx = x - ox;
    int dy = y - oy;
    return (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1);
}

static void place_mines_excluding(int ox, int oy) {
    int placed = 0;
    int attempts = 0;
    int max_attempts = g_w * g_h * 30;

    while (placed < g_mines && attempts < max_attempts) {
        int x = (int)(ms_rand_u32() % (uint32_t)g_w);
        int y = (int)(ms_rand_u32() % (uint32_t)g_h);
        attempts++;

        if (has_mine(x, y)) continue;
        if (excluded_from_first_open(x, y, ox, oy)) continue;
        g_cells[y][x] |= CELL_MINE;
        placed++;
    }

    if (placed >= g_mines) return;

    for (int y = 0; y < g_h && placed < g_mines; y++) {
        for (int x = 0; x < g_w && placed < g_mines; x++) {
            if (has_mine(x, y)) continue;
            if (excluded_from_first_open(x, y, ox, oy)) continue;
            g_cells[y][x] |= CELL_MINE;
            placed++;
        }
    }
}

static int parse_u32(const char* s, uint32_t* out) {
    uint32_t v = 0;
    int any = 0;

    while (*s == ' ' || *s == '\t') s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint32_t)(*s - '0');
        s++;
        any = 1;
    }
    while (*s == ' ' || *s == '\t') s++;

    if (!any || *s != '\0') return 0;
    *out = v;
    return 1;
}

static int parse_coord_token(const char* s, int max, int* out) {
    uint32_t v = 0;
    if (!parse_u32(s, &v)) return 0;
    if ((int)v >= max) return 0;
    *out = (int)v;
    return 1;
}

static int split_tokens(char* line, char* argv[], int max_args) {
    int argc = 0;
    char* p = line;

    while (*p && argc < max_args) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        argv[argc++] = p;

        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p == '\0') break;
        *p++ = '\0';
    }

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0') return -1;
    return argc;
}

static void print_two_digits(int v) {
    if (v < 10) {
        vga_putc('0');
        vga_putc((char)('0' + v));
        return;
    }
    kprint_dec((uint32_t)v);
}

static int count_correct_flags(void) {
    int correct = 0;
    for (int y = 0; y < g_h; y++) {
        for (int x = 0; x < g_w; x++) {
            if (is_flagged(x, y) && has_mine(x, y)) correct++;
        }
    }
    return correct;
}

static void print_board(void) {
    int mines_left = g_mines - (int)g_flags_used;
    if (mines_left < 0) mines_left = 0;

    vga_puts("Minesweeper ");
    if (g_game_over) {
        vga_puts(g_won ? "[WIN]" : "[LOSE]");
    } else {
        vga_puts("[PLAY]");
    }
    vga_puts("  diff=");
    vga_puts(difficulty_name(g_difficulty));
    vga_puts("  size=");
    kprint_dec((uint32_t)g_w);
    vga_putc('x');
    kprint_dec((uint32_t)g_h);
    vga_puts("  mines=");
    kprint_dec((uint32_t)g_mines);
    vga_puts("\n");

    vga_puts("Mines left: ");
    kprint_dec((uint32_t)mines_left);
    vga_puts("  Time: ");
    kprint_dec(elapsed_seconds_now());
    vga_puts("s  Moves: ");
    kprint_dec(g_moves);
    vga_putc('\n');

    vga_puts("    ");
    for (int x = 0; x < g_w; x++) {
        print_two_digits(x);
    }
    vga_putc('\n');

    for (int y = 0; y < g_h; y++) {
        print_two_digits(y);
        vga_puts(" | ");

        for (int x = 0; x < g_w; x++) {
            char c = '#';
            if (is_flagged(x, y) && !is_revealed(x, y)) {
                c = 'F';
            } else if (is_question(x, y) && !is_revealed(x, y)) {
                c = '?';
            } else if (is_revealed(x, y) || (g_game_over && has_mine(x, y))) {
                if (has_mine(x, y)) {
                    c = '*';
                } else {
                    int n = count_adjacent_mines(x, y);
                    c = (n == 0) ? '.' : (char)('0' + n);
                }
            }

            if (!g_game_over && x == g_cursor_x && y == g_cursor_y) {
                vga_putc('>');
            } else {
                vga_putc(' ');
            }
            vga_putc(c);
        }
        vga_putc('\n');
    }

    if (g_game_over) {
        int correct_flags = count_correct_flags();
        vga_puts("Result: ");
        vga_puts(g_won ? "cleared" : "hit a mine");
        vga_puts("  Opens: ");
        kprint_dec(g_open_actions);
        vga_puts("  Safe opens: ");
        kprint_dec(g_safe_open_actions);
        vga_puts("  Flags: ");
        kprint_dec(g_flags_used);
        vga_puts("  Correct flags: ");
        kprint_dec((uint32_t)correct_flags);
        vga_putc('\n');
    }

    vga_puts("Commands: open|o [x y], flag|f [x y], hint, new, restart, help, exit\n");
    vga_puts("Cursor: w/a/s/d move, o open at cursor, f cycle mark (#->F->?->#), m mark at cursor\n");
    vga_puts("Difficulty: difficulty <easy|intermediate|hard>, custom <w> <h> <mines>\n");
    vga_puts("Ctrl+C cancels\n");
}

static void place_mines_for_first_open(int x, int y) {
    clear_board_cells();
    place_mines_excluding(x, y);
    g_first_open = 0;
}

static void move_cursor(int dx, int dy) {
    int nx = g_cursor_x + dx;
    int ny = g_cursor_y + dy;
    if (nx < 0) nx = 0;
    if (nx >= g_w) nx = g_w - 1;
    if (ny < 0) ny = 0;
    if (ny >= g_h) ny = g_h - 1;
    g_cursor_x = nx;
    g_cursor_y = ny;
}

static void do_open(int x, int y) {
    if (g_game_over) {
        vga_puts("game over; type 'new', 'restart' or 'exit'\n");
        return;
    }

    if (is_flagged(x, y)) {
        vga_puts("cell is flagged\n");
        return;
    }

    start_timer_if_needed();

    if (is_revealed(x, y)) {
        int need = count_adjacent_mines(x, y);
        int have = count_adjacent_flags(x, y);
        if (need == 0 || have < need) return;

        g_moves++;
        g_open_actions++;

        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                {
                    int nx = x + dx;
                    int ny = y + dy;
                    if (!in_bounds(nx, ny)) continue;
                    if (is_flagged(nx, ny) || is_revealed(nx, ny)) continue;

                    if (has_mine(nx, ny)) {
                        g_cells[ny][nx] |= CELL_REVEALED;
                        finish_game(0);
                        play_loss_animation();
                        return;
                    }
                    reveal_flood(nx, ny);
                }
            }
        }

        g_safe_open_actions++;
        if (check_win()) {
            finish_game(1);
            play_win_animation();
        }
        return;
    }

    if (g_first_open) {
        place_mines_for_first_open(x, y);
    }

    g_moves++;
    g_open_actions++;

    if (has_mine(x, y)) {
        g_cells[y][x] |= CELL_REVEALED;
        finish_game(0);
        play_loss_animation();
        return;
    }

    reveal_flood(x, y);
    g_safe_open_actions++;
    if (check_win()) {
        finish_game(1);
        play_win_animation();
    }
}

static void do_mark(int x, int y) {
    if (g_game_over) {
        vga_puts("game over; type 'new', 'restart' or 'exit'\n");
        return;
    }
    if (is_revealed(x, y)) {
        vga_puts("cell already open\n");
        return;
    }

    start_timer_if_needed();
    g_moves++;

    if (!is_flagged(x, y) && !is_question(x, y)) {
        g_cells[y][x] |= CELL_FLAGGED;
        g_flags_used++;
        g_flag_place_actions++;
    } else if (is_flagged(x, y)) {
        g_cells[y][x] &= (uint8_t)(~CELL_FLAGGED);
        g_cells[y][x] |= CELL_QUESTION;
        if (g_flags_used > 0) g_flags_used--;
    } else {
        g_cells[y][x] &= (uint8_t)(~CELL_QUESTION);
    }
}

static int find_hint(int* out_x, int* out_y, int* out_action) {
    for (int y = 0; y < g_h; y++) {
        for (int x = 0; x < g_w; x++) {
            if (!is_revealed(x, y)) continue;

            {
                int need = count_adjacent_mines(x, y);
                if (need == 0) continue;

                int flags = count_adjacent_flags(x, y);
                int hidden = count_adjacent_hidden_unflagged(x, y);
                if (hidden == 0) continue;

                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        {
                            int nx = x + dx;
                            int ny = y + dy;
                            if (!in_bounds(nx, ny)) continue;
                            if (is_revealed(nx, ny) || is_flagged(nx, ny)) continue;

                            if (flags == need) {
                                *out_x = nx;
                                *out_y = ny;
                                *out_action = 0;
                                return 1;
                            }
                            if (flags + hidden == need) {
                                *out_x = nx;
                                *out_y = ny;
                                *out_action = 1;
                                return 1;
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}

static int apply_difficulty(ms_difficulty_t d) {
    if (d == MS_DIFF_EASY) {
        g_w = 9; g_h = 9; g_mines = 10;
    } else if (d == MS_DIFF_INTERMEDIATE) {
        g_w = 16; g_h = 16; g_mines = 40;
    } else if (d == MS_DIFF_HARD) {
        g_w = 30; g_h = 16; g_mines = 99;
    } else {
        return 0;
    }
    g_difficulty = d;
    return 1;
}

static int validate_custom(int w, int h, int mines) {
    int safe_needed_min = 1;
    int total = w * h;
    if (w < 5 || h < 5) return 0;
    if (w > MS_MAX_W || h > MS_MAX_H) return 0;
    if (mines < 1) return 0;
    if (mines > total - safe_needed_min) return 0;
    return 1;
}

static void new_game(void) {
    ms_seed_rng();
    clear_board_cells();

    g_game_over = 0;
    g_won = 0;
    g_first_open = 1;
    g_cursor_x = g_w / 2;
    g_cursor_y = g_h / 2;

    g_timer_started = 0;
    g_start_seconds = 0;
    g_elapsed_on_end = 0;

    g_moves = 0;
    g_open_actions = 0;
    g_safe_open_actions = 0;
    g_flag_place_actions = 0;
    g_flags_used = 0;
}

static void show_help(void) {
    vga_puts("Minesweeper help:\n");
    vga_puts("  open <x> <y> or o <x> <y> : open a cell\n");
    vga_puts("  open / o                   : open cell at cursor\n");
    vga_puts("  flag <x> <y> or f <x> <y> : cycle mark (#->F->?->#)\n");
    vga_puts("  flag / f / m              : mark at cursor\n");
    vga_puts("  hint                      : show one safe suggestion\n");
    vga_puts("  w/a/s/d                   : move cursor\n");
    vga_puts("  difficulty <easy|intermediate|hard>\n");
    vga_puts("  custom <w> <h> <mines>    : max 30x16\n");
    vga_puts("  new / restart             : reset board\n");
    vga_puts("  exit                      : leave minesweeper\n");
}

void minesweeper_run(void) {
    char line[96];

    console_clear_cancel();
    apply_difficulty(MS_DIFF_EASY);
    new_game();

    while (!console_cancel_requested()) {
        char* argv[6];
        int argc = 0;

        vga_clear();
        print_board();
        vga_puts("ms> ");
        console_begin_input();
        if (console_cancel_requested()) break;
        console_readline(line, sizeof(line));

        if (console_cancel_requested()) break;
        if (line[0] == '\0') continue;

        argc = split_tokens(line, argv, 6);
        if (argc < 0) {
            vga_puts("too many tokens; type 'help'\n");
            continue;
        }
        if (argc == 0) continue;

        if (kstrcmp(argv[0], "exit") == 0) break;

        if (kstrcmp(argv[0], "help") == 0) {
            show_help();
            wait_ticks(35);
            continue;
        }

        if (kstrcmp(argv[0], "new") == 0 || kstrcmp(argv[0], "restart") == 0) {
            new_game();
            continue;
        }

        if (kstrcmp(argv[0], "w") == 0 || kstrcmp(argv[0], "up") == 0) {
            move_cursor(0, -1);
            continue;
        }
        if (kstrcmp(argv[0], "s") == 0 || kstrcmp(argv[0], "down") == 0) {
            move_cursor(0, 1);
            continue;
        }
        if (kstrcmp(argv[0], "a") == 0 || kstrcmp(argv[0], "left") == 0) {
            move_cursor(-1, 0);
            continue;
        }
        if (kstrcmp(argv[0], "d") == 0 || kstrcmp(argv[0], "right") == 0) {
            move_cursor(1, 0);
            continue;
        }

        if (kstrcmp(argv[0], "difficulty") == 0) {
            if (argc != 2) {
                vga_puts("usage: difficulty <easy|intermediate|hard>\n");
                wait_ticks(35);
                continue;
            }
            if (kstrcmp(argv[1], "easy") == 0) {
                apply_difficulty(MS_DIFF_EASY);
            } else if (kstrcmp(argv[1], "intermediate") == 0) {
                apply_difficulty(MS_DIFF_INTERMEDIATE);
            } else if (kstrcmp(argv[1], "hard") == 0) {
                apply_difficulty(MS_DIFF_HARD);
            } else {
                vga_puts("unknown difficulty\n");
                wait_ticks(35);
                continue;
            }
            new_game();
            continue;
        }

        if (kstrcmp(argv[0], "custom") == 0) {
            int w = 0, h = 0, m = 0;
            if (argc != 4 ||
                !parse_coord_token(argv[1], MS_MAX_W + 1, &w) ||
                !parse_coord_token(argv[2], MS_MAX_H + 1, &h) ||
                !parse_coord_token(argv[3], MS_MAX_W * MS_MAX_H + 1, &m)) {
                vga_puts("usage: custom <w> <h> <mines>\n");
                wait_ticks(35);
                continue;
            }

            if (!validate_custom(w, h, m)) {
                vga_puts("invalid custom settings (w/h: 5..30x16, mines: >=1 and < cells)\n");
                wait_ticks(35);
                continue;
            }

            g_w = w;
            g_h = h;
            g_mines = m;
            g_difficulty = MS_DIFF_CUSTOM;
            new_game();
            continue;
        }

        if (kstrcmp(argv[0], "open") == 0 || kstrcmp(argv[0], "o") == 0) {
            int x = g_cursor_x;
            int y = g_cursor_y;
            if (argc == 3) {
                if (!parse_coord_token(argv[1], g_w, &x) || !parse_coord_token(argv[2], g_h, &y)) {
                    vga_puts("usage: open <x> <y>\n");
                    wait_ticks(35);
                    continue;
                }
            } else if (argc != 1) {
                vga_puts("usage: open [x y]\n");
                wait_ticks(35);
                continue;
            }
            g_cursor_x = x;
            g_cursor_y = y;
            do_open(x, y);
            continue;
        }

        if (kstrcmp(argv[0], "flag") == 0 || kstrcmp(argv[0], "f") == 0 || kstrcmp(argv[0], "m") == 0) {
            int x = g_cursor_x;
            int y = g_cursor_y;
            if (argc == 3) {
                if (!parse_coord_token(argv[1], g_w, &x) || !parse_coord_token(argv[2], g_h, &y)) {
                    vga_puts("usage: flag <x> <y>\n");
                    wait_ticks(35);
                    continue;
                }
            } else if (argc != 1) {
                vga_puts("usage: flag [x y]\n");
                wait_ticks(35);
                continue;
            }
            g_cursor_x = x;
            g_cursor_y = y;
            do_mark(x, y);
            continue;
        }

        if (kstrcmp(argv[0], "hint") == 0) {
            int hx = -1;
            int hy = -1;
            int action = -1;

            if (argc != 1) {
                vga_puts("usage: hint\n");
                wait_ticks(35);
                continue;
            }

            if (find_hint(&hx, &hy, &action)) {
                g_cursor_x = hx;
                g_cursor_y = hy;
                if (action == 0) {
                    vga_puts("hint: safe open at ");
                } else {
                    vga_puts("hint: likely mine at ");
                }
                vga_putc('(');
                kprint_dec((uint32_t)hx);
                vga_putc(',');
                kprint_dec((uint32_t)hy);
                vga_puts(")\n");
            } else {
                vga_puts("hint: no deterministic move found\n");
            }
            wait_ticks(35);
            continue;
        }

        if (starts_with(argv[0], "open") || starts_with(argv[0], "flag")) {
            vga_puts("unknown action (did you mean 'open' or 'flag'?)\n");
            wait_ticks(35);
            continue;
        }

        vga_puts("unknown action; type 'help'\n");
        wait_ticks(35);
    }

    console_clear_cancel();
    vga_puts("[minesweeper] returned\n");
}
