int ilog(int n) {
    int c = 0;
    for (int x = n; x > 1; x >>= 1) c++;
    for (int y = 1; y < n; y <<= 1) c++;
    for (int z = n; z > 0; z = z / 2) c++;
    for (int w = 1; w < n; w = w * 2) c++;
    return c;
}
