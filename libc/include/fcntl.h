#pragma once
#include <stddef.h>
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x40
#define O_TRUNC 0x200
#define O_APPEND 0x400
#define O_BINARY 0
#define O_NONBLOCK 0x800
#define F_GETFL 3
#define F_SETFL 4
int fcntl(int fd, int cmd, int arg);
long read(int fd, void *buf, size_t n);
long write(int fd, const void *buf, size_t n);
int open(const char *path, int flags, ...);
int close(int fd);
long lseek(int fd, long off, int whence);
int unlink(const char *path);
