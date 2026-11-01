#include <bits/stdc++.h>
using namespace std;

struct Graph {
    int n;
    vector<vector<int>> adj;
    Graph(int n) : n(n), adj(n) {}
    void add(int u, int v) { adj[u].push_back(v); adj[v].push_back(u); }
    void dump() { for (int u = 0; u < n; u++) { cout << u << ":"; for (int v : adj[u]) cout << " " << v; cout << "\n"; } }
    int bfs(int s) {
        vector<int> d(n, -1);
        queue<int> q;
        d[s] = 0; q.push(s);
        while (!q.empty()) {
            int u = q.front(); q.pop();
            for (int v : adj[u]) if (d[v] < 0) { d[v] = d[u] + 1; q.push(v); }
        }
        return *max_element(d.begin(), d.end());
    }
};

int main() {
    int n, m, mode;
    cin >> n >> m >> mode;
    Graph g(n);
    for (int i = 0; i < m; i++) { int u, v; cin >> u >> v; g.add(u, v); }
    switch (mode) {
        case 1: cout << g.bfs(0) << "\n"; break;
        case 2: g.dump(); break;
        default: cout << "?";
    }
    string s = "cin cout";
    return 0;
}
