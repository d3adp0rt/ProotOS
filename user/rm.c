#include "libc.h"
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("rm: usage: rm FILE...\n");
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++)
        if (unlink(argv[i]) != 0) {
            printf("rm: %s failed\n", argv[i]);
            rc = 1;
        }
    return rc;
}
