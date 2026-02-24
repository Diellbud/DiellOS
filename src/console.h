#pragma once
#include <stddef.h>
#include <stdint.h>

void console_init(void);
void console_begin_input(void);

void console_on_key(char c);
void console_readline(char* out, size_t max);

void console_history_up(void);
void console_history_down(void);

void console_cursor_left(void);
void console_cursor_right(void);

void console_request_cancel(void);
int  console_cancel_requested(void);
void console_clear_cancel(void);