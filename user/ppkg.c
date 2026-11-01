#include <stdio.h>
#include "libc.h"

static unsigned oct(const char *p) {
    unsigned v = 0;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '7') v = (v << 3) + (unsigned)(*p++ - '0');
    return v;
}

static int remove_tar(const char *path) {
    int fd = open(path, 0, 0);
    if (fd < 0) return 1;
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
        if (type != '5' && name[0]) unlink(name);
        unsigned skip = size + ((512 - (size % 512)) % 512);
        unsigned char tmp[512];
        while (skip) { unsigned c = skip > sizeof(tmp) ? sizeof(tmp) : skip; if (read(fd,tmp,c)<=0) break; skip -= c; }
    }
    close(fd);
    return 0;
}

static int extract_tar_to(const char *path, const char *prefix) {
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
        char full[288];
        full[0] = 0;
        strcat(full, prefix);
        strcat(full, name);
        if (prefix[0] && strncmp(name, "packages/", 9) == 0) {
            char dir[288]; dir[0] = 0; strcat(dir, prefix); strcat(dir, "packages");
            mkdir(dir, 0755);
        }
        if (strncmp(name, "usr/", 4) == 0) { mkdir("usr", 0755); mkdir("usr/bin", 0755); }
        if (strncmp(name, "packages/", 9) == 0 && !prefix[0]) mkdir("packages", 0755);
        if (type == '5') {
            mkdir(full, 0755);
            continue;
        }
        int out = open(full, 0x242, 0644);
        if (out < 0) { printf("ppkg: cannot create %s\n", full); return 1; }
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

static int extract_tar(const char *path) { return extract_tar_to(path, ""); }

/* Update only the index from the registry bundle. */
static int extract_index(const char *path) {
    int fd = open(path, 0, 0);
    if (fd < 0) return 1;
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
        unsigned pad = (512 - (size % 512)) % 512;
        if (strcmp(name, "index") == 0 && type != '5') {
            int out = open("/var/lib/ppkg/index", 0x242, 0644);
            if (out < 0) { close(fd); return 1; }
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
            close(fd);
            return 0;
        }
        unsigned skip = size + pad;
        unsigned char tmp[512];
        while (skip) {
            unsigned c = skip > sizeof(tmp) ? sizeof(tmp) : skip;
            if (read(fd, tmp, c) <= 0) break;
            skip -= c;
        }
    }
    close(fd);
    return 1;
}

static int is_url(const char *s) {
    return strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0;
}

static void cache_path(const char *name, char *out, size_t cap) {
    out[0] = 0;
    strcat(out, "/var/lib/ppkg/packages/");
    size_t n = strlen(out);
    for (const char *p = name; *p && n + 1 < cap; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')
            out[n++] = c;
    }
    out[n] = 0;
    strcat(out, ".pk");
}

static int fetch_package(const char *name, const char *url, char *out_path, size_t cap) {
    const char *p = url;
    int https = 1;
    if (strncmp(p, "https://", 8) == 0) p += 8;
    else if (strncmp(p, "http://", 7) == 0) { p += 7; https = 0; }
    char host[128];
    size_t hn = 0;
    while (*p && *p != '/' && hn + 1 < sizeof(host)) host[hn++] = *p++;
    host[hn] = 0;
    const char *path = (*p == '/') ? p : "/";
    if (!hn) return -1;
    char *buf = malloc(1024 * 1024);
    if (!buf) return -1;
    int n = -1;
    for (int attempt = 0; attempt < 4 && n <= 0; attempt++) {
        n = https ? net_https_get(host, path, buf, 1024 * 1024)
                  : net_http_get(host, path, buf, 1024 * 1024);
    }
    if (n <= 0) { free(buf); return -1; }
    mkdir("/var/lib/ppkg/packages", 0755);
    cache_path(name, out_path, cap);
    int fd = open(out_path, 0x242, 0644);
    if (fd < 0) { free(buf); return -1; }
    int ok = write(fd, buf, (size_t)n) == n;
    close(fd);
    free(buf);
    return ok ? 0 : -1;
}

#define REGISTRY "https://github.com/d3adp0rt/prootos-registry"

static int find_pkg(const char *want, char *path, size_t cap, char *deps, size_t dcap);

#define MAX_INSTALL_DEPTH 8
#define MAX_TRACKED 32
static char g_installed[MAX_TRACKED][32];
static int g_installed_count;

static int already_installed(const char *name) {
    for (int i = 0; i < g_installed_count; i++)
        if (strcmp(g_installed[i], name) == 0) return 1;
    return 0;
}

static void mark_installed(const char *name) {
    if (g_installed_count >= MAX_TRACKED) return;
    size_t n = 0;
    while (name[n] && n < 31) { g_installed[g_installed_count][n] = name[n]; n++; }
    g_installed[g_installed_count][n] = 0;
    g_installed_count++;
}

static int install_tree(const char *name, int depth) {
    if (depth > MAX_INSTALL_DEPTH) { printf("ppkg: dependency too deep at %s\n", name); return 1; }
    if (already_installed(name)) return 0;
    char path[256], deps[128];
    if (!find_pkg(name, path, sizeof(path), deps, sizeof(deps))) {
        printf("ppkg: unknown package %s\n", name);
        return 1;
    }
    mark_installed(name);
    if (deps[0] && strcmp(deps, "-") != 0) {
        char one[64];
        const char *p = deps;
        while (*p) {
            size_t n = 0;
            while (*p && *p != ',' && n < sizeof(one) - 1) one[n++] = *p++;
            one[n] = 0;
            if (*p == ',') p++;
            if (!one[0]) continue;
            if (install_tree(one, depth + 1) != 0) return 1;
        }
    }
    char local[256];
    const char *src = path;
    if (is_url(path)) {
        printf("ppkg: fetching %s\n", name);
        if (fetch_package(name, path, local, sizeof(local)) != 0) {
            char fallback[256];
            snprintf(fallback, sizeof(fallback), "/packages/%s.pk", name);
            int fd = open(fallback, 0, 0);
            if (fd >= 0) { close(fd); strcpy(local, fallback); }
            else { printf("ppkg: download failed for %s\n", name); return 1; }
        }
        src = local;
    }
    int probe = open(src, 0, 0);
    if (probe < 0) {
        char fallback[256];
        snprintf(fallback, sizeof(fallback), "/packages/%s.pk", name);
        probe = open(fallback, 0, 0);
        if (probe >= 0) {
            close(probe);
            strcpy(local, fallback);
            src = local;
        }
    } else {
        close(probe);
    }
    printf("ppkg: installing %s from %s\n", name, src);
    if (extract_tar(src) != 0) return 1;
    printf("ppkg: installed %s\n", name);
    return 0;
}

static int find_pkg(const char *want, char *path, size_t cap, char *deps, size_t dcap) {
    (void)cap;
    (void)dcap;
    if (deps && dcap) deps[0] = 0;
    int fd = open("/var/lib/ppkg/index", 0, 0);
    if (fd < 0) return 0;
    char line[256];
    int n = 0;
    while (read(fd, &line[n], 1) == 1) {
        if (line[n] == '\n') {
            line[n] = 0;
            char *name = line;
            char *sp1 = name; while (*sp1 && *sp1 != ' ') sp1++;
            if (*sp1) *sp1++ = 0;
            char *ver = sp1; char *sp2 = ver; while (*sp2 && *sp2 != ' ') sp2++;
            if (*sp2) *sp2++ = 0;
            char *dlist = sp2; char *sp3 = dlist; while (*sp3 && *sp3 != ' ') sp3++;
            if (*sp3) *sp3++ = 0;
            (void)ver;
            if (strcmp(name, want) == 0) {
                strcpy(path, sp3);
                if (deps && dcap) {
                    size_t dl = 0;
                    while (dlist[dl] && dl + 1 < dcap) { deps[dl] = dlist[dl]; dl++; }
                    deps[dl] = 0;
                }
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
    if (argc < 2) { printf("ppkg: usage: ppkg list|search TERM|update|install NAME|remove NAME\n"); return 1; }
    if (strcmp(argv[1], "search") == 0) {
        if (argc < 3) { printf("ppkg: search TERM\n"); return 1; }
        int fd = open("/var/lib/ppkg/index", 0, 0);
        if (fd < 0) { printf("ppkg: no index\n"); return 1; }
        char line[256]; int n = 0;
        while (read(fd, &line[n], 1) == 1) {
            if (line[n] == '\n') {
                line[n] = 0;
                if (line[0] && line[0] != '#' && strstr(line, argv[2])) printf("%s\n", line);
                n = 0;
            } else if (n < (int)sizeof(line) - 1) n++;
        }
        close(fd);
        return 0;
    }
    if (strcmp(argv[1], "update") == 0) {
        printf("ppkg: updating from %s\n", REGISTRY);
        char *bundle = malloc(131072);
        if (!bundle) return 1;
        int n = -1;
        for (int attempt = 0; attempt < 5 && n <= 0; attempt++)
            n = net_https_get("raw.githubusercontent.com",
                              "/d3adp0rt/prootos-registry/main/index", bundle, 131072);
        if (n <= 0) { printf("ppkg: update failed (%d)\n", n); free(bundle); return 1; }
        int fd = open("/var/lib/ppkg/index", 0x242, 0644);
        if (fd < 0) { free(bundle); return 1; }
        if (write(fd, bundle, (size_t)n) != n) { close(fd); free(bundle); return 1; }
        close(fd);
        free(bundle);
        printf("ppkg: registry updated (index only)\n");
        return 0;
    }
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
        return install_tree(argv[2], 0);
    }
    if (strcmp(argv[1], "remove") == 0) {
        if (argc < 3) { printf("ppkg: remove NAME\n"); return 1; }
        char path[256], deps[128];
        if (!find_pkg(argv[2], path, sizeof(path), deps, sizeof(deps))) {
            printf("ppkg: unknown package %s\n", argv[2]);
            return 1;
        }
        char local[256];
        const char *src = path;
        if (is_url(path)) {
            cache_path(argv[2], local, sizeof(local));
            src = local;
        }
        if (remove_tar(src) != 0) return 1;
        printf("ppkg: removed %s\n", argv[2]);
        return 0;
    }
    printf("ppkg: unknown command %s\n", argv[1]);
    return 1;
}
