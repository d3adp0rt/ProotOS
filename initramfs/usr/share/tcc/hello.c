#include <stdio.h>
#include <math.h>

static int fib(int n) {
    if (n < 2)
        return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    printf("tcc: hello from compiled C\n");
    for (int i = 1; i <= 12; i++)
        printf("tcc: fib(%d) = %d\n", i, fib(i));
    printf("tcc: sqrt(2) = %.5f\n", sqrt(2.0));
    return 0;
}
