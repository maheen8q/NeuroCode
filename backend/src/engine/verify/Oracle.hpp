#pragma once
#include <string>
#include <vector>

#include "engine/IEngine.hpp"          // ProblemSpec
#include "engine/verify/TestGenerator.hpp"

namespace nc::verify {

// The reference answer for a test case, computed in-process with 64-bit arithmetic, so a user's `int` that
// overflows shows up as a wrong answer (the "maximum values" cases exist for exactly that).
//
// The oracle works from ProblemSpec::operation, so it also covers custom problems that come without a
// reference solution. Operations it does not know are unsupported; never guess a reference.
struct Verdict {
    enum class Status {
        Pass,        // the output is a correct answer
        Fail,        // a well-formed output that is wrong: this is a counterexample
        Malformed,   // not a valid answer for this operation (wrong format, extra or missing output)
    };
    Status status = Status::Pass;
    std::string expected;   // canonical expected output (for search: the first valid index, or -1)
    std::string actual;     // the program's output, trimmed and cut to 200 characters
    std::string detail;     // one line: why it failed ("expected true", "query 3: expected 42, got 41", ...)
    bool passed() const { return status == Status::Pass; }
};

class Oracle {
public:
    explicit Oracle(const ProblemSpec& spec);

    bool supported() const { return op_ != Operation::Unknown; }
    Operation operation() const { return op_; }

    // The expected output as the program should print it. Empty string when the operation is unsupported
    // or the case is outside the oracle's range (fibonacci n > 92 does not fit in 64 bits).
    std::string expectedOutput(const TestCase& tc) const;

    // Judge the program's stdout. Tolerant of whitespace and line endings. For search / search_sorted ANY
    // index holding the target is correct, and -1 is correct only when the target is absent.
    Verdict check(const TestCase& tc, const std::string& program_output) const;

private:
    Operation op_;
};

// The answers themselves, exposed so tests and Benchmark can reuse them.
bool pairSumExists(const std::vector<int>& a, int k);                       // i < j, a[i] + a[j] == k, no overflow
int firstIndexOf(const std::vector<int>& a, int target);                    // -1 if absent
std::vector<long long> rangeSums(const std::vector<int>& a, const std::vector<Query>& queries);
long long fibonacciNumber(int n);                                           // 0 <= n <= 92

} // namespace nc::verify
