#pragma once
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include "AnalysisResult.hpp"   // AnalysisResult, AlgorithmGuess, Complexity, Finding, Counterexample

namespace nc {

struct ProblemSpec {
    std::string problem_id;      // empty if custom
    std::string input_type;      // "int_array", "string", "graph", ...
    std::string output_type;     // "bool", "int", "array", ...
    std::string operation;       // "pair_sum", "search", "sort", ...
    long long   max_n = 0;
};

struct BenchmarkResult {
    double before_ms = 0, after_ms = 0;
    long   before_kb = 0, after_kb = 0;
    long long n_used = 0;
};

struct SummaryRow {                        // one comparison line for the UI table
    std::string label;                     // "Time", "Space", "Runtime at n=50000"
    std::string before;
    std::string after;
};

struct OptimizationSummary {
    std::string headline;                  // one sentence, what changed
    std::string explanation;               // 2-4 sentences, why it is faster
    std::vector<SummaryRow> comparison;    // original vs optimized table
    std::vector<std::string> changes;      // numbered list of concrete edits
    std::string tradeoff;                  // what got worse, empty if nothing did
    std::string verification;              // what was run to prove equivalence
    double speedup = 0.0;                  // measured, not theoretical
};

struct OptimizeResult {
    AnalysisResult analysis;
    std::string recommended_algorithm;
    std::string recommended_complexity;
    std::string optimized_code;            // empty when verified == false
    OptimizationSummary summary;
    bool verified = false;                 // passed differential testing
    BenchmarkResult bench;
    bool success = true;
    std::string error;
};

// percent 0..100, stage is a short human label
using ProgressFn = std::function<void(int percent, const std::string& stage)>;

class IEngine {
public:
    virtual ~IEngine() = default;
    virtual AnalysisResult analyze(const std::string& code,
                                   const ProblemSpec& spec,
                                   ProgressFn on_progress) = 0;
    virtual OptimizeResult optimize(const std::string& code,
                                    const ProblemSpec& spec,
                                    ProgressFn on_progress) = 0;
    virtual void cancel() = 0;
};

} // namespace nc