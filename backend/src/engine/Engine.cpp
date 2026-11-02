#include "engine/Engine.hpp"

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstring>
#include <exception>
#include <iomanip>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/ast/Node.hpp"
#include "engine/ast/Serializer.hpp"
#include "engine/cfg/Cfg.hpp"
#include "engine/cfg/CfgBuilder.hpp"
#include "engine/checks/StaticChecks.hpp"
#include "engine/complexity/SpaceComplexity.hpp"
#include "engine/complexity/TimeComplexity.hpp"
#include "engine/optimize/Catalog.hpp"
#include "engine/parse/AstBuilder.hpp"
#include "engine/parse/IoStripper.hpp"
#include "engine/verify/Oracle.hpp"
#include "ml/IPredictor.hpp"
#include "sandbox/Compiler.hpp"
#include "sandbox/Limits.hpp"
#include "sandbox/Runner.hpp"
#include "sandbox/ScratchDir.hpp"

namespace nc {

namespace {

// ---------------------------------------------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------------------------------------------

// Clears the cancel flag when a call returns, so an engine whose job was cancelled can run again.
// A cancel() that arrives BEFORE the call starts is kept (the job was cancelled while queued).
struct ClearCancelOnExit {
    std::atomic<bool>& flag;
    ~ClearCancelOnExit() { flag.store(false); }
};

struct Ctx {
    IPredictor& predictor;
    const EngineOptions& opt;
    const std::atomic<bool>& cancelled;
    const ProgressFn& on_progress;

    bool stop() const { return cancelled.load(); }
    void report(int percent, const char* stage) const {
        if (on_progress) on_progress(percent, stage);
    }
};

AnalysisResult failed(const std::string& error) {
    AnalysisResult r;
    r.success = false;
    r.error = error;
    return r;
}

OptimizeResult failedOptimize(const std::string& error) {
    OptimizeResult r;
    r.success = false;
    r.error = error;
    r.analysis = failed(error);
    return r;
}

int userLineCount(const std::string& code) {
    int lines = 0;
    for (char c : code) lines += (c == '\n');
    if (!code.empty() && code.back() != '\n') ++lines;
    return lines;
}

std::string humanize(const std::string& label) {
    std::string s = label;
    for (char& c : s) if (c == '_') c = ' ';
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

std::string fmtMs(double ms) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(ms < 10.0 ? 2 : 1) << ms << " ms";
    return o.str();
}

// "#include" lines are dropped from rewritten code: submitted code carries no includes (the preamble supplies
// them), so the code shown to the user must not either.
std::string stripIncludes(const std::string& code) {
    std::istringstream in(code);
    std::string line, out;
    bool started = false;
    while (std::getline(in, line)) {
        const auto first = line.find_first_not_of(" \t\r");
        if (first != std::string::npos && line.compare(first, 8, "#include") == 0) continue;
        if (!started && first == std::string::npos) continue;   // leading blank lines
        started = true;
        out += line;
        out += '\n';
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out.empty() ? out : out + "\n";
}

std::string recommendedAlgorithm(const std::string& pass_name) {
    static const std::map<std::string, std::string> known = {
        {"PairSumToHash", "hash_set_lookup"},
        {"LinearToBinarySearch", "binary_search"},
    };
    const auto it = known.find(pass_name);
    if (it != known.end()) return it->second;
    std::string snake;
    for (size_t i = 0; i < pass_name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(pass_name[i]);
        if (std::isupper(c) && i > 0) snake += '_';
        snake += static_cast<char>(std::tolower(c));
    }
    return snake;
}

// ---------------------------------------------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------------------------------------------

// Parses `code` (user code, no includes needed) into the internal AST. On failure `error` is the
// "CODE: message" the job fails with.
bool parseToAst(const Ctx& c, const std::string& code, ast::Node& out, std::string& error) {
    std::unique_ptr<parse::ParsedUnit> unit = parse::parseUserCode(code, c.opt.clang);
    if (!unit) {
        error = "PARSE_ERROR: libclang could not parse the code";
        return false;
    }
    for (const parse::Diagnostic& d : unit->diagnostics()) {
        if (!d.in_user_code) continue;
        error = "COMPILE_ERROR: line " + std::to_string(d.line) + ": " + d.message;
        return false;
    }
    out = parse::buildAst(*unit);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Differential testing: build the user's function into a program that follows the stdin protocol of
// verify/TestGenerator.hpp, run it on generated inputs, and let the Oracle judge the output.
// ---------------------------------------------------------------------------------------------------------------

struct Build {
    enum class Status { none, ok, skipped, user_error, sandbox_error };
    Status status = Status::none;
    std::string message;   // why skipped / the compile error / the sandbox problem
    std::string binary;    // set when ok
    std::string entry;     // name of the function the harness calls
};

struct Job {
    std::string code;
    ProblemSpec spec;
    ast::Node ast;
    std::vector<cfg::Cfg> cfgs;
    std::unique_ptr<ScratchDir> dir;
    Build original;
    std::vector<optimize::OptimizationResult> found;
    verify::TestGenerator gen;
    verify::Oracle oracle;

    Job(const std::string& source, const ProblemSpec& problem, const EngineOptions& options)
        : code(source), spec(problem), gen(problem, options.generator), oracle(problem) {}
};

int expectedParamCount(verify::Operation op) {
    return op == verify::Operation::Fibonacci ? 1 : 2;
}

std::string harnessFor(verify::Operation op, const std::string& entry) {
    std::string h =
        "\n// ---- NeuroCode test harness (not part of the submitted code) ----\n"
        "namespace nc_h {\n"
        "template <class T> void put(const T& v) { std::cout << v << '\\n'; }\n"
        "inline void put(bool v) { std::cout << (v ? \"true\" : \"false\") << '\\n'; }\n"
        "template <class T> void put(const std::vector<T>& v) { for (const auto& x : v) std::cout << x << '\\n'; }\n"
        "}\n"
        "int main() {\n"
        "    std::ios::sync_with_stdio(false);\n"
        "    std::cin.tie(nullptr);\n";
    switch (op) {
        case verify::Operation::PairSum:
        case verify::Operation::Search:
        case verify::Operation::SearchSorted:
            h += "    int n = 0; long long param = 0;\n"
                 "    std::cin >> n >> param;\n"
                 "    std::vector<int> a(n > 0 ? n : 0);\n"
                 "    for (int& x : a) std::cin >> x;\n"
                 "    nc_h::put(" + entry + "(a, static_cast<int>(param)));\n";
            break;
        case verify::Operation::RangeSum:
            h += "    int n = 0, q = 0;\n"
                 "    std::cin >> n >> q;\n"
                 "    std::vector<int> a(n > 0 ? n : 0);\n"
                 "    for (int& x : a) std::cin >> x;\n"
                 "    std::vector<std::pair<int, int>> qs(q > 0 ? q : 0);\n"
                 "    for (auto& p : qs) std::cin >> p.first >> p.second;\n"
                 "    nc_h::put(" + entry + "(a, qs));\n";
            break;
        case verify::Operation::Fibonacci:
            h += "    long long n = 0;\n"
                 "    std::cin >> n;\n"
                 "    nc_h::put(" + entry + "(static_cast<int>(n)));\n";
            break;
        case verify::Operation::Unknown:
            break;
    }
    h += "    return 0;\n}\n";
    return h;
}

Build skipped(const std::string& why) {
    Build b;
    b.status = Build::Status::skipped;
    b.message = why;
    return b;
}

Build buildProgram(const Ctx& c, Job& job, const ast::Node& root, const std::string& code, const std::string& name) {
    const verify::Operation op = job.gen.operation();
    if (!job.gen.supported()) return skipped("operation '" + job.spec.operation + "' is not supported by differential testing");

    const complexity::ProgramIndex index = complexity::buildIndex(root);
    const std::string entry = complexity::chooseEntry(index);
    if (entry.empty()) return skipped("no function found");
    if (index.by_name.count("main")) return skipped("the code defines its own main(), so it cannot be driven by generated inputs");

    const ast::Node* fn = index.by_name.at(entry);
    int params = 0;
    for (const ast::Node& ch : fn->children) params += (ch.type == "PARM_DECL");
    if (params != expectedParamCount(op))
        return skipped("function '" + entry + "' takes " + std::to_string(params) + " parameter(s), the operation '" +
                       std::string(verify::toString(op)) + "' expects " + std::to_string(expectedParamCount(op)));

    if (!job.dir) {
        try {
            job.dir = std::make_unique<ScratchDir>(c.opt.run_as_uid, c.opt.scratch_base);
        } catch (const std::exception& e) {
            Build b;
            b.status = Build::Status::sandbox_error;
            b.message = std::string("could not create a scratch directory: ") + e.what();
            return b;
        }
    }

    Limits compile_limits = Limits::forCompile();
    compile_limits.run_as_uid = c.opt.run_as_uid;
    const std::string source = wrapUserSource(code + "\n") + harnessFor(op, entry);
    const CompileResult cr = compileCpp(source, job.dir->path(), name, compile_limits);

    Build b;
    b.entry = entry;
    if (cr.ok) {
        b.status = Build::Status::ok;
        b.binary = cr.binary_path;
        return b;
    }
    if (cr.sandbox_error) {
        b.status = Build::Status::sandbox_error;
        b.message = cr.error_summary.empty() ? "g++ could not be started" : cr.error_summary;
        return b;
    }
    if (cr.timed_out) return skipped("the compiler ran out of time");

    // An error on a user line is the user's. One on a harness line means the signature does not fit the
    // operation (a return type or parameter the harness cannot use): the program cannot be tested, that is all.
    static const std::regex kLine(R"(^line (\d+):)");
    std::smatch m;
    if (std::regex_search(cr.error_summary, m, kLine) && std::stoi(m[1].str()) <= userLineCount(code)) {
        b.status = Build::Status::user_error;
        b.message = cr.error_summary;
        return b;
    }
    return skipped("the function does not fit the operation's signature (" + cr.error_summary + ")");
}

struct Outcome {
    bool pass = true;
    bool timed_out = false;
    bool sandbox_failure = false;
    std::string expected;
    std::string actual;
    long peak_kb = 0;
};

Outcome runCase(const Ctx& c, const Job& job, const std::string& binary, const verify::TestCase& tc) {
    Limits limits = Limits::forRun(c.opt.test_timeout_ms);
    limits.run_as_uid = c.opt.run_as_uid;
    const RunResult r = runBinary(binary, tc.stdin_text, limits, job.dir->path());

    Outcome o;
    o.peak_kb = r.peak_kb;
    o.expected = job.oracle.expectedOutput(tc);
    switch (r.status) {
        case RunStatus::ok: {
            const verify::Verdict v = job.oracle.check(tc, r.stdout_text);
            o.pass = v.passed();
            o.expected = v.expected;
            o.actual = v.actual;
            if (v.status == verify::Verdict::Status::Malformed && !v.detail.empty()) o.actual = v.actual.empty() ? v.detail : v.actual;
            break;
        }
        case RunStatus::nonzero_exit:
            o.pass = false;
            o.actual = "exited with code " + std::to_string(r.exit_code);
            break;
        case RunStatus::signaled: {
            o.pass = false;
            const char* sig = strsignal(r.term_signal);
            o.actual = "crashed (" + std::string(sig ? sig : "signal " + std::to_string(r.term_signal)) + ")";
            break;
        }
        case RunStatus::timed_out:
            o.pass = false;
            o.timed_out = true;
            o.actual = "timed out after " + std::to_string(c.opt.test_timeout_ms) + " ms";
            break;
        case RunStatus::output_limit:
            o.pass = false;
            o.actual = "printed too much output";
            break;
        case RunStatus::start_failed:
            o.pass = false;
            o.sandbox_failure = true;
            o.actual = r.error;
            break;
    }
    return o;
}

struct DiffReport {
    int tests_run = 0;
    int failed = 0;
    std::vector<Counterexample> examples;
    std::string sandbox_error;      // set when the sandbox itself failed
};

// Runs every generated test against `binary`. Stops after `max_examples` failures (or at the first timeout, which
// would repeat on every larger case). Failing inputs are shrunk when `shrink` is set.
DiffReport differentialTest(const Ctx& c, const Job& job, const std::string& binary, int max_examples, bool shrink) {
    DiffReport report;
    struct Failure { verify::TestCase tc; Outcome out; };
    std::vector<Failure> failures;

    for (const verify::TestCase& tc : job.gen.generate()) {
        if (c.stop()) return report;
        const Outcome o = runCase(c, job, binary, tc);
        ++report.tests_run;
        if (o.sandbox_failure) {
            report.sandbox_error = o.actual;
            return report;
        }
        if (o.pass) continue;
        ++report.failed;
        failures.push_back({tc, o});
        if (o.timed_out || static_cast<int>(failures.size()) >= max_examples) break;
    }

    std::vector<std::string> seen;
    for (size_t i = 0; i < failures.size(); ++i) {
        if (c.stop()) return report;
        verify::TestCase tc = failures[i].tc;
        Outcome out = failures[i].out;
        if (shrink && !out.timed_out) {
            const auto fails = [&](const verify::TestCase& cand) {
                const Outcome r = runCase(c, job, binary, cand);
                return !r.pass && !r.timed_out && !r.sandbox_failure;
            };
            const verify::TestCase small = job.gen.shrink(tc, fails, i == 0 ? 150 : 50);
            const Outcome again = runCase(c, job, binary, small);
            if (!again.pass && !again.sandbox_failure) {      // keep the original if the small case turns out flaky
                tc = small;
                out = again;
            }
        }
        if (std::find(seen.begin(), seen.end(), tc.display) != seen.end()) continue;
        seen.push_back(tc.display);
        report.examples.push_back({tc.display, out.expected, out.actual});
    }
    return report;
}

// ---------------------------------------------------------------------------------------------------------------
// Analysis
// ---------------------------------------------------------------------------------------------------------------

// The AST and CFG JSON the predictor sees. Contest-style code is stripped of its I/O first, exactly as the
// training pipeline did; the UI keeps the original. If the stripped code does not parse, the original is used.
void featureInputs(const Ctx& c, const Job& job, const std::string& ast_json, const std::string& cfg_json,
                   std::string& feat_ast, std::string& feat_cfg) {
    feat_ast = ast_json;
    feat_cfg = cfg_json;
    if (!parse::needsStrip(job.code)) return;

    std::unique_ptr<parse::ParsedUnit> unit = parse::parseUserCode(parse::stripIoCode(job.code), c.opt.clang);
    if (!unit || unit->hasErrors()) return;
    const ast::Node stripped = parse::buildAst(*unit);
    feat_ast = ast::toJsonString(stripped);
    feat_cfg = cfg::cfgsToJsonString(cfg::buildCfgs(stripped));
}

std::vector<optimize::OptimizationResult> findOptimizations(const Ctx& c, const Job& job) {
    try {
        const optimize::Catalog fallback;
        const optimize::Catalog& catalog = c.opt.catalog ? *c.opt.catalog : fallback;
        optimize::PassContext pc;
        pc.ast = &job.ast;            // const ast::Node*
        pc.cfg = &job.cfgs;           // const std::vector<cfg::Cfg>*
        pc.sourceCode = job.code;
        return catalog.run(pc);
    } catch (...) {
        return {};                    // a broken pass must not take the analysis down
    }
}

// Stages and percentages follow api-spec section 10; a step reports when it is DONE.
AnalysisResult runAnalysis(const Ctx& c, Job& job) {
    AnalysisResult result;
    try {
        if (c.stop()) return failed("cancelled");

        std::string error;
        if (!parseToAst(c, job.code, job.ast, error)) return failed(error);
        c.report(10, "clang AST");
        if (c.stop()) return failed("cancelled");

        result.ast_json = ast::toJsonString(job.ast);
        c.report(20, "internal AST");
        if (c.stop()) return failed("cancelled");

        job.cfgs = cfg::buildCfgs(job.ast);
        result.cfg_json = cfg::cfgsToJsonString(job.cfgs);
        c.report(30, "control flow graph");
        if (c.stop()) return failed("cancelled");

        result.complexity = complexity::analyzeComplexity(job.ast);
        c.report(45, "complexity analysis");
        if (c.stop()) return failed("cancelled");

        std::string feat_ast, feat_cfg;
        featureInputs(c, job, result.ast_json, result.cfg_json, feat_ast, feat_cfg);
        const std::vector<float> features = c.predictor.extractFeatures(feat_ast, feat_cfg);
        c.report(55, "feature extraction");
        if (c.stop()) return failed("cancelled");

        result.algorithms = c.predictor.predict(features, 3);
        if (result.algorithms.empty()) return failed("MODEL_UNAVAILABLE: no active model");
        c.report(65, "algorithm classification");
        if (c.stop()) return failed("cancelled");

        result.findings = checks::runChecks(job.ast, job.cfgs);
        job.found = findOptimizations(c, job);
        result.optimization_available = !job.found.empty();
        c.report(75, "static checks");
        if (c.stop()) return failed("cancelled");

        if (c.opt.differential_testing && job.gen.supported()) {
            job.original = buildProgram(c, job, job.ast, job.code, "orig");
            switch (job.original.status) {
                case Build::Status::user_error:
                    return failed("COMPILE_ERROR: " + job.original.message);
                case Build::Status::sandbox_error:
                    return failed("SANDBOX_ERROR: " + job.original.message);
                case Build::Status::ok: {
                    if (c.stop()) return failed("cancelled");
                    const DiffReport report = differentialTest(c, job, job.original.binary, c.opt.max_counterexamples, true);
                    if (c.stop()) return failed("cancelled");
                    if (!report.sandbox_error.empty()) return failed("SANDBOX_ERROR: " + report.sandbox_error);
                    result.counterexamples = report.examples;
                    break;
                }
                case Build::Status::skipped:
                case Build::Status::none:
                    break;
            }
        }
        c.report(90, "differential testing");
    } catch (const std::exception& e) {
        return failed(std::string("INTERNAL: ") + e.what());
    } catch (...) {
        return failed("INTERNAL: unknown error");
    }
    result.success = true;
    return result;
}

// ---------------------------------------------------------------------------------------------------------------
// Optimization
// ---------------------------------------------------------------------------------------------------------------

long long benchmarkSize(verify::Operation op, const complexity::Order& original_time) {
    if (op == verify::Operation::Fibonacci) return original_time.exponential ? 32 : 90;
    if (original_time.unknown || original_time.exponential || original_time.n_exp >= 2.0) return 20000;
    return 200000;
}

long long sizeOf(const verify::TestCase& tc, verify::Operation op) {
    return op == verify::Operation::Fibonacci ? tc.param : static_cast<long long>(tc.a.size());
}

struct Verified {
    std::string code;                // as shown to the user: no includes
    ast::Node ast;
    Build build;
    int tests = 0;
    const optimize::OptimizationResult* rule = nullptr;
};

// Compiles the candidate and runs the generated tests against it. True only if it builds and EVERY test passes.
bool verifyCandidate(const Ctx& c, Job& job, const optimize::OptimizationResult& r, size_t index, Verified& v) {
    v.code = stripIncludes(r.rewrittenCode);
    if (v.code.empty()) { std::fprintf(stderr, "VC: empty code\n"); return false; }
    std::string error;
    if (!parseToAst(c, v.code, v.ast, error)) { std::fprintf(stderr, "VC: parse failed: %s\n", error.c_str()); return false; }
    v.build = buildProgram(c, job, v.ast, v.code, "opt" + std::to_string(index));
    if (v.build.status != Build::Status::ok) { std::fprintf(stderr, "VC: build not ok, status=%d msg=%s\n", (int)v.build.status, v.build.message.c_str()); return false; }
    const DiffReport report = differentialTest(c, job, v.build.binary, 1, false);
    v.tests = report.tests_run;
    std::fprintf(stderr, "VC: tests_run=%d failed=%d sandbox_err=%s\n", report.tests_run, report.failed, report.sandbox_error.c_str());
    if (c.stop() || !report.sandbox_error.empty() || report.failed > 0 || report.tests_run == 0) return false;
    v.rule = &r;
    return true;
}

OptimizeResult optimizeJob(const Ctx& c, Job& job, OptimizeResult out) {
    const verify::Operation op = job.gen.operation();

    // Never claim a verified optimization we could not test (api-spec: unverified code is never shown).
    if (job.found.empty() || job.original.status != Build::Status::ok) {
        out.success = true;
        out.verified = false;
        c.report(100, "optimization");
        return out;
    }

    c.report(92, "optimization");
    Verified best;
    bool verified = false;
    for (size_t i = 0; i < job.found.size() && !verified; ++i) {
        if (c.stop()) return failedOptimize("cancelled");
        Verified v;
        if (verifyCandidate(c, job, job.found[i], i, v)) {
            best = std::move(v);
            verified = true;
        }
    }
    if (c.stop()) return failedOptimize("cancelled");
    if (!verified) {
        out.success = true;
        out.verified = false;
        c.report(100, "optimization");
        return out;
    }
    c.report(96, "benchmark");

    const optimize::OptimizationResult& rule = *best.rule;

    // ---- benchmark: worst-case input, best of N, both versions already compiled ----------------------------
    const complexity::Order original_time = complexity::analyzeTime(job.ast).time;
    const verify::TestCase worst = job.gen.worstCase(benchmarkSize(op, original_time));

    std::unordered_map<std::string, std::string> binaries{{job.code, job.original.binary}, {best.code, best.build.binary}};
    std::unordered_map<std::string, long> peaks;
    const verify::Benchmark::Runner runner = [&](const std::string& code, const std::string& input) -> double {
        const auto it = binaries.find(code);
        if (it == binaries.end()) throw std::runtime_error("no compiled binary for this code");
        Limits limits = Limits::forBenchmark();
        limits.run_as_uid = c.opt.run_as_uid;
        const RunResult r = runBinary(it->second, input, limits, job.dir->path());
        if (r.status != RunStatus::ok)
            throw std::runtime_error(std::string("benchmark run failed: ") + toString(r.status) + (r.error.empty() ? "" : " (" + r.error + ")"));
        peaks[code] = std::max(peaks[code], r.peak_kb);
        return r.wall_ms;
    };
    const verify::BenchmarkResult bench = verify::Benchmark(runner).compare(job.code, best.code, worst.stdin_text, c.opt.benchmark_repeats);
    if (c.stop()) return failedOptimize("cancelled");

    // ---- complexity of the rewritten code ---------------------------------------------------------------------------
    std::string time_before = out.analysis.complexity.time;
    if (time_before.empty() || time_before == "unknown") time_before = rule.beforeComplexity;
    std::string time_after = rule.afterComplexity;
    const Complexity after = complexity::analyzeComplexity(best.ast);
    if (time_after.empty()) time_after = after.time;
    const std::string space_before = out.analysis.complexity.space;
    const std::string space_after = after.space;
    const int space_cmp = complexity::compare(complexity::analyzeSpace(best.ast).space, complexity::analyzeSpace(job.ast).space);

    // ---- result -----------------------------------------------------------------------------------------------------
    out.verified = true;
    out.optimized_code = best.code;
    out.recommended_algorithm = recommendedAlgorithm(rule.passName);
    out.recommended_complexity = time_after;

    const std::string before_label = out.analysis.algorithms.empty() ? "Original" : humanize(out.analysis.algorithms.front().label);
    out.summary.headline = rule.title;
    out.summary.explanation = rule.rationale;
    out.summary.comparison = {
        {"Algorithm", before_label, humanize(out.recommended_algorithm)},
        {"Time", time_before, time_after},
        {"Space", space_before, space_after},
    };
    out.summary.changes = {rule.title + " (" + time_before + " to " + time_after + ")"};
    if (space_before != space_after) out.summary.changes.push_back("Auxiliary space changes from " + space_before + " to " + space_after);

    if (bench.ok) {
        const long before_kb = peaks[job.code];
        const long after_kb = peaks[best.code];
        out.bench.n_used = sizeOf(worst, op);
        out.bench.before_ms = bench.originalMs;
        out.bench.after_ms = bench.optimizedMs;
        out.bench.before_kb = before_kb;
        out.bench.after_kb = after_kb;
        out.summary.speedup = bench.speedup;
        out.summary.comparison.push_back({"Runtime at n = " + std::to_string(out.bench.n_used), fmtMs(bench.originalMs), fmtMs(bench.optimizedMs)});
        if (before_kb > 0 && after_kb > 0)
            out.summary.comparison.push_back({"Memory", std::to_string(before_kb) + " KB", std::to_string(after_kb) + " KB"});
    }

    if (space_cmp > 0) {
        out.summary.tradeoff = "Auxiliary memory grows from " + space_before + " to " + space_after + ".";
        if (bench.ok && out.bench.before_kb > 0 && out.bench.after_kb > 0)
            out.summary.tradeoff += " Measured peak memory at n = " + std::to_string(out.bench.n_used) + ": " +
                                    std::to_string(out.bench.before_kb) + " KB before, " + std::to_string(out.bench.after_kb) + " KB after.";
    }

    std::ostringstream ver;
    ver << "Compiled successfully and matched the reference answer on " << best.tests
        << " generated inputs, including edge cases (empty array, single element, all equal values, values near INT_MAX)."
        << " This is testing, not a proof.";
    if (!out.analysis.counterexamples.empty())
        ver << " The original code gave a wrong answer on some of these inputs; the optimized version does not.";
    out.summary.verification = ver.str();

    out.success = true;
    c.report(100, "optimization");
    return out;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------------------------------------------

Engine::Engine(IPredictor& predictor, EngineOptions options)
    : predictor_(predictor), options_(std::move(options)) {}

AnalysisResult Engine::analyze(const std::string& code, const ProblemSpec& spec, ProgressFn on_progress) {
    ClearCancelOnExit clear{cancelled_};
    const Ctx c{predictor_, options_, cancelled_, on_progress};
    try {
        Job job(code, spec, options_);
        AnalysisResult result = runAnalysis(c, job);
        if (result.success) c.report(100, "complete");
        return result;
    } catch (const std::exception& e) {
        return failed(std::string("INTERNAL: ") + e.what());
    } catch (...) {
        return failed("INTERNAL: unknown error");
    }
}

OptimizeResult Engine::optimize(const std::string& code, const ProblemSpec& spec, ProgressFn on_progress) {
    ClearCancelOnExit clear{cancelled_};
    const Ctx c{predictor_, options_, cancelled_, on_progress};
    try {
        Job job(code, spec, options_);
        OptimizeResult out;
        out.analysis = runAnalysis(c, job);
        if (!out.analysis.success) {
            out.success = false;
            out.error = out.analysis.error;
            return out;
        }
        return optimizeJob(c, job, std::move(out));
    } catch (const std::exception& e) {
        return failedOptimize(std::string("INTERNAL: ") + e.what());
    } catch (...) {
        return failedOptimize("INTERNAL: unknown error");
    }
}

void Engine::cancel() {
    cancelled_.store(true);
}

} // namespace nc
