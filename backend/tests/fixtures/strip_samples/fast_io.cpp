#include <bits/stdc++.h>
using namespace std;
typedef long long ll;

int solve(vector<int>& a, int k) {
    int l = 0, r = (int)a.size() - 1;
    while (l < r) {
        int s = a[l] + a[r];
        if (s == k) return 1;
        if (s < k) l++; else r--;
    }
    return 0;
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(nullptr);
    int t;
    cin >> t;
    while (t--) {
        int n, k;
        cin >> n >> k;
        vector<int> a(n);
        for (int i = 0; i < n; i++) cin >> a[i];
        sort(a.begin(), a.end());
        cout << (solve(a, k) ? "YES" : "NO") << "\n";
    }
    return 0;
}
