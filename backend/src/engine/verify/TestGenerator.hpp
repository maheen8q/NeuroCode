#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "engine/IEngine.hpp"   // ProblemSpec

namespace nc::verify {

// Differential testing (architecture.md 7.6): TestGenerator makes the inputs, the sandbox runs the user's
// code on them, Oracle says what the right answer is. Verification, not proof: it finds bugs, it does not
// show their absence, and the UI says so.
//
// A problem is described by ProblemSpec::operation, which fixes the function signature and the meaning of
// the answer. The operations below are the ones the optimizer catalog needs; any other operation is
// reported unsupported (supported() == false) and the engine then skips differential testing and must not
// claim a verified optimization.
//
//   operation       user function                                         answer
//   pair_sum        bool solve(vector<int>& a, int k)                     is there i < j with a[i] + a[j] == k
//   search          int  solve(vector<int>& a, int target)                an index of target, or -1
//   search_sorted   int  solve(vector<int>& a, int target)  (a ascending) an index of target, or -1
//   range_sum       vector<long long> solve(vector<int>& a, vector<pair<int,int>>& queries)
//                                                                         sum of a[l..r] (inclusive, 0-based) per query
//   fibonacci       long long solve(int n)                                F(0)=0, F(1)=1
// Aliases are accepted (two_sum, linear_search, binary_search, prefix_sum, fib, ...), see parseOperation().
enum class Operation { PairSum, Search, SearchSorted, RangeSum, Fibonacci, Unknown };

Operation parseOperation(const std::string& name);   // case-insensitive, aliases included; Unknown if not supported
const char* toString(Operation op);                   // canonical name, "unknown" for Unknown
std::vector<std::string> supportedOperations();       // canonical names

struct Query { int l = 0, r = 0; };                   // 0-based, inclusive

// One input. `a`, `param` and `queries` are the data; `stdin_text` and `display` are derived from them
// by TestGenerator::finalize() and are what the sandbox harness and the UI use.
struct TestCase {
    std::string name;            // "empty", "single_miss", "max_values_wrap", "random_small_7", ...
    std::string category;        // empty, single, all_equal, sorted_asc, sorted_desc, max_values, edge, random_small, random_large
    std::vector<int> a;
    int param = 0;               // k (pair_sum), target (search, search_sorted), n (fibonacci)
    std::vector<Query> queries;  // range_sum

    // Text sent to the program's stdin (one format per operation, numbers separated by single spaces):
    //   pair_sum, search, search_sorted:  "n param\n" + "a0 a1 ... a(n-1)\n"
    //   range_sum:                        "n q\n" + "a0 ... a(n-1)\n" + q lines "l r\n"
    //   fibonacci:                        "n\n"
    // The program prints the answer: pair_sum "true"/"false"; search an index; range_sum one number per line;
    // fibonacci one number.
    std::string stdin_text;
    // For counterexamples shown to the user, e.g. "a = [1, 3, 5]\nk = 8". Long arrays are abbreviated.
    std::string display;
};

struct GeneratorOptions {
    uint64_t seed = 20260412;       // same seed, same cases, on every platform
    int random_small = 40;          // random cases with n <= 10
    int random_large = 6;           // random cases with n = large_n
    long long large_n = 2000;       // size of the "large" cases: O(n^2) code must finish in the sandbox limit
    int max_fibonacci_n = 30;       // naive recursion has to be able to answer every test
};

class TestGenerator {
public:
    explicit TestGenerator(const ProblemSpec& spec, GeneratorOptions options = {});

    bool supported() const { return op_ != Operation::Unknown; }
    Operation operation() const { return op_; }
    // Why it is unsupported (unknown operation), or a warning (input/output type differs from what the
    // operation expects). Empty when all is well.
    const std::string& note() const { return note_; }

    // Edge cases first (empty, single element, all equal, sorted ascending and descending, maximum values
    // that overflow int, ...), then random small, then random large. Empty if !supported().
    std::vector<TestCase> generate() const;

    // The input that makes the obvious brute-force solution slowest, for timing runs (Benchmark):
    // pair_sum / search: no match, so every element is visited; range_sum: q full-range queries;
    // fibonacci: n = n. `n` is clamped to ProblemSpec::max_n when that is set.
    TestCase worstCase(long long n) const;

    // Preconditions of the operation (array sorted for search_sorted, queries inside the array, n within
    // the fibonacci range, size <= max_n). Shrinking only keeps cases that stay valid.
    bool isValid(const TestCase& tc) const;

    // Fills stdin_text and display from the data.
    void finalize(TestCase& tc) const;

    // Delta debugging for counterexamples: removes elements and simplifies values as long as
    // fails(candidate) stays true, so the user sees `a = [3, 3]` and not 2000 numbers.
    // `fails` is called at most max_calls times; the result is `failing` itself if nothing shrinks.
    using FailsFn = std::function<bool(const TestCase&)>;
    TestCase shrink(const TestCase& failing, const FailsFn& fails, int max_calls = 150) const;

private:
    ProblemSpec spec_;
    GeneratorOptions opt_;
    Operation op_;
    std::string note_;
};

} // namespace nc::verify
