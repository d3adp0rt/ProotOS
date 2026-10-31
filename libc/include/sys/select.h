#pragma once
#include <stddef.h>
#include <sys/time.h>
typedef struct { unsigned long fds_bits[16]; } fd_set;
#define FD_ZERO(s) do { for (int _i = 0; _i < 16; _i++) (s)->fds_bits[_i] = 0; } while (0)
#define FD_SET(fd, s) ((s)->fds_bits[(fd) / 64] |= 1UL << ((fd) % 64))
#define FD_CLR(fd, s) ((s)->fds_bits[(fd) / 64] &= ~(1UL << ((fd) % 64)))
#define FD_ISSET(fd, s) (((s)->fds_bits[(fd) / 64] >> ((fd) % 64)) & 1UL)
int select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *timeout);
