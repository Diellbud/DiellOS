#pragma once
#include <stdint.h>

void kprint(const char* s);
void kprint_char(char c);
void kprint_hex8(uint8_t v);
void kprint_hex32(uint32_t v);
void kprint_dec(uint32_t v);
