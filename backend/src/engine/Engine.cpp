#include "Engine.hpp"
#include <chrono>
#include <thread>

namespace nc {

namespace {

// Spreads roughly 3 seconds of simulated work across a set of (percent, stage)
// checkpoints, per the week-1 stub contract in api-spec.md section 10.
// Returns false if the job was cancelled mid-step.
bool stepThrough(const std::vector<std::pair<int, std::string>>& steps,
                  ProgressFn on_progress,
                  const std::atomic<bool>& cancelled) {
    const auto delay = std::chrono::milliseconds(3000 / static_cast<int>(steps.size()));
    for (const auto& [percent, stage] : steps) {
        if (cancelled.load()) return false;
        std::this_thread::sleep_for(delay);
        if (on_progress) on_progress(percent, stage);
    }
    return true;
}

} // namespace

AnalysisResult Engine::fakeAnalysis(const ProblemSpec& /*spec*/, ProgressFn on_progress) {
    const bool completed = stepThrough({
        {10, "clang AST"},
        {20, "internal AST"},
        {30, "control flow graph"},
        {45, "complexity analysis"},
        {55, "feature extraction"},
        {65, "algorithm classification"},
        {75, "static checks"},
        {90, "differential testing"},
    }, on_progress, cancelled_);

    AnalysisResult result;

    if (!completed) {
        result.success = false;
        result.error = "cancelled";
        return result;
    }

    result.algorithms = {
        {"brute_force_pair_search", 0.94f},
        {"two_pointer",             0.04f},
        {"sliding_window",          0.02f},
    };

    result.complexity.time = "O(n^2)";
    result.complexity.space = "O(1)";
    result.complexity.derivation = {
        "outer loop runs n times (line 2)",
        "inner loop runs n - i - 1 times (line 3)",
        "sum over i gives n(n-1)/2",
        "no heap allocation detected",
    };

    result.findings = {
        {14, "medium", "overflow_risk",
         "a[i] + a[j] may overflow int for values near INT_MAX"},
    };

    result.counterexamples = {
        {"a = [1, 3, 5]\nk = 8", "true", "false"},
    };

    result.ast_json = R"({"type":"Function","name":"solve","children":[]})";
    result.cfg_json = R"({"nodes":[],"edges":[]})";

    result.optimization_available = true;
    result.success = true;
    return result;
}

AnalysisResult Engine::analyze(const std::string& /*code*/,
                               const ProblemSpec& spec,
                               ProgressFn on_progress) {
    AnalysisResult result = fakeAnalysis(spec, on_progress);
    if (result.success && on_progress) on_progress(100, "complete");
    return result;
}

OptimizeResult Engine::optimize(const std::string& code,
                                const ProblemSpec& spec,
                                ProgressFn on_progress) {
    OptimizeResult result;
    result.analysis = fakeAnalysis(spec, on_progress);

    if (!result.analysis.success) {
        result.success = false;
        result.error = result.analysis.error;
        return result;
    }

    if (cancelled_.load()) {
        result.success = false;
        result.error = "cancelled";
        return result;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    if (on_progress) on_progress(100, "optimization");

    result.recommended_algorithm = "hash_set_lookup";
    result.recommended_complexity = "O(n)";

    result.optimized_code =
        "bool solve(vector<int>& a, int k) {\n"
        "    unordered_set<int> seen;\n"
        "    for (int x : a) {\n"
        "        if (seen.count(k - x)) return true;\n"
        "        seen.insert(x);\n"
        "    }\n"
        "    return false;\n"
        "}";

    result.summary.headline = "Replaced the inner scan with a hash set lookup.";
    result.summary.explanation =
        "The original checks every pair, so the work grows with the square "
        "of the input size. The new version stores each value as it passes "
        "and asks the set whether the complement k - x has already been "
        "seen, which answers the same question in one pass.";
    result.summary.comparison = {
        {"Algorithm",            "Brute force pair search", "Hash set lookup"},
        {"Time",                 "O(n^2)",  "O(n) expected"},
        {"Space",                "O(1)",    "O(n)"},
        {"Runtime at n = 50000", "2841 ms", "8.4 ms"},
        {"Memory",               "412 KB",  "2180 KB"},
    };
    result.summary.changes = {
        "Removed the nested loop over j (lines 3 to 5)",
        "Added an unordered_set<int> seen to record values already visited",
        "Replaced the a[i] + a[j] == k test with a complement lookup",
    };
    result.summary.tradeoff =
        "Memory rises from constant to linear in n. At the stated bound of "
        "n = 100000 this is roughly 400 KB, which is acceptable.";
    result.summary.verification =
        "Compiled successfully and produced identical output to the "
        "original on 200 generated inputs, including empty array, single "
        "element, all-equal values, and values near INT_MAX.";
    result.summary.speedup = 338.2;

    result.verified = true;

    result.bench.n_used = 50000;
    result.bench.before_ms = 2841.3;
    result.bench.after_ms = 8.4;
    result.bench.before_kb = 412;
    result.bench.after_kb = 2180;

    result.success = true;
    (void)code;
    return result;
}

void Engine::cancel() {
    cancelled_.store(true);
}

} // namespace nc