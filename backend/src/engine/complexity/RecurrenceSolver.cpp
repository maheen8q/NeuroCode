#include "engine/complexity/RecurrenceSolver.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace nc::complexity {

namespace {

constexpr double kEps = 1e-9;

bool same(double a, double b) { return std::fabs(a - b) < kEps; }

std::string number(double d) {
    char buf[32];
    if (same(d, std::round(d))) std::snprintf(buf, sizeof buf, "%d", static_cast<int>(std::lround(d)));
    else if (same(d * 2, std::round(d * 2))) std::snprintf(buf, sizeof buf, "%.1f", d);
    else std::snprintf(buf, sizeof buf, "%.2f", d);
    return buf;
}

// An exponent that is a multiple of one half prints exactly; anything else is rounded to six places so that
// log_b(a) computed in floating point compares equal to the k of f(n) = n^k.
double snap(double x) {
    const double half = std::round(x * 2.0) / 2.0;
    if (std::fabs(x - half) < 1e-6) return half;
    return std::round(x * 1e6) / 1e6;
}

std::string polyPart(double e) {
    if (same(e, 0.0)) return "";
    if (same(e, 1.0)) return "n";
    if (same(e, 0.5)) return "sqrt(n)";
    return "n^" + number(e);
}

std::string logPart(int e) {
    if (e <= 0) return "";
    if (e == 1) return "log n";
    return "log^" + std::to_string(e) + " n";
}

std::string baseText(double b) {
    if (b <= 2.0 + kEps) return "2";
    return number(std::ceil(b - kEps) == b ? b : std::round(b * 100.0) / 100.0);
}

std::string countText(int c) { return c == 1 ? "" : std::to_string(c); }

std::string sizeText(const SubProblem& s) {
    return s.kind == SubProblem::Kind::Divide ? "n/" + number(s.factor) : "n-" + number(s.factor);
}

} // namespace

std::string Order::toString() const {
    if (unknown) return "O(?)";
    std::vector<std::string> parts;
    if (const std::string p = polyPart(n_exp); !p.empty()) parts.push_back(p);
    if (const std::string l = logPart(log_exp); !l.empty()) parts.push_back(l);
    if (exponential) {
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i) out += parts[i] + " * ";
        return "O(" + out + baseText(base) + "^n)";
    }
    if (parts.empty()) return "O(1)";
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? " " : "") + parts[i];
    return "O(" + out + ")";
}

int compare(const Order& a, const Order& b) {
    if (a.unknown || b.unknown) return a.unknown == b.unknown ? 0 : (a.unknown ? 1 : -1);
    if (a.exponential != b.exponential) return a.exponential ? 1 : -1;
    if (a.exponential && !same(a.base, b.base)) return a.base > b.base ? 1 : -1;
    if (!same(a.n_exp, b.n_exp)) return a.n_exp > b.n_exp ? 1 : -1;
    if (a.log_exp != b.log_exp) return a.log_exp > b.log_exp ? 1 : -1;
    return 0;
}

Order multiply(const Order& a, const Order& b) {
    if (a.unknown || b.unknown) return Order::unknownOrder();
    Order o;
    o.n_exp = a.n_exp + b.n_exp;
    o.log_exp = a.log_exp + b.log_exp;
    o.exponential = a.exponential || b.exponential;
    if (a.exponential && b.exponential) o.base = a.base * b.base;
    else if (a.exponential) o.base = a.base;
    else if (b.exponential) o.base = b.base;
    return o;
}

Order dominant(const Order& a, const Order& b) {
    if (a.unknown || b.unknown) return Order::unknownOrder();
    return compare(a, b) >= 0 ? a : b;
}

std::string describe(const Recurrence& r) {
    std::string out = "T(n) = ";
    bool first = true;
    // Group identical calls so that two T(n/2) print as 2T(n/2).
    std::map<std::pair<int, double>, int> groups;
    for (const SubProblem& s : r.calls) groups[{static_cast<int>(s.kind), s.factor}] += s.count;
    for (const auto& [key, count] : groups) {
        SubProblem s;
        s.kind = static_cast<SubProblem::Kind>(key.first);
        s.factor = key.second;
        out += (first ? "" : " + ") + countText(count) + "T(" + sizeText(s) + ")";
        first = false;
    }
    if (first) return out + r.work.toString();
    return out + " + " + r.work.toString();
}

namespace {

Solution fail(const Recurrence& r, const std::string& why) {
    Solution s;
    s.ok = false;
    s.method = "unsolved";
    s.steps = {describe(r), why};
    return s;
}

// f(n) is a polynomial times a power of a logarithm (the only shapes the master theorem covers) or exponential.
// Compares f with n^c and returns T(n) in the three Akra-Bazzi / master theorem cases.
Order combine(const Order& f, double c, std::string& rule) {
    if (f.exponential) {
        rule = "f(n) grows exponentially, faster than any n^c, so the root of the recursion tree dominates";
        return f;
    }
    if (f.n_exp < c - 1e-9) {
        rule = "f(n) grows slower than n^" + number(c) + ", so the leaves of the recursion tree dominate";
        return Order::polynomial(c);
    }
    if (same(f.n_exp, c)) {
        rule = "f(n) grows like n^" + number(c) + ", so every level of the recursion tree costs the same: " +
               "cost per level times the number of levels";
        Order o = Order::polynomial(c);
        o.log_exp = f.log_exp + 1;
        return o;
    }
    rule = "f(n) grows faster than n^" + number(c) + ", so the root of the recursion tree dominates";
    return f;
}

double akraBazziExponent(const std::vector<SubProblem>& calls) {
    // Solve  sum(count / factor^p) = 1  for p. The sum falls strictly from sum(count) >= 1 at p = 0.
    auto g = [&](double p) {
        double s = 0.0;
        for (const SubProblem& c : calls) s += c.count * std::pow(c.factor, -p);
        return s;
    };
    double lo = 0.0, hi = 64.0;
    for (int i = 0; i < 200; ++i) {
        const double mid = (lo + hi) / 2.0;
        (g(mid) > 1.0 ? lo : hi) = mid;
    }
    return snap((lo + hi) / 2.0);
}

Solution solveDivide(const Recurrence& r) {
    Solution s;
    std::map<double, int> by_factor;
    int total = 0;
    for (const SubProblem& c : r.calls) { by_factor[c.factor] += c.count; total += c.count; }
    s.steps.push_back(describe(r));

    double c;
    std::string rule;
    if (by_factor.size() == 1) {
        const double b = by_factor.begin()->first;
        c = snap(std::log(static_cast<double>(total)) / std::log(b));
        s.method = "master theorem";
        s.steps.push_back("a = " + std::to_string(total) + " calls on n/" + number(b) + ", so log_" + number(b) + "(" +
                          std::to_string(total) + ") = " + number(c));
    } else {
        c = akraBazziExponent(r.calls);
        s.method = "Akra-Bazzi";
        s.steps.push_back("the calls shrink n by different factors; the exponent p with sum(count / factor^p) = 1 is " + number(c));
    }
    const Order f = r.work;
    s.order = combine(f, c, rule);
    s.steps.push_back("compare f(n) = " + f.toString() + " with n^" + number(c) + ": " + rule);
    s.steps.push_back("T(n) = " + s.order.toString());
    s.ok = true;
    return s;
}

// Roots of  sum(count * x^-factor) = 1  with x > 1: how fast the number of calls grows per unit of n.
double growthBase(const std::vector<SubProblem>& calls) {
    auto g = [&](double x) {
        double s = 0.0;
        for (const SubProblem& c : calls) s += c.count * std::pow(x, -c.factor);
        return s;
    };
    double lo = 1.0, hi = 1.0;
    for (const SubProblem& c : calls) hi += c.count;
    for (int i = 0; i < 200; ++i) {
        const double mid = (lo + hi) / 2.0;
        (g(mid) > 1.0 ? lo : hi) = mid;
    }
    return (lo + hi) / 2.0;
}

Solution solveSubtract(const Recurrence& r) {
    Solution s;
    s.steps.push_back(describe(r));
    int total = 0;
    for (const SubProblem& c : r.calls) total += c.count;
    const Order f = r.work;

    if (total == 1) {
        s.method = "unrolling";
        if (f.exponential) {
            s.order = f;
            s.steps.push_back("f(n) grows exponentially, so the sum of f over n steps is dominated by its last term");
        } else {
            s.order = multiply(Order::linear(), f);
            s.steps.push_back("unrolling gives n terms: T(n) = f(n) + f(n-" + number(r.calls[0].factor) + ") + ... which adds up to n * " +
                              f.toString());
        }
        s.steps.push_back("T(n) = " + s.order.toString());
        s.ok = true;
        return s;
    }

    const double x = growthBase(r.calls);
    s.method = "exponential recursion";
    s.steps.push_back(std::to_string(total) + " calls per level and n shrinks slowly, so the call tree has about " + number(std::round(x * 1000) / 1000) +
                      "^n nodes");
    Order growth = Order::exponentialBase(x);
    if (f.exponential) {
        const int cmp = same(f.base, x) ? 0 : (f.base > x ? 1 : -1);
        s.order = cmp > 0 ? f : (cmp == 0 ? multiply(Order::linear(), f) : growth);
    } else {
        s.order = growth;                       // a polynomial f(n) cannot compete with x^n
    }
    if (x < 2.0 - 1e-9 && s.order.exponential && !f.exponential) {
        char tb[16];
        std::snprintf(tb, sizeof tb, "%.3f", x);
        s.steps.push_back(std::string("the tight base is ") + tb + "; it is reported as O(2^n), a valid upper bound");
    }
    s.steps.push_back("T(n) = " + s.order.toString());
    s.ok = true;
    return s;
}

} // namespace

Solution solveRecurrence(const Recurrence& r) {
    if (r.work.unknown) return fail(r, "the work done outside the recursive calls is not known");
    if (r.calls.empty()) {
        Solution s;
        s.ok = true;
        s.order = r.work;
        s.method = "not recursive";
        s.steps = {"no recursive calls: T(n) = " + r.work.toString()};
        return s;
    }

    bool any_divide = false, any_subtract = false;
    for (const SubProblem& c : r.calls) {
        if (c.count < 1) return fail(r, "a recursive call has a count below 1");
        if (c.kind == SubProblem::Kind::Divide) {
            if (!(c.factor > 1.0)) return fail(r, "a recursive call does not shrink the problem (n / " + number(c.factor) + ")");
            any_divide = true;
        } else {
            if (!(c.factor >= 1.0)) return fail(r, "a recursive call does not shrink the problem (n - " + number(c.factor) + ")");
            any_subtract = true;
        }
    }
    if (any_divide && any_subtract) return fail(r, "the calls mix n/b and n-c shrinking, which this solver does not handle");
    return any_divide ? solveDivide(r) : solveSubtract(r);
}

} // namespace nc::complexity
