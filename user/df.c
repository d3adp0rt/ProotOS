#include "libc.h"
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/";
    struct statfs st;
    if (statfs(path, &st) != 0) {
        printf("df: statfs failed\n");
        return 1;
    }
    printf("Filesystem %s: block=%u total=%llu free=%llu\n", path, st.block_size,
           (unsigned long long)st.total_blocks, (unsigned long long)st.free_blocks);
    return 0;
}
