-- Problem catalog. Idempotent: re-running refreshes the rows without touching jobs.
-- Operations match engine/verify/TestGenerator.hpp (the Oracle works from `operation`).
--
-- UNCONFIRMED (asked Ansh): is reference_code a solve() function or a full stdin program?
-- Using the README convention, solve(vector<int>& a, int k), until he answers.
-- The Oracle does not read this column today; it is kept for display and future use.
-- fibonacci max_n is 92: F(93) does not fit in 64 bits (the Oracle's range is 0..92).

INSERT INTO problems (id, title, description, input_type, output_type, operation, max_n, optimal_time, reference_code)
VALUES ('pair_sum', 'Pair with given sum', 'Given an integer array a and an integer k, decide whether there are indices i < j with a[i] + a[j] == k.', 'int_array', 'bool', 'pair_sum', 100000, 'O(n)',
'bool solve(vector<int>& a, int k) {
    unordered_set<long long> seen;
    for (int x : a) {
        if (seen.count((long long)k - x)) return true;
        seen.insert(x);
    }
    return false;
}')
ON CONFLICT(id) DO UPDATE SET title=excluded.title, description=excluded.description,
  input_type=excluded.input_type, output_type=excluded.output_type, operation=excluded.operation,
  max_n=excluded.max_n, optimal_time=excluded.optimal_time, reference_code=excluded.reference_code;

INSERT INTO problems (id, title, description, input_type, output_type, operation, max_n, optimal_time, reference_code)
VALUES ('linear_search', 'Linear search', 'Given an integer array a and a target, return an index holding the target, or -1 if it is absent.', 'int_array', 'int', 'search', 100000, 'O(n)',
'int solve(vector<int>& a, int target) {
    for (int i = 0; i < (int)a.size(); i++)
        if (a[i] == target) return i;
    return -1;
}')
ON CONFLICT(id) DO UPDATE SET title=excluded.title, description=excluded.description,
  input_type=excluded.input_type, output_type=excluded.output_type, operation=excluded.operation,
  max_n=excluded.max_n, optimal_time=excluded.optimal_time, reference_code=excluded.reference_code;

INSERT INTO problems (id, title, description, input_type, output_type, operation, max_n, optimal_time, reference_code)
VALUES ('binary_search', 'Binary search in a sorted array', 'Given an array a sorted in ascending order and a target, return an index holding the target, or -1 if it is absent.', 'int_array', 'int', 'search_sorted', 100000, 'O(log n)',
'int solve(vector<int>& a, int target) {
    int lo = 0, hi = (int)a.size() - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] == target) return mid;
        if (a[mid] < target) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}')
ON CONFLICT(id) DO UPDATE SET title=excluded.title, description=excluded.description,
  input_type=excluded.input_type, output_type=excluded.output_type, operation=excluded.operation,
  max_n=excluded.max_n, optimal_time=excluded.optimal_time, reference_code=excluded.reference_code;

INSERT INTO problems (id, title, description, input_type, output_type, operation, max_n, optimal_time, reference_code)
VALUES ('range_sum', 'Range sum queries', 'Given an integer array a and q queries (l, r), answer the sum of a[l..r] (inclusive, 0-based) for each query.', 'int_array', 'array', 'range_sum', 100000, 'O(n + q)',
'vector<long long> solve(vector<int>& a, vector<pair<int,int>>& queries) {
    vector<long long> prefix(a.size() + 1, 0);
    for (size_t i = 0; i < a.size(); i++) prefix[i + 1] = prefix[i] + a[i];
    vector<long long> out;
    for (auto& q : queries) out.push_back(prefix[q.second + 1] - prefix[q.first]);
    return out;
}')
ON CONFLICT(id) DO UPDATE SET title=excluded.title, description=excluded.description,
  input_type=excluded.input_type, output_type=excluded.output_type, operation=excluded.operation,
  max_n=excluded.max_n, optimal_time=excluded.optimal_time, reference_code=excluded.reference_code;

INSERT INTO problems (id, title, description, input_type, output_type, operation, max_n, optimal_time, reference_code)
VALUES ('fibonacci', 'Fibonacci number', 'Return F(n) with F(0) = 0 and F(1) = 1, for 0 <= n <= 92.', 'int', 'int', 'fibonacci', 92, 'O(n)',
'long long solve(int n) {
    long long a = 0, b = 1;
    for (int i = 1; i < n; i++) { long long t = a + b; a = b; b = t; }
    return n <= 0 ? 0 : b;
}')
ON CONFLICT(id) DO UPDATE SET title=excluded.title, description=excluded.description,
  input_type=excluded.input_type, output_type=excluded.output_type, operation=excluded.operation,
  max_n=excluded.max_n, optimal_time=excluded.optimal_time, reference_code=excluded.reference_code;
