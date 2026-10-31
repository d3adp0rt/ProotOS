#include "libc.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("hexdump: FILE [offset] [length]\n");
        return 1;
    }
    int fd = open(argv[1], 0, 0);
    if (fd < 0) {
        printf("hexdump: cannot open %s\n", argv[1]);
        return 1;
    }
    long off = argc > 2 ? (long)atoi(argv[2]) : 0;
    long len = argc > 3 ? (long)atoi(argv[3]) : 256;
    if (off && lseek(fd, off, 0) < 0) {
        printf("hexdump: seek failed\n");
        close(fd);
        return 1;
    }
    unsigned char buf[16];
    long done = 0;
    while (done < len) {
        long n = read(fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        printf("%08lx  ", (unsigned long)(off + done));
        for (long i = 0; i < 16; i++) {
            if (i < n) printf("%02x ", buf[i]);
            else printf("   ");
        }
        printf(" |");
        for (long i = 0; i < n; i++)
            putchar(buf[i] >= 32 && buf[i] < 127 ? buf[i] : '.');
        printf("|\n");
        done += n;
    }
    close(fd);
    return 0;
}
