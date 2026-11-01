#include <stdio.h>
#include <algorithm>
int a[100005];
int main() {
    int n;
    scanf("%d", &n);
    for (int i = 0; i < n; i++) scanf("%d", &a[i]);
    std::sort(a, a + n);
    int lo = 0, hi = n - 1, ans = -1, x;
    scanf("%d", &x);
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (a[mid] == x) { ans = mid; break; }
        else if (a[mid] < x) lo = mid + 1;
        else hi = mid - 1;
    }
    printf("%d\n", ans);
    return 0;
}
