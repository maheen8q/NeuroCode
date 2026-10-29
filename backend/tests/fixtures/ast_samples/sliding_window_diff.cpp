int longestSmall(vector<int>& a, int limit) {
    int l = 0, sum = 0, best = 0;
    for (int r = 0; r < (int)a.size(); r++) {
        sum += a[r];
        while (sum > limit && l <= r) {
            sum -= a[l];
            l++;
        }
        best = max(best, r - l + 1);
    }
    return best;
}
