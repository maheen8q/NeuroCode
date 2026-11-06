#include "engine/verify/TestGenerator.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <random>

namespace nc::verify {

namespace {

constexpr int kMaxFib = 90;                                // F(90) is the largest the oracle trusts in tests

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Same seed, same numbers on every standard library (std::uniform_int_distribution is not portable).
class Rng {
public:
    explicit Rng(uint64_t seed) : e_(seed) {}
    long long range(long long lo, long long hi) {
        if (hi <= lo) return lo;
        return lo + static_cast<long long>(e_() % static_cast<uint64_t>(hi - lo + 1));
    }
    bool coin() { return (e_() & 1u) != 0; }
private:
    std::mt19937_64 e_;
};

std::string joinInts(const std::vector<int>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? " " : "") + std::to_string(v[i]);
    return s;
}

std::string showArray(const std::vector<int>& a) {
    std::string s = "[";
    if (a.size() <= 12) {
        for (size_t i = 0; i < a.size(); ++i) s += (i ? ", " : "") + std::to_string(a[i]);
        return s + "]";
    }
    for (size_t i = 0; i < 8; ++i) s += std::to_string(a[i]) + ", ";
    s += "..., " + std::to_string(a[a.size() - 1]) + "] (" + std::to_string(a.size()) + " elements)";
    return s;
}

std::string showQueries(const std::vector<Query>& q) {
    std::string s = "[";
    const size_t shown = std::min<size_t>(q.size(), 8);
    for (size_t i = 0; i < shown; ++i) s += (i ? ", (" : "(") + std::to_string(q[i].l) + ", " + std::to_string(q[i].r) + ")";
    if (q.size() > shown) s += ", ... (" + std::to_string(q.size()) + " queries)";
    return s + "]";
}

} // namespace

Operation parseOperation(const std::string& name) {
    const std::string n = lower(name);
    if (n == "pair_sum" || n == "two_sum" || n == "pair_with_sum" || n == "pair_with_given_sum") return Operation::PairSum;
    if (n == "search" || n == "linear_search" || n == "find" || n == "find_index") return Operation::Search;
    if (n == "search_sorted" || n == "binary_search" || n == "sorted_search" || n == "find_sorted") return Operation::SearchSorted;
    if (n == "range_sum" || n == "prefix_sum" || n == "range_query_sum" || n == "subarray_sum_queries") return Operation::RangeSum;
    if (n == "fibonacci" || n == "fib" || n == "nth_fibonacci") return Operation::Fibonacci;
    if (n == "k_smallest" || n == "k_smallest_elements" || n == "kth_smallest") return Operation::KSmallest;
    return Operation::Unknown;
}

const char* toString(Operation op) {
    switch (op) {
        case Operation::PairSum: return "pair_sum";
        case Operation::Search: return "search";
        case Operation::SearchSorted: return "search_sorted";
        case Operation::RangeSum: return "range_sum";
        case Operation::Fibonacci: return "fibonacci";
        case Operation::KSmallest: return "k_smallest";
        case Operation::Unknown: break;
    }
    return "unknown";
}

std::vector<std::string> supportedOperations() {
    return {"pair_sum", "search", "search_sorted", "range_sum", "fibonacci"};
}

TestGenerator::TestGenerator(const ProblemSpec& spec, GeneratorOptions options)
    : spec_(spec), opt_(options), op_(parseOperation(spec.operation)) {
    if (op_ == Operation::Unknown) {
        note_ = "operation '" + spec.operation + "' is not supported for differential testing";
        return;
    }
    // Type strings come from the catalog; a mismatch is worth a warning but never silently changes the operation.
    const std::string in = lower(spec.input_type), out = lower(spec.output_type);
    const bool in_ok = in.empty() || in == "int_array" || in == "int" || in == "int_array_int" || op_ == Operation::Fibonacci;
    bool out_ok = out.empty();
    switch (op_) {
        case Operation::PairSum: out_ok = out_ok || out == "bool"; break;
        case Operation::Search:
        case Operation::SearchSorted: out_ok = out_ok || out == "int"; break;
        case Operation::RangeSum: out_ok = out_ok || out == "int_array" || out == "long_array"; break;
        case Operation::Fibonacci: out_ok = out_ok || out == "int" || out == "long" || out == "long long"; break;
        case Operation::Unknown: break;
    }
    if (!in_ok || !out_ok)
        note_ = std::string("input/output types (") + spec.input_type + " -> " + spec.output_type + ") differ from what '" +
                toString(op_) + "' expects; testing it as " + toString(op_);
}

bool TestGenerator::isValid(const TestCase& tc) const {
    const long long max_n = spec_.max_n > 0 ? spec_.max_n : 100000;
    switch (op_) {
        case Operation::PairSum:
        case Operation::Search:
            return static_cast<long long>(tc.a.size()) <= max_n && tc.queries.empty();
        case Operation::SearchSorted:
            return static_cast<long long>(tc.a.size()) <= max_n && tc.queries.empty() && std::is_sorted(tc.a.begin(), tc.a.end());
        case Operation::RangeSum:
            if (static_cast<long long>(tc.a.size()) > max_n) return false;
            for (const Query& q : tc.queries)
                if (q.l < 0 || q.r < q.l || static_cast<size_t>(q.r) >= tc.a.size()) return false;
            return true;
        case Operation::Fibonacci:
            return tc.a.empty() && tc.queries.empty() && tc.param >= 0 && tc.param <= kMaxFib;
        case Operation::Unknown:
            break;
    }
    return false;
}

void TestGenerator::finalize(TestCase& tc) const {
    const std::string n = std::to_string(tc.a.size());
    switch (op_) {
        case Operation::PairSum:
            tc.stdin_text = n + " " + std::to_string(tc.param) + "\n" + joinInts(tc.a) + "\n";
            tc.display = "a = " + showArray(tc.a) + "\nk = " + std::to_string(tc.param);
            break;
        case Operation::Search:
        case Operation::SearchSorted:
            tc.stdin_text = n + " " + std::to_string(tc.param) + "\n" + joinInts(tc.a) + "\n";
            tc.display = "a = " + showArray(tc.a) + "\ntarget = " + std::to_string(tc.param);
            break;
        case Operation::RangeSum: {
            tc.stdin_text = n + " " + std::to_string(tc.queries.size()) + "\n" + joinInts(tc.a) + "\n";
            for (const Query& q : tc.queries) tc.stdin_text += std::to_string(q.l) + " " + std::to_string(q.r) + "\n";
            tc.display = "a = " + showArray(tc.a) + "\nqueries = " + showQueries(tc.queries);
            break;
        }
        case Operation::Fibonacci:
            tc.stdin_text = std::to_string(tc.param) + "\n";
            tc.display = "n = " + std::to_string(tc.param);
            break;
        case Operation::KSmallest:
            tc.stdin_text = n + " " + std::to_string(tc.param) + "\n" + joinInts(tc.a) + "\n";
            tc.display = "a = " + showArray(tc.a) + "\nk = " + std::to_string(tc.param);
            break;    
        case Operation::Unknown:
            break;
    }
}

std::vector<TestCase> TestGenerator::generate() const {
    std::vector<TestCase> out;
    if (!supported()) return out;

    const long long max_n = spec_.max_n > 0 ? spec_.max_n : 100000;
    const int large_n = static_cast<int>(std::max<long long>(1, std::min<long long>(opt_.large_n, max_n)));
    Rng rng(opt_.seed);
    const bool sorted_needed = op_ == Operation::SearchSorted;

    auto add = [&](const char* name, const char* category, std::vector<int> a, int param, std::vector<Query> q = {}) {
        TestCase tc;
        tc.name = name;
        tc.category = category;
        tc.a = std::move(a);
        tc.param = param;
        tc.queries = std::move(q);
        if (!isValid(tc)) return;                               // e.g. a descending case for search_sorted
        finalize(tc);
        out.push_back(std::move(tc));
    };
    auto addNamed = [&](std::string name, const char* category, std::vector<int> a, int param, std::vector<Query> q = {}) {
        TestCase tc;
        tc.name = std::move(name);
        tc.category = category;
        tc.a = std::move(a);
        tc.param = param;
        tc.queries = std::move(q);
        if (sorted_needed) std::sort(tc.a.begin(), tc.a.end());
        if (!isValid(tc)) return;
        finalize(tc);
        out.push_back(std::move(tc));
    };

    const int MX = INT_MAX, MN = INT_MIN;

    switch (op_) {
        case Operation::PairSum:
            add("empty", "empty", {}, 0);
            add("single_needs_two_indices", "single", {5}, 10);
            add("single_zero", "single", {0}, 0);
            add("two_match", "edge", {3, 4}, 7);
            add("two_no_match", "edge", {3, 4}, 8);
            add("all_equal_hit", "all_equal", {7, 7, 7, 7}, 14);
            add("all_equal_miss", "all_equal", {7, 7, 7, 7}, 15);
            add("duplicate_pair", "edge", {3, 3}, 6);
            add("zero_pair", "edge", {0, 0}, 0);
            add("negatives", "edge", {-5, -3, 8}, 3);
            add("negative_target", "edge", {-1, -2, 5}, -3);
            add("pair_at_both_ends", "edge", {4, 1, 2, 3, 6}, 10);
            add("sorted_asc_hit", "sorted_asc", {1, 2, 3, 4, 5, 6}, 11);
            add("sorted_asc_miss", "sorted_asc", {1, 2, 3, 4, 5, 6}, 12);
            add("sorted_desc_hit", "sorted_desc", {6, 5, 4, 3, 2, 1}, 3);
            add("sorted_desc_miss", "sorted_desc", {6, 5, 4, 3, 2, 1}, 13);
            add("max_values_wrap", "max_values", {MX, MX}, -2);                // INT_MAX + INT_MAX wraps to -2
            add("min_values_wrap", "max_values", {MN, MN}, 0);                 // INT_MIN + INT_MIN wraps to 0
            add("max_and_min", "max_values", {MX, MN}, -1);
            add("max_plus_one_wrap", "max_values", {MX, 1}, MN);               // wraps to INT_MIN
            add("complement_overflow", "max_values", {MX, 5}, MN + 4);         // k - 5 wraps to INT_MAX: a hash version says true
            add("complement_underflow", "max_values", {1, 2}, MN);
            break;

        case Operation::Search:
        case Operation::SearchSorted:
            addNamed("empty", "empty", {}, 1);
            addNamed("single_hit", "single", {5}, 5);
            addNamed("single_miss", "single", {5}, 4);
            addNamed("first_element", "edge", {7, 1, 2, 3}, 7);
            addNamed("last_element", "edge", {1, 2, 3, 7}, 7);
            addNamed("middle_element", "edge", {1, 2, 7, 8, 9}, 7);
            addNamed("duplicates_any_index", "all_equal", {2, 2, 2, 2}, 2);
            addNamed("all_equal_miss", "all_equal", {4, 4, 4}, 5);
            addNamed("absent_below", "edge", {5, 6, 7}, 1);
            addNamed("absent_above", "edge", {5, 6, 7}, 9);
            addNamed("absent_between", "edge", {1, 3, 5, 7}, 4);
            addNamed("negatives", "edge", {-3, -1, -2}, -1);
            addNamed("max_value_hit", "max_values", {MN, -1, 0, MX}, MX);
            addNamed("min_value_hit", "max_values", {MN, -1, 0, MX}, MN);
            addNamed("max_value_miss", "max_values", {MN, -1, 0, MX - 1}, MX);
            add("sorted_asc_hit", "sorted_asc", {1, 2, 3, 4, 5}, 3);
            add("sorted_desc_hit", "sorted_desc", {5, 4, 3, 2, 1}, 2);           // dropped for search_sorted by isValid
            add("sorted_desc_miss", "sorted_desc", {5, 4, 3, 2, 1}, 9);
            break;

        case Operation::RangeSum:
            add("empty", "empty", {}, 0);
            add("single", "single", {5}, 0, {{0, 0}});
            add("all_equal", "all_equal", {3, 3, 3, 3}, 0, {{0, 3}, {1, 2}, {2, 2}});
            add("sorted_asc", "sorted_asc", {1, 2, 3, 4, 5, 6, 7, 8}, 0, {{0, 7}, {0, 0}, {3, 5}, {7, 7}});
            add("sorted_desc", "sorted_desc", {8, 7, 6, 5, 4, 3, 2, 1}, 0, {{0, 7}, {2, 4}, {6, 7}});
            add("single_element_ranges", "edge", {4, -2, 9, 0}, 0, {{0, 0}, {1, 1}, {2, 2}, {3, 3}});
            add("prefix_and_suffix", "edge", {1, 2, 3, 4, 5}, 0, {{0, 2}, {2, 4}, {0, 4}});
            add("negatives", "edge", {-5, -3, 8, -1}, 0, {{0, 3}, {1, 2}, {0, 0}});
            add("max_values_exceed_int", "max_values", {MX, MX, MX}, 0, {{0, 2}, {0, 1}, {1, 1}});
            add("min_values_exceed_int", "max_values", {MN, MN}, 0, {{0, 1}});
            break;

        case Operation::Fibonacci: {
            const int top = static_cast<int>(std::min<long long>(opt_.max_fibonacci_n, kMaxFib));
            for (int n : {0, 1, 2, 3, 4, 5, 10, 15, 20})
                if (n <= top) add(("n_" + std::to_string(n)).c_str(), n <= 1 ? "single" : "edge", {}, n);
            add("largest_n", "max_values", {}, top);
            break;
        }
        case Operation::KSmallest:
            add("empty", "empty", {}, 0);
            add("k_equals_n", "edge", {5, 3, 1, 4, 2}, 5);
            add("k_equals_1", "edge", {5, 3, 1, 4, 2}, 1);
            add("all_equal", "all_equal", {3, 3, 3, 3}, 2);
            add("sorted_asc", "sorted_asc", {1, 2, 3, 4, 5, 6, 7, 8}, 3);
            add("sorted_desc", "sorted_desc", {8, 7, 6, 5, 4, 3, 2, 1}, 3);
            add("negatives", "edge", {-5, -3, 8, -1, 0}, 2);
            add("max_values", "max_values", {MX, MN, 0, MX, MN}, 3);
            break;
        case Operation::Unknown:
            break;
    }

    // random small: n <= 10, small values, about half of the targets are real
    for (int i = 0; i < opt_.random_small; ++i) {
        const std::string name = "random_small_" + std::to_string(i);
        if (op_ == Operation::Fibonacci) {
            const int top = static_cast<int>(std::min<long long>(opt_.max_fibonacci_n, kMaxFib));
            addNamed(name, "random_small", {}, static_cast<int>(rng.range(0, top)));
            continue;
        }
        const int n = static_cast<int>(rng.range(1, 10));
        std::vector<int> a(static_cast<size_t>(n));
        for (int& x : a) x = static_cast<int>(rng.range(-10, 10));
        if (op_ == Operation::RangeSum) {
            std::vector<Query> q;
            for (int j = 0, qn = static_cast<int>(rng.range(1, 8)); j < qn; ++j) {
                const int l = static_cast<int>(rng.range(0, n - 1));
                q.push_back({l, static_cast<int>(rng.range(l, n - 1))});
            }
            addNamed(name, "random_small", std::move(a), 0, std::move(q));
        } else if (op_ == Operation::PairSum) {
            int k;
            if (n >= 2 && rng.coin()) {
                const int i1 = static_cast<int>(rng.range(0, n - 2)), i2 = static_cast<int>(rng.range(i1 + 1, n - 1));
                k = a[static_cast<size_t>(i1)] + a[static_cast<size_t>(i2)];
            } else {
                k = static_cast<int>(rng.range(-25, 25));
            }
              addNamed(name, "random_small", std::move(a), k);
        } else if (op_ == Operation::KSmallest) {
            const int k = static_cast<int>(rng.range(1, n));
            addNamed(name, "random_small", std::move(a), k);
        } else {
            const int target = rng.coin() ? a[static_cast<size_t>(rng.range(0, n - 1))] : static_cast<int>(rng.range(-12, 12));
            addNamed(name, "random_small", std::move(a), target);
        }
    }

    // random large: n = large_n, values over the whole useful int range
    for (int i = 0; i < opt_.random_large && op_ != Operation::Fibonacci; ++i) {
        const std::string name = "random_large_" + std::to_string(i);
        std::vector<int> a(static_cast<size_t>(large_n));
        for (int& x : a) x = static_cast<int>(rng.range(-1000000000, 1000000000));
        if (op_ == Operation::RangeSum) {
            std::vector<Query> q;
            for (int j = 0, qn = std::min(large_n, 1000); j < qn; ++j) {
                const int l = static_cast<int>(rng.range(0, large_n - 1));
                q.push_back({l, static_cast<int>(rng.range(l, large_n - 1))});
            }
            addNamed(name, "random_large", std::move(a), 0, std::move(q));
        } else if (op_ == Operation::PairSum) {
            int k;
            if (i % 2 == 0) {                                              // a pair far apart, found only late by a scan
                const int i1 = large_n / 2, i2 = large_n - 1;
                k = static_cast<int>(static_cast<long long>(a[static_cast<size_t>(i1)]) + a[static_cast<size_t>(i2)] > INT_MAX ||
                                             static_cast<long long>(a[static_cast<size_t>(i1)]) + a[static_cast<size_t>(i2)] < INT_MIN
                                         ? 0 : a[static_cast<size_t>(i1)] + a[static_cast<size_t>(i2)]);
            } else {
                k = static_cast<int>(rng.range(-1000000000, 1000000000));
            }
            addNamed(name, "random_large", std::move(a), k);
        } else if (op_ == Operation::KSmallest) {
            const int k2 = static_cast<int>(rng.range(1, std::min(large_n, 1000)));
            addNamed(name, "random_large", std::move(a), k2);
        } else {
            int target;
            switch (i % 3) {
                case 0: target = a[static_cast<size_t>(rng.range(0, large_n - 1))]; break;   // present
                case 1: target = 2000000000; break;                                           // absent
                default: target = a[static_cast<size_t>(large_n - 1)]; break;                // last element in the array
            }  addNamed(name, "random_large", std::move(a), target);
        }    
    }
    return out;
}

TestCase TestGenerator::worstCase(long long n) const {
    TestCase tc;
    tc.name = "worst_case";
    tc.category = "worst_case";
    const long long max_n = spec_.max_n > 0 ? spec_.max_n : 100000;
    if (op_ != Operation::Fibonacci) n = std::min(n, max_n);
    n = std::max<long long>(n, 0);

    switch (op_) {
        case Operation::PairSum:                          // distinct non-negative values, k = -1: no pair, every pair is compared
            for (long long i = 0; i < n; ++i) tc.a.push_back(static_cast<int>(i));
            tc.param = -1;
            break;
        case Operation::Search:                           // target absent: the whole array is scanned
            for (long long i = 0; i < n; ++i) tc.a.push_back(static_cast<int>((i * 7919) % 1000003));
            tc.param = -1;
            break;
        case Operation::SearchSorted:                     // sorted, target above every element: linear scan visits all, binary search ~log n
            for (long long i = 0; i < n; ++i) tc.a.push_back(static_cast<int>(i * 2));
            tc.param = static_cast<int>(std::min<long long>(n * 2 + 1, INT_MAX));
            break;
        case Operation::RangeSum:                         // full-range queries: a naive sum per query costs n each
            for (long long i = 0; i < n; ++i) tc.a.push_back(static_cast<int>(i % 1000));
            if (n > 0) tc.queries.assign(static_cast<size_t>(std::min<long long>(n, 2000)), Query{0, static_cast<int>(n - 1)});
            break;
        case Operation::Fibonacci:
            tc.param = static_cast<int>(std::min<long long>(n, kMaxFib));
            break;
        case Operation::KSmallest:                        // full sort costs n log n; k itself kept small
            for (long long i = 0; i < n; ++i) tc.a.push_back(static_cast<int>((i * 2654435761ULL) % 2000000000ULL) - 1000000000);
            tc.param = static_cast<int>(std::min<long long>(n, 10));
            break;    
        case Operation::Unknown:
            break;
    }
    finalize(tc);
    return tc;
}

TestCase TestGenerator::shrink(const TestCase& failing, const FailsFn& fails, int max_calls) const {
    TestCase best = failing;
    int calls = 0;
    auto tryCase = [&](TestCase cand) {
        if (calls >= max_calls || !isValid(cand)) return false;
        ++calls;
        finalize(cand);
        if (!fails(cand)) return false;
        best = std::move(cand);
        return true;
    };
    if (!supported()) return best;

    if (op_ == Operation::Fibonacci) {                    // smallest n that still fails
        for (int n = 0; n < best.param && calls < max_calls; ++n) {
            TestCase c = best;
            c.param = n;
            if (tryCase(std::move(c))) break;
        }
        finalize(best);
        return best;
    }

    if (op_ == Operation::RangeSum) {                     // queries first, then trim the array behind them
        for (size_t chunk = std::max<size_t>(best.queries.size() / 2, 1);;) {
            bool removed = false;
            for (size_t i = 0; i + chunk <= best.queries.size();) {
                TestCase c = best;
                c.queries.erase(c.queries.begin() + static_cast<long>(i), c.queries.begin() + static_cast<long>(i + chunk));
                if (tryCase(std::move(c))) removed = true; else i += chunk;
            }
            if (calls >= max_calls) break;
            if (chunk == 1) { if (!removed) break; } else chunk = std::max<size_t>(chunk / 2, 1);
        }
        size_t needed = 0;
        for (const Query& q : best.queries) needed = std::max(needed, static_cast<size_t>(q.r) + 1);
        if (needed < best.a.size()) { TestCase c = best; c.a.resize(needed); tryCase(std::move(c)); }
    } else {                                              // delta debugging on the array
        for (size_t chunk = std::max<size_t>(best.a.size() / 2, 1);;) {
            bool removed = false;
            for (size_t i = 0; i + chunk <= best.a.size();) {
                TestCase c = best;
                c.a.erase(c.a.begin() + static_cast<long>(i), c.a.begin() + static_cast<long>(i + chunk));
                if (tryCase(std::move(c))) removed = true; else i += chunk;
            }
            if (calls >= max_calls) break;
            if (chunk == 1) { if (!removed) break; } else chunk = std::max<size_t>(chunk / 2, 1);
        }
    }

    for (size_t i = 0; i < best.a.size() && calls < max_calls; ++i) {          // simpler values
        if (best.a[i] == 0) continue;
        TestCase c = best;
        c.a[i] = 0;
        tryCase(std::move(c));
    }
    if (op_ != Operation::RangeSum && best.param != 0 && calls < max_calls) {
        TestCase c = best;
        c.param = 0;
        tryCase(std::move(c));
    }
    finalize(best);
    return best;
}

} // namespace nc::verify
