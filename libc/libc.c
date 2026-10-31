#include "libc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <poll.h>

int errno;

struct __proot_file {
    int fd;
    int eof;
    int err;
    int unget;
};
static struct __proot_file g_stdin = {0, 0, 0, -1};
static struct __proot_file g_stdout = {1, 0, 0, -1};
static struct __proot_file g_stderr = {2, 0, 0, -1};
FILE *stdin = &g_stdin;
FILE *stdout = &g_stdout;
FILE *stderr = &g_stderr;

long syscall0(long n) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n) : "memory");
    return ret;
}
long syscall1(long n, long a) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a) : "memory");
    return ret;
}
long syscall2(long n, long a, long b) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a), "S"(b) : "memory");
    return ret;
}
long syscall3(long n, long a, long b, long c) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : "memory");
    return ret;
}
long syscall4(long n, long a, long b, long c, long d) {
    register long r10 __asm__("r10") = d;
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "memory");
    return ret;
}

long write(int fd, const void *buf, size_t len) {
    return syscall3(1, fd, (long)buf, (long)len);
}
long read(int fd, void *buf, size_t len) {
    return syscall3(0, fd, (long)buf, (long)len);
}
int open(const char *path, int flags, ...) {
    int mode = 0;
    if (flags & 0x242)
        mode = 0644;
    return (int)syscall3(6, (long)path, flags, mode);
}
int close(int fd) {
    return (int)syscall1(3, fd);
}
int fork(void) {
    return (int)syscall0(57);
}
int execve(const char *path, char *const argv[], char *const envp[]) {
    return (int)syscall3(59, (long)path, (long)argv, (long)envp);
}
int wait4(int pid, int *status, int options, void *rusage) {
    return (int)syscall4(61, pid, (long)status, options, (long)rusage);
}
void _exit(int code) {
    syscall1(60, code);
    for (;;) {
    }
}
int getpid(void) {
    return (int)syscall0(2);
}
int sleep_ms(unsigned ms) {
    return (int)syscall1(4, ms);
}
int pipe(int fds[2]) {
    return (int)syscall1(22, (long)fds);
}
int dup2(int oldfd, int newfd) {
    return (int)syscall2(33, oldfd, newfd);
}
int fcntl(int fd, int cmd, int arg) {
    return (int)syscall3(72, fd, cmd, arg);
}
long lseek(int fd, long offset, int whence) {
    return syscall3(10, fd, offset, whence);
}
int mkdir(const char *path, int mode) {
    return (int)syscall2(8, (long)path, mode);
}
int unlink(const char *path) {
    return (int)syscall1(9, (long)path);
}
int stat(const char *path, struct stat *st) {
    struct { uint64_t size; uint32_t mode; uint32_t type; } k;
    int r = (int)syscall2(7, (long)path, (long)&k);
    if (r != 0 || !st)
        return r;
    st->st_size = k.size;
    st->st_type = k.type;
    st->st_mode = (k.type == 2 ? S_IFDIR : S_IFREG) | (k.mode & 0777);
    st->st_mtime = 0;
    return 0;
}
int shutdown_os(void) {
    return (int)syscall0(169);
}
int reboot_os(void) {
    return (int)syscall0(170);
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
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}
char *strcat(char *d, const char *s) {
    char *r = d;
    while (*d) d++;
    while ((*d++ = *s++)) {}
    return r;
}

char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++)) {
    }
    return r;
}
void *memset(void *d, int c, size_t n) {
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--)
        *a++ = *b++;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    if (a < b) {
        while (n--)
            *a++ = *b++;
    } else {
        a += n;
        b += n;
        while (n--)
            *--a = *--b;
    }
    return d;
}
int atoi(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return v;
}

int printf(const char *fmt, ...) {
    char out[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out, sizeof(out), fmt, ap);
    va_end(ap);
    if (n > 0)
        write(1, out, (size_t)(n < (int)sizeof(out) ? n : (int)sizeof(out) - 1));
    return n;
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    char out[1024];
    int n = vsnprintf(out, sizeof(out), fmt, ap);
    if (f && n > 0)
        write(f->fd, out, (size_t)(n < (int)sizeof(out) ? n : (int)sizeof(out) - 1));
    return n;
}

int fprintf(FILE *f, const char *fmt, ...) {
    char out[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out, sizeof(out), fmt, ap);
    va_end(ap);
    if (f && n > 0)
        write(f->fd, out, (size_t)(n < (int)sizeof(out) ? n : (int)sizeof(out) - 1));
    return n;
}

int fputc(int c, FILE *f) {
    unsigned char ch = (unsigned char)c;
    return f && write(f->fd, &ch, 1) == 1 ? c : -1;
}

int fputs(const char *s, FILE *f) {
    if (!f || !s)
        return -1;
    size_t n = strlen(s);
    return write(f->fd, s, n) == (long)n ? 0 : -1;
}

FILE *fopen(const char *path, const char *mode) {
    (void)mode;
    int fd = open(path, 0, 0);
    if (fd < 0)
        return 0;
    FILE *f = malloc(sizeof(*f));
    if (!f) {
        close(fd);
        return 0;
    }
    f->fd = fd;
    f->eof = 0;
    f->err = 0;
    f->unget = -1;
    return f;
}

FILE *fdopen(int fd, const char *mode) {
    (void)mode;
    if (fd < 0)
        return 0;
    FILE *f = malloc(sizeof(*f));
    if (!f)
        return 0;
    f->fd = fd;
    f->eof = 0;
    f->err = 0;
    f->unget = -1;
    return f;
}

FILE *freopen(const char *path, const char *mode, FILE *f) {
    (void)mode;
    if (!f)
        return 0;
    int fd = open(path, 0, 0);
    if (fd < 0)
        return 0;
    if (f->fd > 2)
        close(f->fd);
    f->fd = fd;
    f->eof = 0;
    f->err = 0;
    f->unget = -1;
    return f;
}

int fclose(FILE *f) {
    if (!f)
        return -1;
    if (f->fd > 2)
        close(f->fd);
    if (f->fd > 2)
        free(f);
    return 0;
}

size_t fread(void *buf, size_t size, size_t n, FILE *f) {
    if (!f || !size || !n)
        return 0;
    size_t want = size * n;
    long got = read(f->fd, buf, want);
    if (got <= 0) {
        f->eof = 1;
        return 0;
    }
    if ((size_t)got < want)
        f->eof = 1;
    return (size_t)got / size;
}

size_t fwrite(const void *buf, size_t size, size_t n, FILE *f) {
    if (!f || !size || !n)
        return 0;
    size_t want = size * n;
    long got = write(f->fd, buf, want);
    return got > 0 ? (size_t)got / size : 0;
}

int fseek(FILE *f, long off, int whence) {
    if (!f)
        return -1;
    return lseek(f->fd, off, whence) < 0 ? -1 : 0;
}

long ftell(FILE *f) {
    if (!f)
        return -1;
    return lseek(f->fd, 0, 1);
}

int ferror(FILE *f) { return f ? f->err : 1; }
int feof(FILE *f) { return f ? f->eof : 1; }
char *fgets(char *s, int n, FILE *f) {
    if (!f || !s || n <= 0)
        return 0;
    int i = 0;
    while (i < n - 1) {
        unsigned char c;
        if (read(f->fd, &c, 1) != 1) {
            f->eof = 1;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n')
            break;
    }
    if (i == 0)
        return 0;
    s[i] = 0;
    return s;
}

int fflush(FILE *f) { (void)f; return 0; }

static int scan_common(FILE *f, const char *s, const char *fmt, va_list ap) {
    (void)f;
    int matched = 0;
    while (*fmt && *s) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 'd') {
                int *out = va_arg(ap, int *);
                int neg = 0, v = 0, any = 0;
                while (*s == ' ' || *s == '\t') s++;
                if (*s == '-') { neg = 1; s++; }
                while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); any = 1; }
                *out = neg ? -v : v;
                matched++;
            } else if (*fmt == 'f') {
                char *end = 0;
                double *out = va_arg(ap, double *);
                *out = strtod(s, &end);
                if (end == s) break;
                s = end;
                matched++;
            } else if (*fmt == 's') {
                char *out = va_arg(ap, char *);
                while (*s == ' ' || *s == '\t') s++;
                while (*s && *s != ' ' && *s != '\t') *out++ = *s++;
                *out = 0;
                matched++;
            } else if (*fmt == 'c') {
                char *out = va_arg(ap, char *);
                *out = *s++;
                matched++;
            } else {
                break;
            }
            fmt++;
        } else {
            if (*fmt != *s)
                break;
            fmt++;
            s++;
        }
    }
    return matched;
}

int vfscanf(FILE *f, const char *fmt, va_list ap) {
    char buf[512];
    long n = f ? read(f->fd, buf, sizeof(buf) - 1) : -1;
    if (n <= 0)
        return EOF;
    buf[n] = 0;
    return scan_common(f, buf, fmt, ap);
}

int fscanf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfscanf(f, fmt, ap);
    va_end(ap);
    return r;
}

int sscanf(const char *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int matched = 0;
    while (*fmt && *s) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 'd') {
                int *out = va_arg(ap, int *);
                int neg = 0, v = 0, any = 0;
                while (*s == ' ' || *s == '\t') s++;
                if (*s == '-') { neg = 1; s++; }
                while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); any = 1; }
                if (*out) *out = neg ? -v : v;
                matched++;
            } else if (*fmt == 's') {
                char *out = va_arg(ap, char *);
                while (*s == ' ' || *s == '\t') s++;
                while (*s && *s != ' ' && *s != '\t') *out++ = *s++;
                *out = 0;
                matched++;
            } else if (*fmt == 'c') {
                char *out = va_arg(ap, char *);
                *out = *s++;
                matched++;
            } else {
                break;
            }
            fmt++;
        } else {
            if (*fmt != *s)
                break;
            fmt++;
            s++;
        }
    }
    va_end(ap);
    return matched;
}

time_t time(time_t *t) {
    if (t)
        *t = 0;
    return 0;
}

clock_t clock(void) { return 0; }
double difftime(time_t a, time_t b) { return (double)(a - b); }

static struct tm g_tm;
struct tm *localtime(const time_t *t) {
    (void)t;
    g_tm.tm_year = 124;
    g_tm.tm_mon = 0;
    g_tm.tm_mday = 1;
    return &g_tm;
}
struct tm *gmtime(const time_t *t) { return localtime(t); }

void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *)) {
    const unsigned char *b = base;
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int r = cmp(key, b + mid * size);
        if (r == 0)
            return (void *)(b + mid * size);
        if (r < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return 0;
}

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
    unsigned char *b = base;
    unsigned char tmp[128];
    for (size_t i = 1; i < n; i++) {
        size_t j = i;
        while (j > 0 && cmp(b + (j - 1) * size, b + j * size) > 0) {
            if (size <= sizeof(tmp)) {
                memcpy(tmp, b + (j - 1) * size, size);
                memcpy(b + (j - 1) * size, b + j * size, size);
                memcpy(b + j * size, tmp, size);
            }
            j--;
        }
    }
}

int getc(FILE *f) {
    unsigned char c;
    if (!f)
        return -1;
    if (f->unget >= 0) {
        c = (unsigned char)f->unget;
        f->unget = -1;
        return c;
    }
    if (read(f->fd, &c, 1) != 1)
        return -1;
    return c;
}

int fgetc(FILE *f) { return getc(f); }
int ungetc(int c, FILE *f) {
    if (!f || c == -1)
        return -1;
    f->unget = c & 0xff;
    return c & 0xff;
}
int fileno(FILE *f) { return f ? f->fd : -1; }
int setvbuf(FILE *f, char *buf, int mode, size_t size) {
    (void)f; (void)buf; (void)mode; (void)size;
    return 0;
}
void perror(const char *s) {
    if (s && *s) {
        write(2, s, strlen(s));
        write(2, ": ", 2);
    }
    const char *e = strerror(errno);
    write(2, e, strlen(e));
    write(2, "\n", 1);
}

int puts(const char *s) {
    write(1, s, strlen(s));
    write(1, "\n", 1);
    return 0;
}
int putchar(int c) {
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}

int statfs(const char *path, struct statfs *st) {
    return (int)syscall2(80, (long)path, (long)st);
}
int readdir(const char *path, unsigned index, char *name, size_t len) {
    return (int)syscall4(78, (long)path, index, (long)name, (long)len);
}
long getprocs(char *buf, size_t len) {
    return syscall2(79, (long)buf, (long)len);
}
int mount_fs(const char *path, const char *type) {
    return (int)syscall2(11, (long)path, (long)type);
}
int umount_fs(const char *path) {
    return (int)syscall1(12, (long)path);
}

unsigned inet_addr(const char *s) {
    unsigned a=0,b=0,c=0,d=0;
    while (*s >= '0' && *s <= '9') a = a*10 + (unsigned)(*s++ - '0'); if (*s=='.') s++; else return 0;
    while (*s >= '0' && *s <= '9') b = b*10 + (unsigned)(*s++ - '0'); if (*s=='.') s++; else return 0;
    while (*s >= '0' && *s <= '9') c = c*10 + (unsigned)(*s++ - '0'); if (*s=='.') s++; else return 0;
    while (*s >= '0' && *s <= '9') d = d*10 + (unsigned)(*s++ - '0');
    return (a<<24)|(b<<16)|(c<<8)|d;
}
int net_ping_ip(unsigned ip) { return (int)syscall1(81, ip); }
long net_http_get(const char *host, const char *path, char *buf, size_t cap) {
    return syscall4(82, (long)host, (long)path, (long)buf, (long)cap);
}

int fb_info(struct fb_info *out) {
    return (int)syscall1(85, (long)out);
}
void *fb_map(void) {
    return (void *)syscall0(86);
}
int fb_unmap(void) {
    return (int)syscall0(88);
}
int console_gui(int on) {
    return (int)syscall1(89, on);
}
unsigned rtc_hms(void) {
    return (unsigned)syscall0(93);
}
int console_info(int *cols, int *rows) {
    int out[2];
    if (syscall1(94, (long)out) != 0)
        return -1;
    if (cols) *cols = out[0];
    if (rows) *rows = out[1];
    return 0;
}
int clear_screen(void) { return (int)syscall0(83); }

long net_https_get(const char *host, const char *path, char *buf, size_t cap) {
    return syscall4(84, (long)host, (long)path, (long)buf, (long)cap);
}

int strcoll(const char *a, const char *b) { return strcmp(a, b); }

int strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a++);
        int cb = tolower((unsigned char)*b++);
        if (ca != cb)
            return ca - cb;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    while (n && *a && *b) {
        int ca = tolower((unsigned char)*a++);
        int cb = tolower((unsigned char)*b++);
        if (ca != cb)
            return ca - cb;
        n--;
    }
    if (!n)
        return 0;
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

char *strndup(const char *s, size_t n) {
    size_t len = 0;
    while (len < n && s[len])
        len++;
    char *p = malloc(len + 1);
    if (p) {
        memcpy(p, s, len);
        p[len] = 0;
    }
    return p;
}

char *strerror(int err) {
    switch (err) {
    case 0: return (char *)"success";
    case ENOENT: return (char *)"no such file";
    case EIO: return (char *)"I/O error";
    case EBADF: return (char *)"bad file descriptor";
    case EAGAIN: return (char *)"try again";
    case ENOMEM: return (char *)"out of memory";
    case EINVAL: return (char *)"invalid argument";
    case EPIPE: return (char *)"broken pipe";
    default: return (char *)"error";
    }
}

char *strstr(const char *h, const char *n) {
    if (!*n) return (char *)h;
    for (; *h; h++) { const char *a=h,*b=n; while (*a && *b && *a==*b) {a++;b++;} if (!*b) return (char*)h; }
    return 0;
}

#define HEAP_SIZE (4 * 1024 * 1024)
#define HEAP_CHUNK (4 * 1024 * 1024)
#define HEAP_MAX (32 * 1024 * 1024)
#define HEAP_MAGIC 0x48454150u
#define MAX_CHUNKS 16
struct heap_block {
    size_t size;
    uint32_t magic;
    int free;
    int last;
    struct heap_block *next;
};
struct heap_chunk {
    unsigned char *start;
    size_t size;
};
static unsigned char g_heap[HEAP_SIZE] __attribute__((aligned(16)));
static struct heap_block *g_heap_head;
static struct heap_block *g_heap_tail;
static struct heap_chunk g_chunks[MAX_CHUNKS];
static int g_nchunks;
static size_t g_heap_total;
static int g_heap_ready;

static void chunk_register(unsigned char *start, size_t size) {
    if (g_nchunks < MAX_CHUNKS) {
        g_chunks[g_nchunks].start = start;
        g_chunks[g_nchunks].size = size;
        g_nchunks++;
    }
    g_heap_total += size;
}

static void heap_init(void) {
    g_heap_head = (struct heap_block *)g_heap;
    g_heap_head->size = HEAP_SIZE - sizeof(struct heap_block);
    g_heap_head->magic = HEAP_MAGIC;
    g_heap_head->free = 1;
    g_heap_head->last = 1;
    g_heap_head->next = 0;
    g_heap_tail = g_heap_head;
    g_nchunks = 0;
    g_heap_total = 0;
    chunk_register(g_heap, HEAP_SIZE);
    g_heap_ready = 1;
}

static int heap_grow(size_t need) {
    size_t chunk = HEAP_CHUNK;
    if (need + sizeof(struct heap_block) + 64 > chunk)
        chunk = (need + sizeof(struct heap_block) + 64 + 4095) & ~(size_t)4095;
    if (g_heap_total + chunk > HEAP_MAX)
        return -1;
    void *p = mmap(0, chunk, 1 /*PROT_READ*/, 0x22 /*MAP_PRIVATE|MAP_ANONYMOUS*/, -1, 0);
    if (p == (void *)-1)
        return -1;
    unsigned char *start = p;
    struct heap_block *b = (struct heap_block *)start;
    b->size = chunk - sizeof(struct heap_block);
    b->magic = HEAP_MAGIC;
    b->free = 1;
    b->last = 1;
    b->next = 0;
    g_heap_tail->next = b;
    g_heap_tail = b;
    chunk_register(start, chunk);
    return 0;
}

static int heap_owns(const struct heap_block *b) {
    const unsigned char *p = (const unsigned char *)b;
    for (int i = 0; i < g_nchunks; i++) {
        if (p >= g_chunks[i].start && p < g_chunks[i].start + g_chunks[i].size)
            return 1;
    }
    return 0;
}

void *malloc(size_t n) {
    if (!g_heap_ready)
        heap_init();
    if (!n)
        n = 1;
    n = (n + 15) & ~(size_t)15;
    for (;;) {
        for (struct heap_block *b = g_heap_head; b; b = b->next) {
            if (!b->free || b->size < n)
                continue;
            if (b->size >= n + sizeof(struct heap_block) + 32) {
                struct heap_block *nb =
                    (struct heap_block *)((unsigned char *)(b + 1) + n);
                nb->size = b->size - n - sizeof(struct heap_block);
                nb->magic = HEAP_MAGIC;
                nb->free = 1;
                nb->last = b->last;
                nb->next = b->next;
                b->next = nb;
                b->last = 0;
                b->size = n;
            }
            b->free = 0;
            return b + 1;
        }
        if (heap_grow(n) != 0)
            return 0;
    }
}

void free(void *p) {
    if (!p)
        return;
    struct heap_block *b = (struct heap_block *)p - 1;
    if (!g_heap_ready || !heap_owns(b) || b->magic != HEAP_MAGIC)
        return;
    b->free = 1;
    for (struct heap_block *cur = g_heap_head; cur && cur->next;) {
        if (cur->free && cur->next->free && !cur->last) {
            cur->size += sizeof(struct heap_block) + cur->next->size;
            cur->last = cur->next->last;
            cur->next = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
}

void *calloc(size_t n, size_t size) {
    size_t total = n * size;
    if (size && total / size != n)
        return 0;
    void *p = malloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

void *realloc(void *p, size_t n) {
    if (!p)
        return malloc(n);
    if (!n) {
        free(p);
        return 0;
    }
    struct heap_block *b = (struct heap_block *)p - 1;
    if (b->magic != HEAP_MAGIC)
        return p;
    size_t old = b->size;
    if (old >= n)
        return p;
    void *q = malloc(n);
    if (!q)
        return 0;
    memcpy(q, p, old);
    free(p);
    return q;
}

char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
}

char *strncat(char *d, const char *s, size_t n) {
    char *r = d;
    while (*d)
        d++;
    size_t i = 0;
    while (i < n && s[i]) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
    return r;
}

char *strchr(const char *s, int c) {
    for (; *s; s++)
        if (*s == (char)c)
            return (char *)s;
    return c == 0 ? (char *)s : 0;
}

char *strrchr(const char *s, int c) {
    const char *r = 0;
    for (;; s++) {
        if (*s == (char)c)
            r = s;
        if (!*s)
            break;
    }
    return (char *)r;
}

size_t strspn(const char *s, const char *accept) {
    size_t n = 0;
    while (s[n]) {
        int ok = 0;
        for (const char *a = accept; *a; a++)
            if (*a == s[n]) { ok = 1; break; }
        if (!ok)
            break;
        n++;
    }
    return n;
}

size_t strcspn(const char *s, const char *reject) {
    size_t n = 0;
    while (s[n]) {
        for (const char *r = reject; *r; r++)
            if (*r == s[n])
                return n;
        n++;
    }
    return n;
}

char *strpbrk(const char *s, const char *accept) {
    for (; *s; s++)
        for (const char *a = accept; *a; a++)
            if (*a == *s)
                return (char *)s;
    return 0;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    for (size_t i = 0; i < n; i++)
        if (p[i] == (unsigned char)c)
            return (void *)(p + i);
    return 0;
}

int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int iscntrl(int c) { return c >= 0 && (c < 32 || c == 127); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isgraph(int c) { return c > 32 && c < 127; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isprint(int c) { return c >= 32 && c < 127; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int tolower(int c) { return isupper(c) ? c - 'A' + 'a' : c; }
int toupper(int c) { return islower(c) ? c - 'a' + 'A' : c; }

static void (*g_atexit[32])(void);
static int g_atexit_count;

int atexit(void (*fn)(void)) {
    if (!fn || g_atexit_count >= 32)
        return -1;
    g_atexit[g_atexit_count++] = fn;
    return 0;
}

int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }
long atol(const char *s) { return strtol(s, 0, 10); }

double strtod(const char *s, char **end) {
    const char *p = s;
    while (isspace((unsigned char)*p))
        p++;
    int neg = 0;
    if (*p == '+' || *p == '-') {
        neg = *p == '-';
        p++;
    }
    double v = 0.0;
    int any = 0;
    while (isdigit((unsigned char)*p)) {
        v = v * 10.0 + (*p++ - '0');
        any = 1;
    }
    if (*p == '.') {
        p++;
        double f = 0.1;
        while (isdigit((unsigned char)*p)) {
            v += (*p++ - '0') * f;
            f *= 0.1;
            any = 1;
        }
    }
    if (any && (*p == 'e' || *p == 'E')) {
        const char *save = p;
        p++;
        int eneg = 0;
        if (*p == '+' || *p == '-') { eneg = *p == '-'; p++; }
        int e = 0, ed = 0;
        while (isdigit((unsigned char)*p)) { e = e * 10 + (*p++ - '0'); ed = 1; }
        if (ed) {
            while (e-- > 0)
                v = eneg ? v / 10.0 : v * 10.0;
        } else {
            p = save;
        }
    }
    if (end)
        *end = (char *)p;
    return neg ? -v : v;
}

float strtof(const char *s, char **end) { return (float)strtod(s, end); }
long double strtold(const char *s, char **end) { return (long double)strtod(s, end); }

unsigned long long strtoull(const char *s, char **end, int base) {
    return (unsigned long long)strtoul(s, end, base);
}

unsigned long strtoul(const char *s, char **end, int base) {
    const char *p = s;
    while (isspace((unsigned char)*p))
        p++;
    int neg = 0;
    if (*p == '+' || *p == '-') { neg = *p == '-'; p++; }
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    } else if (base == 0) {
        base = p[0] == '0' ? 8 : 10;
    }
    unsigned long v = 0;
    for (;;) {
        int d;
        if (isdigit((unsigned char)*p)) d = *p - '0';
        else if (*p >= 'a' && *p <= 'z') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') d = *p - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned long)base + (unsigned long)d;
        p++;
    }
    if (end) *end = (char *)p;
    return neg ? (unsigned long)-(long)v : v;
}

long strtol(const char *s, char **end, int base) {
    return (long)strtoul(s, end, base);
}

void exit(int code) {
    for (int i = g_atexit_count - 1; i >= 0; i--)
        g_atexit[i]();
    _exit(code);
}

double atof(const char *s) { return strtod(s, 0); }
int system(const char *cmd) { (void)cmd; return -1; }
int rename(const char *old, const char *neu) {
    (void)old; (void)neu;
    return -1;
}
void abort(void) { _exit(134); }
char *getenv(const char *name) { (void)name; return 0; }

static struct lconv g_lconv = {".", "", ""};
struct lconv *localeconv(void) { return &g_lconv; }
char *setlocale(int category, const char *locale) {
    (void)category; (void)locale;
    return (char *)"C";
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
    (void)addr; (void)flags; (void)fd; (void)off;
    long r = syscall2(90, (long)len, (long)prot);
    return r < 0 ? MAP_FAILED : (void *)r;
}

int munmap(void *addr, size_t len) {
    return (int)syscall2(91, (long)addr, (long)len);
}

int mprotect(void *addr, size_t len, int prot) {
    return (int)syscall3(92, (long)addr, (long)len, (long)prot);
}

char *getcwd(char *buf, size_t size) {
    if (buf && size >= 2) { buf[0] = '/'; buf[1] = 0; return buf; }
    return 0;
}
int chdir(const char *path) { (void)path; return 0; }
unsigned sleep(unsigned seconds) {
    sleep_ms(seconds * 1000);
    return 0;
}
int usleep(unsigned usec) {
    sleep_ms(usec / 1000 + (usec % 1000 != 0));
    return 0;
}
int isatty(int fd) { return fd >= 0 && fd <= 2; }

int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (tv) { tv->tv_sec = 0; tv->tv_usec = 0; }
    return 0;
}

void *dlopen(const char *name, int flags) { (void)name; (void)flags; return 0; }
void *dlsym(void *h, const char *name) { (void)h; (void)name; return 0; }
int dlclose(void *h) { (void)h; return 0; }
char *dlerror(void) { return (char *)"no dynamic loader"; }
sighandler_t signal(int sig, sighandler_t h) { (void)sig; return h; }
int raise(int sig) { (void)sig; return 0; }
int remove(const char *path) { return unlink(path); }

int execvp(const char *file, char *const argv[]) { (void)file; (void)argv; return -1; }

int tcgetattr(int fd, struct termios *t) {
    (void)fd;
    if (!t) return -1;
    memset(t, 0, sizeof(*t));
    t->c_cc[VMIN] = 1;
    return 0;
}
int tcsetattr(int fd, int action, const struct termios *t) {
    (void)fd; (void)action; (void)t;
    return 0;
}
void cfmakeraw(struct termios *t) {
    if (!t) return;
    t->c_iflag = 0;
    t->c_oflag = 0;
    t->c_cflag = CS8 | CREAD;
    t->c_lflag = 0;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}
int tcflush(int fd, int queue) { (void)fd; (void)queue; return 0; }
speed_t cfgetospeed(const struct termios *t) { (void)t; return B38400; }
speed_t cfgetispeed(const struct termios *t) { (void)t; return B38400; }
int cfsetospeed(struct termios *t, speed_t speed) { (void)t; (void)speed; return 0; }
int cfsetispeed(struct termios *t, speed_t speed) { (void)t; (void)speed; return 0; }

int ioctl(int fd, unsigned long request, ...) {
    (void)fd;
    if (request == TIOCGWINSZ) {
        va_list ap;
        va_start(ap, request);
        struct winsize *ws = va_arg(ap, struct winsize *);
        va_end(ap);
        if (!ws) return -1;
        int cols = 80, rows = 25;
        console_info(&cols, &rows);
        ws->ws_col = (unsigned short)cols;
        ws->ws_row = (unsigned short)rows;
        ws->ws_xpixel = 0;
        ws->ws_ypixel = 0;
        return 0;
    }
    return -1;
}

int select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *timeout) {
    (void)nfds; (void)r; (void)w; (void)e;
    if (timeout) sleep_ms((unsigned)(timeout->tv_sec * 1000 + timeout->tv_usec / 1000));
    return 0;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    (void)fds; (void)nfds;
    if (timeout > 0) sleep_ms((unsigned)timeout);
    return 0;
}
