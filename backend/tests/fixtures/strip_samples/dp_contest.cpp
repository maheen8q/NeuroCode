#include <iostream>
#include <vector>
using namespace std;
long long dp[1005][1005];
int main() {
    ios::sync_with_stdio(false);
    cin.tie(0);
    int n;
    cin >> n;
    vector<int> w(n);
    for (int i = 0; i < n; i++) cin >> w[i];
    for (int i = 1; i <= n; i++)
        for (int j = 0; j <= n; j++) {
            dp[i][j] = dp[i - 1][j];
            if (j >= w[i - 1]) dp[i][j] = max(dp[i][j], dp[i - 1][j - w[i - 1]] + w[i - 1]);
        }
    cout << dp[n][n] << "\n";
    return 0;
}
