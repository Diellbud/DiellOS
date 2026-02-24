#include "string.h"

size_t kstrlen(const char* s) {
    size_t n = 0;
    while (s && *s++) n++;
    return n;
}

int kstrcmp(const char* a, const char* b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *b && (*a == *b)) { a++; b++; }
    return (int)((unsigned char)*a) - (int)((unsigned char)*b);
}

void* kmemcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void* kmemset(void* dst, int v, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)v;
    return dst;
}

char* kstrncpy(char* dst, const char* src, size_t n) {
    if (n == 0) return dst;
    size_t i = 0;
    for (; i < n && src && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}
