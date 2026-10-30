#include "libc.h"
int main(int argc, char **argv) {
    if (argc < 2) { printf("ping: usage: ping IP\n"); return 1; }
    unsigned ip = inet_addr(argv[1]);
    int rc = net_ping_ip(ip);
    printf("ping %s: %s\n", argv[1], rc == 0 ? "reply" : "timeout");
    return rc == 0 ? 0 : 1;
}
