#include "libc.h"
int main(int argc, char **argv) {
    if (argc != 3) {
        printf("mv: usage: mv SRC DST\n");
        return 1;
    }
    int in = open(argv[1], 0, 0);
    if (in < 0) {
        printf("mv: cannot open %s\n", argv[1]);
        return 1;
    }
    int out = open(argv[2], 0x242, 0644);
    if (out < 0) {
        close(in);
        return 1;
    }
    char buf[512];
    long n;
    while ((n = read(in, buf, sizeof(buf))) > 0)
        write(out, buf, (size_t)n);
    close(in);
    close(out);
    if (unlink(argv[1]) != 0)
        return 1;
    return 0;
}
