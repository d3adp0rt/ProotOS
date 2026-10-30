#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

long syscall0(long n);
long syscall1(long n, long a);
long syscall2(long n, long a, long b);
long syscall3(long n, long a, long b, long c);
long syscall4(long n, long a, long b, long c, long d);

long write(int fd, const void *buf, size_t len);
long read(int fd, void *buf, size_t len);
int open(const char *path, int flags, int mode);
int close(int fd);
int fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
int wait4(int pid, int *status, int options, void *rusage);
void _exit(int code) __attribute__((noreturn));
int getpid(void);
int sleep_ms(unsigned ms);
int pipe(int fds[2]);
long lseek(int fd, long offset, int whence);
int mkdir(const char *path, int mode);
int unlink(const char *path);
int shutdown_os(void);
int reboot_os(void);

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *d, const char *s);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);

int printf(const char *fmt, ...);
int puts(const char *s);
int putchar(int c);
int atoi(const char *s);

struct stat { uint64_t size; uint32_t mode; uint32_t type; };
int stat(const char *path, struct stat *st);
