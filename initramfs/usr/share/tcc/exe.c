#include <stdio.h>

void _exit(int code);

static int square(int x) { return x * x; }

int main(void) {
    printf("tcc: standalone executable works\n");
    for (int i = 1; i <= 5; i++)
        printf("tcc: square(%d) = %d\n", i, square(i));
    printf("tcc: sqrt(2) = %.5f\n", 1.41421356);
    return 0;
}

void _start(void) {
    _exit(main());
}
