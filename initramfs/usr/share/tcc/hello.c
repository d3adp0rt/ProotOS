int printf(const char *, ...);
double sqrt(double);

static int fib(int n) {
    if (n < 2)
        return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    printf("tcc: hello from compiled C\n");
    for (int i = 1; i <= 12; i++)
        printf("tcc: fib(%d) = %d\n", i, fib(i));
    double x = 2.0;
    printf("tcc: sqrt(2) = %.5f\n", sqrt(x));
    return 0;
}
