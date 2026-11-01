#pragma once
#include <atomic>
#include <memory>
#include <string>

#include "engine/IEngine.hpp"
#include "engine/parse/ClangFrontend.hpp"
#include "engine/verify/Benchmark.hpp"
#include "engine/verify/TestGenerator.hpp"
#include "sandbox/Limits.hpp"

namespace nc::optimize { class Catalog; }

namespace nc {

// Knobs of the real engine. The defaults are what production uses; tests change them.
struct EngineOptions {
    // Passes that look for an optimization. Null = optimize::Catalog() with the built-in passes.
    // Each pass receives PassContext{ast = const ast::Node*, cfg = const std::vector<cfg::Cfg>*, sourceCode}.
    std::shared_ptr<const optimize::Catalog> catalog;

    parse::ClangOptions clang;               // libclang resource dir, extra arguments
    verify::GeneratorOptions generator;      // seed and sizes of the generated test inputs

    // Differential testing and benchmarking compile and run the user's code in the sandbox.
    // false = skip both (analysis only, no counterexamples, never a verified optimization).
    bool differential_testing = true;
    std::string scratch_base = "/tmp";       // per-job scratch directory is created here
    int run_as_uid = -1;                     // pass through to the sandbox when the server runs as root
    int test_timeout_ms = 2000;              // wall limit of ONE generated test run
    int max_counterexamples = 3;
    int benchmark_repeats = 5;               // best of N per version
};

// The real engine: parse -> AST -> CFG -> complexity -> features + prediction -> static checks ->
// differential testing (TestGenerator -> sandbox -> Oracle), and for optimize() the optimizer passes,
// verification of the rewritten code and a benchmark. Replaces EngineStub; makeEngine() keeps its signature.
//
// Lifetime: ONE engine per job. cancel() may be called from any thread; the flag is cleared when
// analyze()/optimize() returns, so a cancel() that arrives before the call starts is honoured.
//
// Failures come back as success=false with error "CODE: message" (api-spec section 7):
//   COMPILE_ERROR   syntax or semantic error in the user's code, "line 7: expected ';' before '}'"
//   PARSE_ERROR     libclang could not produce a translation unit
//   TIMEOUT         never raised: a test that runs too long is reported as a counterexample
//   SANDBOX_ERROR   g++ or the sandbox could not be started
//   MODEL_UNAVAILABLE  the predictor returned no algorithm
//   INTERNAL        anything unexpected, including exceptions from the predictor
//   cancelled       cancel() was called
//
// Differential testing needs a supported ProblemSpec::operation (pair_sum, search, search_sorted, range_sum,
// fibonacci) and a user function with the matching signature (see verify/TestGenerator.hpp). Otherwise it is
// skipped: no counterexamples, and optimize() returns verified=false with no code and no summary.
class Engine final : public IEngine {
public:
    explicit Engine(IPredictor& predictor, EngineOptions options = {});
    ~Engine() override = default;

    AnalysisResult analyze(const std::string& code, const ProblemSpec& spec, ProgressFn on_progress) override;
    OptimizeResult optimize(const std::string& code, const ProblemSpec& spec, ProgressFn on_progress) override;
    void cancel() override;

private:
    IPredictor& predictor_;
    EngineOptions options_;
    std::atomic<bool> cancelled_{false};
};

} // namespace nc
