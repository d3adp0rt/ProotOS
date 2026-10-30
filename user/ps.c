#include "libc.h"
int main(void) {
    char buf[2048];
    long n = getprocs(buf, sizeof(buf));
    if (n > 0)
        write(1, buf, (size_t)n);
    else
        printf("ps: no processes\n");
    return 0;
}
