#include "libc.h"
int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "example.com";
    const char *path = argc > 2 ? argv[2] : "/";
    char buf[8192];
    int n = net_http_get(host, path, buf, sizeof(buf));
    if (n == -2) { printf("wget: HTTPS redirect requires TLS (not implemented yet)\n"); return 2; }
    if (n <= 0) { printf("wget: failed (%d)\n", n); return 1; }
    printf("wget: %d bytes\n", n);
    write(1, buf, (size_t)n);
    return 0;
}
