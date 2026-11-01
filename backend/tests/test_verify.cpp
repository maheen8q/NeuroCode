// Tests for nc::verify::TestGenerator and nc::verify::Oracle. No libclang, no sandbox.
#include "engine/verify/TestGenerator.hpp"
#include "engine/verify/Oracle.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <set>

using namespace nc;
using namespace nc::verify;

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { ++g_failed; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; } } while (0)

static ProblemSpec spec(const char* op, long long max_n = 100000) {
    ProblemSpec s; s.operation = op; s.max_n = max_n; return s;
}

// Independent brute forces (deliberately different from the oracle's implementations).
static bool brutePair(const std::vector<int>& a, int k) {
    for (size_t i = 0; i < a.size(); ++i)
        for (size_t j = i + 1; j < a.size(); ++j)
            if (static_cast<long long>(a[i]) + a[j] == k) return true;
    return false;
}

static const char* kOps[] = {"pair_sum", "search", "search_sorted", "range_sum", "fibonacci"};

static void testParse() {
    CHECK(parseOperation("Two_Sum") == Operation::PairSum);
    CHECK(parseOperation("binary_search") == Operation::SearchSorted);
    CHECK(parseOperation("linear_search") == Operation::Search);
    CHECK(parseOperation("prefix_sum") == Operation::RangeSum);
    CHECK(parseOperation("fib") == Operation::Fibonacci);
    CHECK(parseOperation("matrix_chain") == Operation::Unknown);
    CHECK(!TestGenerator(spec("matrix_chain")).supported());
    CHECK(TestGenerator(spec("matrix_chain")).generate().empty());
    CHECK(!TestGenerator(spec("matrix_chain")).note().empty());
    CHECK(supportedOperations().size() == 5);
    for (const char* op : kOps) CHECK(parseOperation(toString(parseOperation(op))) == parseOperation(op));
}

static void testDeterminismAndValidity() {
    for (const char* op : kOps) {
        TestGenerator g(spec(op));
        const auto x = g.generate(), y = g.generate();
        CHECK(!x.empty());
        CHECK(x.size() == y.size());
        for (size_t i = 0; i < x.size() && i < y.size(); ++i)
            CHECK(x[i].stdin_text == y[i].stdin_text && x[i].name == y[i].name);
        for (const TestCase& tc : x) { CHECK(g.isValid(tc)); CHECK(!tc.stdin_text.empty()); CHECK(!tc.display.empty()); }
        GeneratorOptions o; o.seed = 7;
        const auto z = TestGenerator(spec(op), o).generate();
        bool differs = z.size() != x.size();
        for (size_t i = 0; !differs && i < x.size(); ++i) differs = x[i].stdin_text != z[i].stdin_text;
        if (std::string(op) != "fibonacci") CHECK(differs);        // fibonacci random cases may coincide
    }
}

static void testCategories() {
    for (const char* op : kOps) {
        std::set<std::string> cats;
        for (const TestCase& tc : TestGenerator(spec(op)).generate()) cats.insert(tc.category);
        const bool fib = std::string(op) == "fibonacci";
        for (const char* need : {"single", "random_small", "max_values"}) CHECK(cats.count(need));
        if (!fib) for (const char* need : {"empty", "all_equal", "sorted_asc", "random_large"}) CHECK(cats.count(need) || (std::string(op) == "search_sorted" && std::string(need) == "all_equal" && cats.count(need)));
        if (!fib && std::string(op) != "search_sorted") CHECK(cats.count("sorted_desc"));
        if (!fib) CHECK(cats.count("empty"));
    }
}

static void testSortedForBinarySearch() {
    for (const TestCase& tc : TestGenerator(spec("search_sorted")).generate())
        CHECK(std::is_sorted(tc.a.begin(), tc.a.end()));
}

static void testMaxNRespected() {
    for (const char* op : kOps)
        for (const TestCase& tc : TestGenerator(spec(op, 50)).generate()) CHECK(tc.a.size() <= 50);
}

static void testOracleAgainstBrute() {
    for (const TestCase& tc : TestGenerator(spec("pair_sum")).generate()) {
        const bool want = brutePair(tc.a, tc.param);
        CHECK(Oracle(spec("pair_sum")).expectedOutput(tc) == (want ? "true" : "false"));
    }
    for (const TestCase& tc : TestGenerator(spec("search")).generate()) {
        const int idx = firstIndexOf(tc.a, tc.param);
        CHECK(idx == -1 || tc.a[static_cast<size_t>(idx)] == tc.param);
        CHECK((idx == -1) == (std::find(tc.a.begin(), tc.a.end(), tc.param) == tc.a.end()));
    }
    for (const TestCase& tc : TestGenerator(spec("range_sum")).generate()) {
        const auto got = rangeSums(tc.a, tc.queries);
        CHECK(got.size() == tc.queries.size());
        for (size_t i = 0; i < got.size(); ++i) {
            long long s = 0;
            for (int j = tc.queries[i].l; j <= tc.queries[i].r; ++j) s += tc.a[static_cast<size_t>(j)];
            CHECK(got[i] == s);
        }
    }
    CHECK(fibonacciNumber(0) == 0 && fibonacciNumber(1) == 1 && fibonacciNumber(10) == 55 && fibonacciNumber(92) == 7540113804746346429LL);
}

static void testOverflowCases() {
    Oracle o(spec("pair_sum"));
    TestCase tc; tc.a = {INT_MAX, INT_MAX}; tc.param = -2;
    CHECK(o.expectedOutput(tc) == "false");                        // a wrapping solution says true
    CHECK(o.check(tc, "true").status == Verdict::Status::Fail);
    tc.a = {INT_MAX, 5}; tc.param = INT_MIN + 4;
    CHECK(o.expectedOutput(tc) == "false");
    tc.a = {INT_MAX, INT_MIN}; tc.param = -1;
    CHECK(o.expectedOutput(tc) == "true");
    tc.a = {5}; tc.param = 10;                                     // one element cannot pair with itself
    CHECK(o.expectedOutput(tc) == "false");

    Oracle r(spec("range_sum"));
    TestCase rc; rc.a = {INT_MAX, INT_MAX, INT_MAX}; rc.queries = {{0, 2}};
    CHECK(r.expectedOutput(rc) == "6442450941");
    CHECK(r.check(rc, "2147483645").status == Verdict::Status::Fail);   // int overflow in user code
    CHECK(r.check(rc, "6442450941\n").passed());
}

static void testCheckVerdicts() {
    Oracle p(spec("pair_sum"));
    TestCase tc; tc.a = {1, 2, 3}; tc.param = 5;
    CHECK(p.check(tc, "true").passed());
    CHECK(p.check(tc, "  TRUE \n").passed());
    CHECK(p.check(tc, "1").passed());
    CHECK(p.check(tc, "false").status == Verdict::Status::Fail);
    CHECK(p.check(tc, "").status == Verdict::Status::Malformed);
    CHECK(p.check(tc, "true true").status == Verdict::Status::Malformed);
    CHECK(p.check(tc, "maybe").status == Verdict::Status::Malformed);
    CHECK(!p.check(tc, "false").detail.empty());

    Oracle s(spec("search"));
    TestCase sc; sc.a = {4, 9, 9, 1}; sc.param = 9;
    CHECK(s.check(sc, "1").passed());
    CHECK(s.check(sc, "2").passed());                              // any valid index
    CHECK(s.check(sc, "0").status == Verdict::Status::Fail);
    CHECK(s.check(sc, "-1").status == Verdict::Status::Fail);
    CHECK(s.check(sc, "7").status == Verdict::Status::Fail);
    CHECK(s.check(sc, "x").status == Verdict::Status::Malformed);
    CHECK(s.check(sc, "1.5").status == Verdict::Status::Malformed);
    sc.param = 100;
    CHECK(s.check(sc, "-1").passed());
    CHECK(s.check(sc, "0").status == Verdict::Status::Fail);

    Oracle r(spec("range_sum"));
    TestCase rc; rc.a = {1, 2, 3}; rc.queries = {{0, 0}, {0, 2}};
    CHECK(r.check(rc, "1\n6\n").passed());
    CHECK(r.check(rc, "1\n7\n").status == Verdict::Status::Fail);
    CHECK(r.check(rc, "1\n").status == Verdict::Status::Malformed);

    Oracle f(spec("fibonacci"));
    TestCase fc; fc.param = 10;
    CHECK(f.check(fc, "55").passed());
    CHECK(f.check(fc, "54").status == Verdict::Status::Fail);
    CHECK(f.check(fc, "").status == Verdict::Status::Malformed);

    CHECK(Oracle(spec("nope")).check(tc, "true").status == Verdict::Status::Malformed);
    CHECK(!Oracle(spec("nope")).supported());
    CHECK(std::string(500, 'z').size() == 500 && p.check(tc, std::string(500, 'z')).actual.size() <= 203);
}

static void testStdinFormat() {
    TestGenerator g(spec("pair_sum"));
    TestCase tc; tc.a = {1, -2, 3}; tc.param = 4;
    g.finalize(tc);
    CHECK(tc.stdin_text == "3 4\n1 -2 3\n");
    TestGenerator gr(spec("range_sum"));
    TestCase rc; rc.a = {5, 6}; rc.queries = {{0, 1}, {1, 1}};
    gr.finalize(rc);
    CHECK(rc.stdin_text == "2 2\n5 6\n0 1\n1 1\n");
    TestGenerator gf(spec("fibonacci"));
    TestCase fc; fc.param = 12;
    gf.finalize(fc);
    CHECK(fc.stdin_text == "12\n");
    TestCase e; TestGenerator(spec("search")).finalize(e);
    CHECK(e.stdin_text == "0 0\n\n");
    TestCase big; big.a.assign(100, 1);
    g.finalize(big);
    CHECK(big.display.size() < 200);                               // long arrays are abbreviated in the UI text
}

static void testShrink() {
    TestGenerator g(spec("pair_sum"));
    // A buggy "solution" that says false whenever the array has a duplicate pair.
    auto fails = [&](const TestCase& c) { return Oracle(spec("pair_sum")).expectedOutput(c) == "true" && c.a.size() >= 2 &&
                                                 std::any_of(c.a.begin(), c.a.end(), [&](int x) { return x == c.param - x; }) == false; };
    TestCase big; big.a = {9, 8, 1, 2, 3, 4, 5, 6, 7}; big.param = 3;
    CHECK(fails(big));
    const TestCase small = g.shrink(big, fails);
    CHECK(fails(small));
    CHECK(small.a.size() == 2);
    CHECK(g.isValid(small));

    // Calls are bounded.
    int calls = 0;
    g.shrink(big, [&](const TestCase&) { ++calls; return true; }, 10);
    CHECK(calls <= 10);

    // Nothing fails on smaller input: result is the original.
    const TestCase same = g.shrink(big, [&](const TestCase& c) { return c.a.size() == big.a.size() && c.a == big.a && c.param == big.param; });
    CHECK(same.a == big.a);

    // range_sum drops queries and trims the array.
    TestGenerator gr(spec("range_sum"));
    TestCase rc; rc.a = {1, 2, 3, 4, 5, 6}; rc.queries = {{0, 5}, {1, 2}, {3, 3}, {0, 0}};
    const TestCase rs = gr.shrink(rc, [](const TestCase& c) {
        return std::any_of(c.queries.begin(), c.queries.end(), [](const Query& q) { return q.l == 3 && q.r == 3; });
    });
    CHECK(rs.queries.size() == 1 && rs.queries[0].l == 3 && rs.a.size() == 4);

    // search_sorted shrinks must stay sorted.
    TestGenerator gs(spec("search_sorted"));
    TestCase sc; sc.a = {1, 2, 3, 4, 5, 6, 7, 8}; sc.param = 5;
    const TestCase ss = gs.shrink(sc, [](const TestCase& c) { return !c.a.empty(); });
    CHECK(std::is_sorted(ss.a.begin(), ss.a.end()) && ss.a.size() == 1);

    // fibonacci: smallest failing n.
    TestGenerator gf(spec("fibonacci"));
    TestCase fc; fc.param = 40;
    CHECK(gf.shrink(fc, [](const TestCase& c) { return c.param >= 7; }).param == 7);
}

static void testWorstCase() {
    TestGenerator pg(spec("pair_sum"));
    TestCase w = pg.worstCase(3000);
    CHECK(w.a.size() == 3000 && !brutePair(w.a, w.param));         // no pair: a scan compares every pair
    CHECK(pg.isValid(w));
    TestGenerator sg(spec("search"));
    w = sg.worstCase(1000);
    CHECK(firstIndexOf(w.a, w.param) == -1 && w.a.size() == 1000);
    TestGenerator bg(spec("search_sorted"));
    w = bg.worstCase(1000);
    CHECK(bg.isValid(w) && firstIndexOf(w.a, w.param) == -1);
    TestGenerator rg(spec("range_sum"));
    w = rg.worstCase(1000);
    CHECK(rg.isValid(w) && w.queries.size() == 1000 && w.queries[0].r == 999);
    TestGenerator fg(spec("fibonacci"));
    CHECK(fg.worstCase(100000).param <= 90 && fg.isValid(fg.worstCase(100000)));
    CHECK(TestGenerator(spec("pair_sum", 100)).worstCase(5000).a.size() == 100);   // clamped to max_n
    CHECK(pg.worstCase(0).a.empty());
}

int main() {
    testParse();
    testDeterminismAndValidity();
    testCategories();
    testSortedForBinarySearch();
    testMaxNRespected();
    testOracleAgainstBrute();
    testOverflowCases();
    testCheckVerdicts();
    testStdinFormat();
    testShrink();
    testWorstCase();
    if (g_failed) { std::cerr << g_failed << " check(s) failed\n"; return 1; }
    std::cout << "all verify tests passed\n";
    return 0;
}
