int stlMix(vector<int>& a, int x) {
    sort(a.begin(), a.end());
    auto it = lower_bound(a.begin(), a.end(), x);
    auto jt = upper_bound(a.begin(), a.end(), x);
    auto f = find(a.begin(), a.end(), x);
    auto mx = max_element(a.begin(), a.end());
    set<int> s;
    map<int, int> m;
    unordered_map<int, int> um;
    priority_queue<int> pq;
    stack<int> st;
    pair<int, int> pr = {1, 2};
    string name = "abc";
    for (int v : a) { s.insert(v); m[v]++; um[v] += 2; pq.push(v); st.push(v); }
    return (int)(it - a.begin()) + (int)(jt - a.begin()) + (f != a.end()) + *mx + (int)s.count(x) + pr.first + (int)name.size();
}
