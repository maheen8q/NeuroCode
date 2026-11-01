#pragma once
#include <string>
#include <vector>

namespace nc::complexity {

// An asymptotic order in one variable n, the size of the input:   n^n_exp * (log n)^log_exp  or, when
// `exponential`, the same polynomial factor times base^n. One variable on purpose: every array length, query
// count, value bound ... of the user's function is "n" (the catalog problems have one size parameter).
// `unknown` is absorbing: an unknown factor makes any product or maximum unknown.
struct Order {
    double n_exp = 0.0;          // 0 = constant, 1 = linear, 0.5 = square root, 2 = quadratic, ...
    int log_exp = 0;             // power of log n
    bool exponential = false;
    double base = 2.0;           // only used when exponential
    bool unknown = false;

    static Order constant() { return {}; }
    static Order logarithmic() { Order o; o.log_exp = 1; return o; }
    static Order sqrtN() { Order o; o.n_exp = 0.5; return o; }
    static Order linear() { Order o; o.n_exp = 1.0; return o; }
    static Order linearithmic() { Order o; o.n_exp = 1.0; o.log_exp = 1; return o; }
    static Order polynomial(double k) { Order o; o.n_exp = k; return o; }
    static Order exponentialBase(double b) { Order o; o.exponential = true; o.base = b; return o; }
    static Order unknownOrder() { Order o; o.unknown = true; return o; }

    bool isConstant() const { return !unknown && !exponential && n_exp == 0.0 && log_exp == 0; }

    // "O(1)", "O(log n)", "O(sqrt(n))", "O(n)", "O(n log n)", "O(n^2)", "O(n^2 log n)", "O(2^n)", "O(n * 2^n)", "O(?)".
    // An exponential base in (1, 2] prints as 2: O() is an upper bound, and 2^n is the conventional bound for
    // Fibonacci-like recursion whose tight base is 1.618...
    std::string toString() const;
};

// -1, 0, +1. Unknown orders compare equal to each other and greater than everything else.
int compare(const Order& a, const Order& b);

Order multiply(const Order& a, const Order& b);       // cost of a loop: trips * body
Order dominant(const Order& a, const Order& b);       // cost of a sequence or a branch: the larger one

// ---- recurrences:  T(n) = sum of (count x T(smaller n)) + work(n) --------------------------------------------

struct SubProblem {
    enum class Kind {
        Divide,      // the call is on n / factor            (merge sort: factor 2)
        Subtract,    // the call is on n - factor            (factorial: factor 1)
    };
    Kind kind = Kind::Divide;
    double factor = 2.0;        // Divide: > 1.  Subtract: >= 1.
    int count = 1;              // how many such calls one invocation makes
};

struct Recurrence {
    std::vector<SubProblem> calls;     // empty: not recursive
    Order work;                        // f(n): the cost of one call outside its recursive calls
};

struct Solution {
    bool ok = false;                   // false: the recurrence has a shape the solver does not handle
    Order order = Order::unknownOrder();
    std::string method;                // "master theorem, case 2", "Akra-Bazzi", "unrolling", ...
    std::vector<std::string> steps;    // derivation lines for the UI, first line is the recurrence itself
};

// Solves:
//  - only Divide calls (one or several divisors): Master theorem / Akra-Bazzi, f(n) = n^k log^q n or exponential;
//  - only Subtract calls: one call unrolls to a sum, several calls give an exponential whose base solves
//    sum(count * x^-factor) = 1, which is an upper bound written as 2^n (or c^n for bases above 2);
//  - a mix of Divide and Subtract, an unknown work term, or invalid factors: ok == false, order unknown.
Solution solveRecurrence(const Recurrence& r);

// "T(n) = 2T(n/2) + O(n)"
std::string describe(const Recurrence& r);

} // namespace nc::complexity
