#pragma once
#include <functional>
#include <string>

namespace nc::verify {

struct BenchmarkResult {
    double originalMs = 0.0;   // best (min) wall time of the original
    double optimizedMs = 0.0;  // best wall time of the optimized version
    double speedup = 0.0;      // originalMs / optimizedMs, 0 if not computable
    bool ok = false;           // true only if both versions ran cleanly
    std::string error;         // populated when ok is false
};

// Times original vs optimized code on the same input. Decoupled from the
// sandbox: T4 injects a Runner that compiles+runs code under Limits.hpp and
// returns elapsed ms (or throws on compile/runtime/limit failure).
class Benchmark {
public:
    using Runner = std::function<double(const std::string& code,
                                        const std::string& input)>;

    explicit Benchmark(Runner runner);

    // Runs each version `repeats` times, keeps the best time of each (min is
    // the standard choice: least noise from scheduling). repeats clamped >= 1.
    BenchmarkResult compare(const std::string& originalCode,
                            const std::string& optimizedCode,
                            const std::string& input,
                            int repeats = 5) const;

private:
    Runner runner_;
};

} // namespace nc::verify
