#include <stdio.h>
#include <stdlib.h>

int main(void) {
    size_t n = 12 * 1024 * 1024;
    char *p = malloc(n);
    if (!p) {
        printf("big malloc failed\n");
        return 1;
    }
    for (size_t i = 0; i < n; i += 4096)
        p[i] = (char)i;
    printf("big malloc ok: %u MiB\n", (unsigned)(n >> 20));
    free(p);
    return 0;
}
