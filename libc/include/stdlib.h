#pragma once
#include <stddef.h>

void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void free(void *p);

int abs(int v);
long labs(long v);
int atoi(const char *s);
double atof(const char *s);
int atexit(void (*fn)(void));
int system(const char *cmd);
long atol(const char *s);
double strtod(const char *s, char **end);
float strtof(const char *s, char **end);
long double strtold(const char *s, char **end);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *));
void exit(int code) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
char *getenv(const char *name);
