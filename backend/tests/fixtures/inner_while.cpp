struct Acc { vector<int> data; set<int> seen; };
long long triSum(int a[], int n) {
    long long s = 0;
    for (size_t i = 0; i < (size_t)n; i++)
        for (size_t j = i + 1; j < (size_t)n; j++)
            s += (long long)a[i] * a[j] % 1000003;
    int mid = n / 2;
    s += a[mid] + a[n - 1] - a[0];
    return s;
}
