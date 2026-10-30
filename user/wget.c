#include "libc.h"
int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "example.com";
    if (strncmp(host, "https://", 8) == 0) host += 8;
    else if (strncmp(host, "http://", 7) == 0) host += 7;
    const char *path = argc > 2 ? argv[2] : "/";
    char buf[8192];
    int n = net_https_get(host, path, buf, sizeof(buf));
    const char *proto = "https";
    if (n <= 0) n = net_https_get(host, path, buf, sizeof(buf));
    if (n <= 0) {
        n = net_http_get(host, path, buf, sizeof(buf));
        proto = "http";
    }
    if (n <= 0) { printf("wget: failed (%d)\n", n); return 1; }
    printf("wget: %d bytes (%s)\n", n, proto);
    write(1, buf, (size_t)n);
    return 0;
}
