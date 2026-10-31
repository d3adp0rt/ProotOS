#pragma once
#include <stdint.h>
struct stat {
    uint64_t st_size;
    uint32_t st_mode;
    uint32_t st_type;
    uint64_t st_mtime;
};
#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
int stat(const char *path, struct stat *st);
