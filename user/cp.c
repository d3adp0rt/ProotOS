#include "libc.h"
static int copy(const char *src, const char *dst) {
    int in = open(src, 0, 0);
    if (in < 0) {
        printf("cp: cannot open %s\n", src);
        return 1;
    }
    int out = open(dst, 0x242, 0644);
    if (out < 0) {
        printf("cp: cannot create %s\n", dst);
        close(in);
        return 1;
    }
    char buf[512];
    long n;
    int rc = 0;
    while ((n = read(in, buf, sizeof(buf))) > 0)
        if (write(out, buf, (size_t)n) != n) {
            rc = 1;
            break;
        }
    if (n < 0)
        rc = 1;
    close(in);
    close(out);
    return rc;
}
int main(int argc, char **argv) {
    if (argc != 3) {
        printf("cp: usage: cp SRC DST\n");
        return 1;
    }
    return copy(argv[1], argv[2]);
}
