#pragma once
#include <stddef.h>

void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void free(void *p);

int abs(int v);
long labs(long v);
int atoi(const char *s);
long atol(const char *s);
double strtod(const char *s, char **end);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
void exit(int code) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
char *getenv(const char *name);
