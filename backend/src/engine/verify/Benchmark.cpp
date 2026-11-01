#include "Benchmark.hpp"
#include <limits>

namespace nc::verify {

Benchmark::Benchmark(Runner runner) : runner_(std::move(runner)) {}

// Best-of-N for one version. Returns min ms, or throws upward on failure.
static double bestOf(const Benchmark::Runner& run,
                     const std::string& code,
                     const std::string& input,
                     int repeats) {
    double best = std::numeric_limits<double>::max();
    for (int i = 0; i < repeats; ++i) {
        double ms = run(code, input);
        if (ms < best) best = ms;
    }
    return best;
}

BenchmarkResult Benchmark::compare(const std::string& originalCode,
                                   const std::string& optimizedCode,
                                   const std::string& input,
                                   int repeats) const {
    BenchmarkResult r;
    if (repeats < 1) repeats = 1;
    if (!runner_) { r.error = "no runner injected"; return r; }

    try {
        r.originalMs = bestOf(runner_, originalCode, input, repeats);
        r.optimizedMs = bestOf(runner_, optimizedCode, input, repeats);
        if (r.optimizedMs > 0.0) r.speedup = r.originalMs / r.optimizedMs;
        r.ok = true;
    } catch (const std::exception& e) {
        r.ok = false;
        r.error = e.what();
    } catch (...) {
        r.ok = false;
        r.error = "unknown benchmark failure";
    }
    return r;
}

} // namespace nc::verify
