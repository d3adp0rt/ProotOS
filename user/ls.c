#include "libc.h"
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : ".";
    char name[128];
    int type;
    for (unsigned i = 0;; i++) {
        type = readdir(path, i, name, sizeof(name));
        if (type < 0)
            break;
        printf("%s%s\n", name, type == 2 ? "/" : "");
    }
    return 0;
}
