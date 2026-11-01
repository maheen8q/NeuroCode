int countRuns(vector<int>& a) {
    int i = 0, runs = 0, n = a.size();
    while (i < n) {
        int j = i;
        while (j < n && a[j] == a[i]) j++;
        runs++;
        i = j;
    }
    int p = 0;
    for (int t = 0; t < n; t++) {
        while (p < n && a[p] < a[t]) p++;
    }
    return runs + p;
}
