#include "libc.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    struct utsname u;
    if (uname(&u) != 0) {
        printf("uname: syscall failed\n");
        return 1;
    }
    int show_s = 0, show_n = 0, show_r = 0, show_v = 0, show_m = 0;
    if (argc < 2) {
        show_s = 1;
    } else {
        for (int i = 1; i < argc; i++) {
            const char *a = argv[i];
            if (a[0] != '-') {
                show_s = 1;
                continue;
            }
            for (int j = 1; a[j]; j++) {
                if (a[j] == 'a') {
                    show_s = show_n = show_r = show_v = show_m = 1;
                } else if (a[j] == 's') show_s = 1;
                else if (a[j] == 'n') show_n = 1;
                else if (a[j] == 'r') show_r = 1;
                else if (a[j] == 'v') show_v = 1;
                else if (a[j] == 'm') show_m = 1;
                else {
                    printf("uname: unknown option -%c\n", a[j]);
                    return 1;
                }
            }
        }
    }
    int first = 1;
#define FIELD(flag, val) do { if (flag) { if (!first) putchar(' '); printf("%s", (val)); first = 0; } } while (0)
    FIELD(show_s, u.sysname);
    FIELD(show_n, u.nodename);
    FIELD(show_r, u.release);
    FIELD(show_v, u.version);
    FIELD(show_m, u.machine);
#undef FIELD
    putchar('\n');
    return 0;
}
