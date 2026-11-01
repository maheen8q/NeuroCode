#include "engine/verify/Oracle.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <optional>
#include <sstream>
#include <unordered_set>

namespace nc::verify {

bool pairSumExists(const std::vector<int>& a, int k) {
    std::unordered_set<long long> seen;                       // 64-bit: no overflow for any int inputs
    for (int x : a) {
        if (seen.count(static_cast<long long>(k) - x)) return true;   // an EARLIER element: indices are distinct
        seen.insert(x);
    }
    return false;
}

int firstIndexOf(const std::vector<int>& a, int target) {
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] == target) return static_cast<int>(i);
    return -1;
}

std::vector<long long> rangeSums(const std::vector<int>& a, const std::vector<Query>& queries) {
    std::vector<long long> prefix(a.size() + 1, 0);
    for (size_t i = 0; i < a.size(); ++i) prefix[i + 1] = prefix[i] + a[i];
    std::vector<long long> out;
    out.reserve(queries.size());
    for (const Query& q : queries) out.push_back(prefix[static_cast<size_t>(q.r) + 1] - prefix[static_cast<size_t>(q.l)]);
    return out;
}

long long fibonacciNumber(int n) {
    long long a = 0, b = 1;                                   // F(0), F(1)
    for (int i = 1; i < n; ++i) {                             // never computes F(n + 1): F(93) would overflow
        const long long next = a + b;
        a = b;
        b = next;
    }
    return n <= 0 ? 0 : b;
}

namespace {

std::vector<std::string> tokens(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    for (std::string t; in >> t;) out.push_back(t);
    return out;
}

std::string clip(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    std::string t = s.substr(b, e - b);
    if (t.size() > 200) t = t.substr(0, 200) + "...";
    return t;
}

// Strict integer: optional sign, digits, nothing else.
std::optional<long long> parseInteger(const std::string& t) {
    long long v = 0;
    const char* first = t.data() + (!t.empty() && t[0] == '+' ? 1 : 0);
    const auto [ptr, ec] = std::from_chars(first, t.data() + t.size(), v);
    if (ec != std::errc() || ptr != t.data() + t.size() || first == t.data() + t.size()) return std::nullopt;
    return v;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

Oracle::Oracle(const ProblemSpec& spec) : op_(parseOperation(spec.operation)) {}

std::string Oracle::expectedOutput(const TestCase& tc) const {
    switch (op_) {
        case Operation::PairSum:
            return pairSumExists(tc.a, tc.param) ? "true" : "false";
        case Operation::Search:
        case Operation::SearchSorted:
            return std::to_string(firstIndexOf(tc.a, tc.param));
        case Operation::RangeSum: {
            std::string out;
            for (long long v : rangeSums(tc.a, tc.queries)) out += (out.empty() ? "" : "\n") + std::to_string(v);
            return out;
        }
        case Operation::Fibonacci:
            return tc.param >= 0 && tc.param <= 92 ? std::to_string(fibonacciNumber(tc.param)) : std::string();
        case Operation::Unknown:
            break;
    }
    return {};
}

Verdict Oracle::check(const TestCase& tc, const std::string& program_output) const {
    Verdict v;
    v.expected = expectedOutput(tc);
    v.actual = clip(program_output);
    auto malformed = [&](const std::string& why) { v.status = Verdict::Status::Malformed; v.detail = why; return v; };
    auto fail = [&](const std::string& why) { v.status = Verdict::Status::Fail; v.detail = why; return v; };

    if (op_ == Operation::Unknown) return malformed("operation not supported by the oracle");
    const auto toks = tokens(program_output);

    switch (op_) {
        case Operation::PairSum: {
            if (toks.size() != 1) return malformed("expected a single true or false, got " + std::to_string(toks.size()) + " token(s)");
            const std::string t = lower(toks[0]);
            if (t != "true" && t != "false" && t != "1" && t != "0") return malformed("expected true or false, got '" + clip(toks[0]) + "'");
            const bool got = t == "true" || t == "1";
            const bool want = pairSumExists(tc.a, tc.param);
            if (got != want) return fail(std::string("expected ") + (want ? "true" : "false") + ", got " + (got ? "true" : "false"));
            return v;
        }
        case Operation::Search:
        case Operation::SearchSorted: {
            if (toks.size() != 1) return malformed("expected a single index, got " + std::to_string(toks.size()) + " token(s)");
            const auto idx = parseInteger(toks[0]);
            if (!idx) return malformed("expected an integer index, got '" + clip(toks[0]) + "'");
            const int first = firstIndexOf(tc.a, tc.param);
            if (*idx == -1) {
                if (first != -1) return fail("returned -1 but " + std::to_string(tc.param) + " is at index " + std::to_string(first));
                return v;
            }
            if (*idx < 0 || *idx >= static_cast<long long>(tc.a.size())) return fail("index " + std::to_string(*idx) + " is outside the array");
            if (tc.a[static_cast<size_t>(*idx)] != tc.param)
                return fail("a[" + std::to_string(*idx) + "] is " + std::to_string(tc.a[static_cast<size_t>(*idx)]) + ", not " + std::to_string(tc.param));
            return v;
        }
        case Operation::RangeSum: {
            if (toks.size() != tc.queries.size())
                return malformed("expected " + std::to_string(tc.queries.size()) + " answer(s), got " + std::to_string(toks.size()));
            const auto want = rangeSums(tc.a, tc.queries);
            for (size_t i = 0; i < toks.size(); ++i) {
                const auto got = parseInteger(toks[i]);
                if (!got) return malformed("answer " + std::to_string(i) + " is not an integer: '" + clip(toks[i]) + "'");
                if (*got != want[i])
                    return fail("query " + std::to_string(i) + " (" + std::to_string(tc.queries[i].l) + ".." + std::to_string(tc.queries[i].r) +
                                "): expected " + std::to_string(want[i]) + ", got " + std::to_string(*got));
            }
            return v;
        }
        case Operation::Fibonacci: {
            if (v.expected.empty()) return malformed("n = " + std::to_string(tc.param) + " is outside the oracle's range (0..92)");
            if (toks.size() != 1) return malformed("expected a single number, got " + std::to_string(toks.size()) + " token(s)");
            const auto got = parseInteger(toks[0]);
            if (!got) return malformed("expected an integer, got '" + clip(toks[0]) + "'");
            if (std::to_string(*got) != v.expected) return fail("expected " + v.expected + ", got " + std::to_string(*got));
            return v;
        }
        case Operation::Unknown:
            break;
    }
    return malformed("operation not supported by the oracle");
}

} // namespace nc::verify
