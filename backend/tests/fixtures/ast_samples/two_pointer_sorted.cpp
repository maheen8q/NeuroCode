bool hasPair(vector<int> a, int target) {
    sort(a.begin(), a.end());
    int lo = 0, hi = (int)a.size() - 1;
    while (lo < hi) {
        int s = a[lo] + a[hi];
        if (s == target) return true;
        else if (s < target) ++lo;
        else --hi;
    }
    return false;
}
