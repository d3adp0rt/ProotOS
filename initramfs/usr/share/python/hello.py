print("MicroPython on ProotOS")
def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)
print("fib:", [fib(i) for i in range(1, 13)])
print("sum 1..100 =", sum(range(1, 101)))
print("pi ~ %.5f" % 3.14159265)
d = {"prootos": "os", "python": "micropython"}
print("dict:", sorted(d.items()))
print("words:", "one two three".split())
