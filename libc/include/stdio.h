#pragma once
#include <stddef.h>
#include <stdarg.h>

typedef struct __proot_file FILE;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define BUFSIZ 8192
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int snprintf(char *out, size_t cap, const char *fmt, ...);
int sprintf(char *out, const char *fmt, ...);
int vsnprintf(char *out, size_t cap, const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int fscanf(FILE *f, const char *fmt, ...);
int vfscanf(FILE *f, const char *fmt, va_list ap);
int rename(const char *old, const char *new);
int puts(const char *s);
int putchar(int c);
int fputs(const char *s, FILE *f);
int fputc(int c, FILE *f);
FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *f);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *f);
size_t fread(void *buf, size_t size, size_t n, FILE *f);
size_t fwrite(const void *buf, size_t size, size_t n, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
int ferror(FILE *f);
int feof(FILE *f);
int getc(FILE *f);
int fgetc(FILE *f);
int ungetc(int c, FILE *f);
int fileno(FILE *f);
int setvbuf(FILE *f, char *buf, int mode, size_t size);
void perror(const char *s);
char *fgets(char *s, int n, FILE *f);
int fflush(FILE *f);
int remove(const char *path);
int sscanf(const char *s, const char *fmt, ...);
