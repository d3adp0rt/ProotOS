#include "libc.h"

static unsigned oct(const char *p) {
    unsigned v = 0;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '7') v = (v << 3) + (unsigned)(*p++ - '0');
    return v;
}

static int extract_tar(const char *path) {
    int fd = open(path, 0, 0);
    if (fd < 0) { printf("ppkg: cannot open %s\n", path); return 1; }
    unsigned char h[512];
    char name[256];
    while (read(fd, h, 512) == 512) {
        int all_zero = 1;
        for (int i = 0; i < 512; i++) if (h[i]) { all_zero = 0; break; }
        if (all_zero) break;
        int n = 0;
        while (n < 255 && h[n]) { name[n] = (char)h[n]; n++; }
        name[n] = 0;
        unsigned size = oct((const char *)h + 124);
        char type = (char)h[156];
        if (name[0] == 0) break;
        if (strncmp(name, "usr/", 4) == 0) { mkdir("usr", 0755); mkdir("usr/bin", 0755); }
        if (type == '5') {
            mkdir(name, 0755);
            continue;
        }
        int out = open(name, 0x242, 0644);
        if (out < 0) { printf("ppkg: cannot create %s\n", name); return 1; }
        unsigned left = size;
        unsigned char buf[512];
        while (left) {
            unsigned chunk = left > sizeof(buf) ? sizeof(buf) : left;
            long r = read(fd, buf, chunk);
            if (r <= 0) break;
            write(out, buf, (size_t)r);
            left -= (unsigned)r;
        }
        close(out);
        unsigned pad = (512 - (size % 512)) % 512;
        while (pad) { unsigned char tmp[512]; unsigned c = pad > 512 ? 512 : pad; read(fd, tmp, c); pad -= c; }
    }
    close(fd);
    return 0;
}

static int find_pkg(const char *want, char *path, size_t cap) {
    int fd = open("/var/lib/ppkg/index", 0, 0);
    if (fd < 0) return 0;
    char line[256];
    int n = 0;
    while (read(fd, &line[n], 1) == 1) {
        if (line[n] == '\n') {
            line[n] = 0;
            char *sp = line;
            while (*sp && *sp != ' ') sp++;
            if (*sp) *sp++ = 0;
            if (strcmp(line, want) == 0) {
                strcpy(path, sp);
                close(fd);
                return 1;
            }
            n = 0;
        } else if (n < (int)sizeof(line) - 1) n++;
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("ppkg: usage: ppkg list|install NAME\n"); return 1; }
    if (strcmp(argv[1], "list") == 0) {
        int fd = open("/var/lib/ppkg/index", 0, 0);
        if (fd < 0) { printf("ppkg: no index\n"); return 1; }
        char buf[512];
        long n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) write(1, buf, (size_t)n);
        close(fd);
        return 0;
    }
    if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) { printf("ppkg: install NAME\n"); return 1; }
        char path[256];
        if (!find_pkg(argv[2], path, sizeof(path))) { printf("ppkg: unknown package %s\n", argv[2]); return 1; }
        printf("ppkg: installing %s from %s\n", argv[2], path);
        if (extract_tar(path) != 0) return 1;
        printf("ppkg: installed %s\n", argv[2]);
        return 0;
    }
    printf("ppkg: unknown command %s\n", argv[1]);
    return 1;
}
