// Tests for nc::Engine (the real IEngine). Needs libclang, g++ and the sandbox; about a minute.
#include "engine/Engine.hpp"
#include "engine/optimize/Catalog.hpp"
#include "FakePredictor.hpp"

#include <iostream>
#include <thread>

using namespace nc;

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { ++g_failed; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; } } while (0)

static ProblemSpec spec(const char* op, long long max_n = 100000) {
    ProblemSpec s; s.operation = op; s.max_n = max_n; return s;
}

// A pass that always "finds" a rewrite, to test verification without the real detection.
class FixedPass final : public optimize::IPass {
public:
    FixedPass(std::string code, std::string name = "PairSumToHash") : code_(std::move(code)), name_(std::move(name)) {}
    std::string name() const override { return name_; }
    std::optional<optimize::OptimizationResult> tryApply(const optimize::PassContext& ctx) const override {
        CHECK(ctx.ast != nullptr);
        CHECK(ctx.cfg != nullptr);
        optimize::OptimizationResult r;
        r.passName = name_;
        r.title = "Replace nested loop with hash lookup";
        r.rationale = "Each element is checked against a hash set.";
        r.beforeComplexity = "O(n^2)";
        r.afterComplexity = "O(n)";
        r.rewrittenCode = code_;
        return r;
    }
private:
    std::string code_, name_;
};

static EngineOptions withPass(const std::string& code, const std::string& name = "PairSumToHash") {
    auto catalog = std::make_shared<optimize::Catalog>();
    catalog->add(std::make_unique<FixedPass>(code, name));
    EngineOptions o;
    o.catalog = catalog;
    return o;
}

static const char* kBrute =
    "bool hasPair(vector<int>& a, int k) {\n"
    "    int n = a.size();\n"
    "    for (int i = 0; i < n; i++)\n"
    "        for (int j = i + 1; j < n; j++)\n"
    "            if ((long long)a[i] + a[j] == k) return true;\n"
    "    return false;\n"
    "}\n";

static const char* kHashGood =
    "#include <unordered_set>\n"
    "bool hasPairFast(const vector<int>& a, int k) {\n"
    "    unordered_set<long long> seen;\n"
    "    for (int x : a) {\n"
    "        if (seen.count((long long)k - x)) return true;\n"
    "        seen.insert(x);\n"
    "    }\n"
    "    return false;\n"
    "}\n";

static const char* kHashWrongOverflow =      // int arithmetic: wraps for values near INT_MAX
    "bool hasPairFast(const vector<int>& a, int k) {\n"
    "    unordered_set<int> seen;\n"
    "    for (int x : a) {\n"
    "        if (seen.count(k - x)) return true;\n"
    "        seen.insert(x);\n"
    "    }\n"
    "    return false;\n"
    "}\n";

static const char* kHashWrongSelf =          // pairs an element with itself
    "bool hasPairFast(const vector<int>& a, int k) {\n"
    "    unordered_set<long long> seen;\n"
    "    for (int x : a) seen.insert(x);\n"
    "    for (int x : a) if (seen.count((long long)k - x)) return true;\n"
    "    return false;\n"
    "}\n";

static void testAnalyzeCorrect() {
    FakePredictor p;
    Engine e(p);
    std::vector<std::pair<int, std::string>> seen;
    const AnalysisResult r = e.analyze(kBrute, spec("pair_sum"), [&](int pc, const std::string& st) { seen.push_back({pc, st}); });
    CHECK(r.success);
    CHECK(r.error.empty());
    CHECK(r.complexity.time == "O(n^2)");
    CHECK(r.complexity.space == "O(1)");
    CHECK(!r.complexity.derivation.empty());
    CHECK(r.algorithms.size() == 3);
    CHECK(r.counterexamples.empty());
    CHECK(!r.ast_json.empty() && r.ast_json.front() == '{');
    CHECK(r.cfg_json.find("\"functions\"") != std::string::npos);
    CHECK(p.lastAst == r.ast_json);            // no I/O to strip: the predictor sees the same AST
    CHECK(!r.optimization_available);          // the shipped passes do not detect anything yet
    CHECK(!seen.empty() && seen.back().first == 100 && seen.back().second == "complete");
    bool monotonic = true;
    for (size_t i = 1; i < seen.size(); ++i) monotonic = monotonic && seen[i].first >= seen[i - 1].first;
    CHECK(monotonic);
    CHECK(seen.size() >= 9);
}

static void testCounterexample() {
    FakePredictor p;
    Engine e(p);
    // j starts at i: an element pairs with itself.
    const AnalysisResult r = e.analyze(
        "bool hasPair(vector<int>& a, int k) {\n"
        "    for (size_t i = 0; i < a.size(); i++)\n"
        "        for (size_t j = i; j < a.size(); j++)\n"
        "            if (a[i] + a[j] == k) return true;\n"
        "    return false;\n"
        "}\n", spec("pair_sum"), nullptr);
    CHECK(r.success);
    CHECK(!r.counterexamples.empty());
    if (!r.counterexamples.empty()) {
        const Counterexample& c = r.counterexamples.front();
        CHECK(c.expected_output == "false");
        CHECK(c.actual_output == "true");
        CHECK(c.input.find("a = [") != std::string::npos);
        CHECK(c.input.size() < 60);            // shrunk, not 2000 numbers
    }
}

static void testErrors() {
    FakePredictor p;
    {
        Engine e(p);
        const AnalysisResult r = e.analyze("bool f(vector<int>& a, int k) {\n  return a.size() > k\n}\n", spec("pair_sum"), nullptr);
        CHECK(!r.success);
        CHECK(r.error.rfind("COMPILE_ERROR: line ", 0) == 0);
    }
    {
        FakePredictor empty; empty.empty = true;
        Engine e(empty);
        const AnalysisResult r = e.analyze(kBrute, spec("pair_sum"), nullptr);
        CHECK(!r.success);
        CHECK(r.error.rfind("MODEL_UNAVAILABLE", 0) == 0);
    }
    {
        FakePredictor bad; bad.throws = true;
        Engine e(bad);
        const AnalysisResult r = e.analyze(kBrute, spec("pair_sum"), nullptr);
        CHECK(!r.success);
        CHECK(r.error.rfind("INTERNAL: boom", 0) == 0);
    }
}

static void testUnsupportedAndMain() {
    FakePredictor p;
    {
        Engine e(p);
        const AnalysisResult r = e.analyze(kBrute, spec("matrix_chain"), nullptr);
        CHECK(r.success);
        CHECK(r.counterexamples.empty());
    }
    {   // contest style: own main, I/O in it
        Engine e(p);
        const AnalysisResult r = e.analyze(
            "int main() {\n  int n; cin >> n;\n  long long s = 0;\n  for (int i = 0; i < n; i++) { int x; cin >> x; s += x; }\n  cout << s << endl;\n}\n",
            spec("pair_sum"), nullptr);
        CHECK(r.success);
        CHECK(r.counterexamples.empty());
        CHECK(r.complexity.time == "O(n)");
    }
    {   // signature that does not fit the operation: tested nowhere, no error
        Engine e(p);
        const AnalysisResult r = e.analyze("bool f(int x) { return x > 0; }\n", spec("pair_sum"), nullptr);
        CHECK(r.success);
        CHECK(r.counterexamples.empty());
    }
}

static void testOtherOperations() {
    FakePredictor p;
    {
        Engine e(p);
        const AnalysisResult r = e.analyze(
            "long long fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }\n", spec("fibonacci"), nullptr);
        CHECK(r.success);
        CHECK(r.counterexamples.empty());
        CHECK(r.complexity.time == "O(2^n)");
    }
    {
        Engine e(p);
        const AnalysisResult r = e.analyze(
            "long long fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2) + 1; }\n", spec("fibonacci"), nullptr);
        CHECK(r.success);
        CHECK(!r.counterexamples.empty());
    }
    {
        Engine e(p);
        const AnalysisResult r = e.analyze(
            "int find(vector<int>& a, int t) { for (int i = 0; i < (int)a.size(); i++) if (a[i] == t) return i; return -1; }\n",
            spec("search"), nullptr);
        CHECK(r.success);
        CHECK(r.counterexamples.empty());
    }
    {   // prefix sums with int overflow: wrong on large values
        Engine e(p);
        const AnalysisResult r = e.analyze(
            "vector<long long> rs(vector<int>& a, vector<pair<int,int>>& q) {\n"
            "  vector<int> pre(a.size() + 1, 0);\n"
            "  for (size_t i = 0; i < a.size(); i++) pre[i + 1] = pre[i] + a[i];\n"
            "  vector<long long> out;\n"
            "  for (auto& x : q) out.push_back(pre[x.second + 1] - pre[x.first]);\n"
            "  return out;\n}\n", spec("range_sum"), nullptr);
        CHECK(r.success);
        CHECK(!r.counterexamples.empty());
    }
}

static void testRuntimeProblems() {
    FakePredictor p;
    {
        Engine e(p);
        const AnalysisResult r = e.analyze("bool f(vector<int>& a, int k) { return a[1000000000] == k; }\n", spec("pair_sum"), nullptr);
        CHECK(r.success);
        CHECK(!r.counterexamples.empty());
        if (!r.counterexamples.empty()) CHECK(r.counterexamples.front().actual_output.find("crashed") == 0);
    }
    {
        EngineOptions o;
        o.test_timeout_ms = 300;
        Engine e(p, o);
        const AnalysisResult r = e.analyze("bool f(vector<int>& a, int k) { volatile int x = 0; while (true) { x = x + 1; } return false; }\n",
                                           spec("pair_sum"), nullptr);
        CHECK(r.success);
        CHECK(r.counterexamples.size() == 1);
        if (!r.counterexamples.empty()) CHECK(r.counterexamples.front().actual_output.find("timed out") == 0);
    }
}

static void testOptimizeVerified() {
    FakePredictor p;
    Engine e(p, withPass(kHashGood));
    std::vector<std::pair<int, std::string>> seen;
    const OptimizeResult r = e.optimize(kBrute, spec("pair_sum"), [&](int pc, const std::string& st) { seen.push_back({pc, st}); });
    CHECK(r.success);
    CHECK(r.analysis.success);
    CHECK(r.analysis.optimization_available);
    CHECK(r.verified);
    CHECK(r.recommended_algorithm == "hash_set_lookup");
    CHECK(r.recommended_complexity == "O(n)");
    CHECK(r.optimized_code.find("hasPairFast") != std::string::npos);
    CHECK(r.optimized_code.find("#include") == std::string::npos);
    CHECK(!r.summary.headline.empty());
    CHECK(!r.summary.explanation.empty());
    CHECK(r.summary.comparison.size() >= 4);
    CHECK(r.summary.comparison[1].label == "Time" && r.summary.comparison[1].before == "O(n^2)" && r.summary.comparison[1].after == "O(n)");
    CHECK(!r.summary.verification.empty());
    CHECK(r.bench.n_used == 20000);
    CHECK(r.bench.before_ms > 0 && r.bench.after_ms > 0);
    CHECK(r.summary.speedup > 1.0);
    CHECK(r.bench.before_kb > 0 && r.bench.after_kb > 0);
    CHECK(!r.summary.tradeoff.empty());       // O(1) -> O(n) space
    CHECK(!seen.empty() && seen.back().first == 100 && seen.back().second == "optimization");
}

static void testOptimizeUnverified() {
    FakePredictor p;
    const char* bad[] = {kHashWrongOverflow, kHashWrongSelf,
                         "int notEvenCompiles(const vector<int>& a, int k) { return a.size( }\n",
                         "bool wrongSignature(int x) { return true; }\n",
                         ""};
    for (const char* code : bad) {
        Engine e(p, withPass(code));
        const OptimizeResult r = e.optimize(kBrute, spec("pair_sum"), nullptr);
        CHECK(r.success);
        CHECK(!r.verified);
        CHECK(r.optimized_code.empty());
        CHECK(r.summary.headline.empty() && r.summary.verification.empty());
        CHECK(r.analysis.success);
    }
    {   // nothing found by the built-in passes
        Engine e(p);
        const OptimizeResult r = e.optimize(kBrute, spec("pair_sum"), nullptr);
        CHECK(r.success && !r.verified && r.optimized_code.empty());
    }
    {   // unsupported operation: a rewrite can exist but cannot be tested, so it is never verified
        Engine e(p, withPass(kHashGood));
        const OptimizeResult r = e.optimize(kBrute, spec("matrix_chain"), nullptr);
        CHECK(r.success && !r.verified && r.optimized_code.empty());
    }
    {   // analysis failure propagates
        Engine e(p, withPass(kHashGood));
        const OptimizeResult r = e.optimize("bool f( {", spec("pair_sum"), nullptr);
        CHECK(!r.success);
        CHECK(r.error.rfind("COMPILE_ERROR", 0) == 0);
        CHECK(r.error == r.analysis.error);
    }
}

static void testCancel() {
    FakePredictor p;
    Engine e(p);
    e.cancel();                                  // before the call: honoured
    AnalysisResult r = e.analyze(kBrute, spec("pair_sum"), nullptr);
    CHECK(!r.success && r.error == "cancelled");
    r = e.analyze(kBrute, spec("pair_sum"), nullptr);   // the flag was cleared: the engine runs again
    CHECK(r.success);

    Engine e2(p);
    std::thread t([&] { std::this_thread::sleep_for(std::chrono::milliseconds(1200)); e2.cancel(); });
    const AnalysisResult mid = e2.analyze(kBrute, spec("pair_sum"), nullptr);
    t.join();
    CHECK(!mid.success && mid.error == "cancelled");

    std::unique_ptr<IEngine> f = makeEngine(p);  // the factory returns the real engine
    CHECK(f != nullptr);
}

static void testStripPath() {
    FakePredictor p;
    Engine e(p);
    const AnalysisResult r = e.analyze(
        "int solve(int n, vector<int>& a) {\n"
        "    cin >> n;\n"
        "    long long s = 0;\n"
        "    for (int i = 0; i < n; i++) s += a[i];\n"
        "    cout << s;\n"
        "    return (int)s;\n"
        "}\n", spec("custom"), nullptr);
    CHECK(r.success);
    CHECK(p.lastAst != r.ast_json);              // the predictor saw the stripped code, the UI the original
    CHECK(r.ast_json.find("cin") != std::string::npos);
    CHECK(p.lastAst.find("cin") == std::string::npos);
}

int main() {
    testAnalyzeCorrect();
    testCounterexample();
    testErrors();
    testUnsupportedAndMain();
    testOtherOperations();
    testRuntimeProblems();
    testStripPath();
    testOptimizeVerified();
    testOptimizeUnverified();
    testCancel();
    if (g_failed) { std::cerr << g_failed << " check(s) failed\n"; return 1; }
    std::cout << "test_engine: all checks passed\n";
    return 0;
}
