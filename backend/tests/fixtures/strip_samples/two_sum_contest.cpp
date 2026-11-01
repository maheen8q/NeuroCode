#include <bits/stdc++.h>
using namespace std;
int main() {
    int n, k;
    scanf("%d %d", &n, &k);
    unordered_set<int> seen;
    bool found = false;
    for (int i = 0; i < n; i++) {
        int x;
        scanf("%d", &x);
        if (seen.count(k - x)) found = true;
        seen.insert(x);
    }
    puts(found ? "YES" : "NO");
    return 0;
}
