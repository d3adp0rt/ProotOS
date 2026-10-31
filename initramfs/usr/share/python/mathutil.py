def fact(n):
    r = 1
    for i in range(2, n + 1):
        r *= i
    return r

def fib(n):
    a, b = 0, 1
    out = []
    for _ in range(n):
        out.append(a)
        a, b = b, a + b
    return out
