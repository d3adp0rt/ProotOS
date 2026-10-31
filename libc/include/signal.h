#pragma once
typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);
#define SIGINT 2
#define SIGILL 4
#define SIGABRT 6
#define SIGFPE 8
#define SIGSEGV 11
#define SIGBUS 7
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
sighandler_t signal(int sig, sighandler_t h);
int raise(int sig);
