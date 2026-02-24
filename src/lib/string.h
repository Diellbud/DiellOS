#pragma once
#include <stddef.h>
#include <stdint.h>

size_t kstrlen(const char* s);
int    kstrcmp(const char* a, const char* b);
void*  kmemcpy(void* dst, const void* src, size_t n);
void*  kmemset(void* dst, int v, size_t n);
char*  kstrncpy(char* dst, const char* src, size_t n);
