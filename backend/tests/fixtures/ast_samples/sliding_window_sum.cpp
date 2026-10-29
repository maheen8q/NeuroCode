int maxWindowSum(vector<int>& a, int k) {
    int n = a.size();
    int cur = 0;
    for (int i = 0; i < k; i++) cur += a[i];
    int best = cur;
    for (int i = k; i < n; i++) {
        cur += a[i];
        cur -= a[i - k];
        best = max(best, cur);
    }
    return best;
}
