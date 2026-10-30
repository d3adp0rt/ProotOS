/*
 * ProotOS — freestanding memory/string routines.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "kernel.h"

void *memset(void *dst, int value, size_t n) {
    unsigned char *p = dst;
    while (n--)
        *p++ = (unsigned char)value;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (!n)
        return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) { }
    return dst;
}

char *strstr(const char *hay, const char *needle) {
    if (!*needle) return (char *)hay;
    for (; *hay; hay++) {
        const char *a = hay, *b = needle;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char *)hay;
    }
    return 0;
}
char *strchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char *)s; s++; }
    return (c == 0) ? (char *)s : 0;
}
char *strrchr(const char *s, int c) {
    const char *last = 0;
    while (*s) { if (*s == (char)c) last = s; s++; }
    return (c == 0) ? (char *)s : (char *)last;
}
