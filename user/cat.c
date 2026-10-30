#include "libc.h"
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("cat: usage: cat FILE...\n");
        return 1;
    }
    char buf[256];
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], 0, 0);
        if (fd < 0) {
            printf("cat: cannot open %s\n", argv[i]);
            continue;
        }
        long n;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            write(1, buf, (size_t)n);
        close(fd);
    }
    return 0;
}
