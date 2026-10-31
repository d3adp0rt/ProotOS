#pragma once
#include <stddef.h>
typedef long ssize_t;
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
long read(int fd, void *buf, size_t n);
long write(int fd, const void *buf, size_t n);
int close(int fd);
long lseek(int fd, long off, int whence);
int unlink(const char *path);
int getpid(void);
char *getcwd(char *buf, size_t size);
int chdir(const char *path);
int isatty(int fd);
int execvp(const char *file, char *const argv[]);
unsigned sleep(unsigned seconds);
int usleep(unsigned usec);
