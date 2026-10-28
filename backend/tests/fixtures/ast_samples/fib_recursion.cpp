int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
int tri(int n) { return n == 0 ? 0 : n + tri(n - 1); }
long long power(long long b, int e) {
    if (e == 0) return 1;
    long long h = power(b, e / 2);
    return e % 2 ? h * h * b : h * h;
}
