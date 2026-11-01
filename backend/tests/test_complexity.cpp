// RecurrenceSolver and LoopAnalyzer tests. The solver tests need nothing; the analyzer tests parse code with libclang.
#include <iostream>
#include <string>
#include <vector>

#include "engine/complexity/LoopAnalyzer.hpp"
#include "engine/complexity/RecurrenceSolver.hpp"
#include "engine/parse/AstBuilder.hpp"
#include "engine/parse/ClangFrontend.hpp"

using namespace nc::complexity;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; } } while (0)
#define CHECK_EQ(a, b) do { const auto va = (a); const auto vb = (b); if (!(va == vb)) { ++g_fail; std::cerr << "FAIL line " << __LINE__ << ": " #a " = " << va << ", expected " << vb << "\n"; } } while (0)

static Recurrence rec(std::vector<SubProblem> calls, Order work) { return Recurrence{std::move(calls), work}; }
static SubProblem divide_(double b, int count = 1) { return {SubProblem::Kind::Divide, b, count}; }
static SubProblem subtract_(double c, int count = 1) { return {SubProblem::Kind::Subtract, c, count}; }
static std::string solve(const Recurrence& r) { return solveRecurrence(r).order.toString(); }

static void testOrder() {
    CHECK_EQ(Order::constant().toString(), std::string("O(1)"));
    CHECK_EQ(Order::logarithmic().toString(), std::string("O(log n)"));
    CHECK_EQ(Order::sqrtN().toString(), std::string("O(sqrt(n))"));
    CHECK_EQ(Order::linear().toString(), std::string("O(n)"));
    CHECK_EQ(Order::linearithmic().toString(), std::string("O(n log n)"));
    CHECK_EQ(Order::polynomial(2).toString(), std::string("O(n^2)"));
    CHECK_EQ(Order::polynomial(2.5).toString(), std::string("O(n^2.5)"));
    CHECK_EQ(Order::exponentialBase(2).toString(), std::string("O(2^n)"));
    CHECK_EQ(Order::exponentialBase(1.618).toString(), std::string("O(2^n)"));
    CHECK_EQ(Order::exponentialBase(3).toString(), std::string("O(3^n)"));
    CHECK_EQ(multiply(Order::linear(), Order::exponentialBase(2)).toString(), std::string("O(n * 2^n)"));
    CHECK_EQ(Order::unknownOrder().toString(), std::string("O(?)"));

    CHECK_EQ(multiply(Order::linear(), Order::linear()).toString(), std::string("O(n^2)"));
    CHECK_EQ(multiply(Order::linear(), Order::logarithmic()).toString(), std::string("O(n log n)"));
    CHECK_EQ(multiply(Order::linearithmic(), Order::logarithmic()).toString(), std::string("O(n log^2 n)"));
    CHECK(multiply(Order::linear(), Order::unknownOrder()).unknown);
    CHECK_EQ(dominant(Order::linear(), Order::linearithmic()).toString(), std::string("O(n log n)"));
    CHECK_EQ(dominant(Order::polynomial(2), Order::exponentialBase(2)).toString(), std::string("O(2^n)"));
    CHECK(dominant(Order::constant(), Order::unknownOrder()).unknown);
    CHECK(compare(Order::sqrtN(), Order::logarithmic()) > 0);
    CHECK(compare(Order::linear(), Order::linearithmic()) < 0);
    CHECK(compare(Order::linear(), Order::linear()) == 0);
}

static void testSolver() {
    const Order one = Order::constant(), n = Order::linear();
    CHECK_EQ(solve(rec({divide_(2, 2)}, n)), std::string("O(n log n)"));                // merge sort
    CHECK_EQ(solve(rec({divide_(2)}, one)), std::string("O(log n)"));                   // binary search
    CHECK_EQ(solve(rec({divide_(2)}, n)), std::string("O(n)"));
    CHECK_EQ(solve(rec({divide_(2, 2)}, one)), std::string("O(n)"));                    // case 1
    CHECK_EQ(solve(rec({divide_(2, 4)}, n)), std::string("O(n^2)"));
    CHECK_EQ(solve(rec({divide_(2, 3)}, n)), std::string("O(n^1.58)"));                 // Karatsuba, log2(3)
    CHECK_EQ(solve(rec({divide_(2, 2)}, Order::polynomial(2))), std::string("O(n^2)")); // case 3
    CHECK_EQ(solve(rec({divide_(3, 3)}, n)), std::string("O(n log n)"));                // not the same as b = 2
    CHECK_EQ(solve(rec({divide_(2, 2)}, Order::linearithmic())), std::string("O(n log^2 n)"));
    CHECK_EQ(solve(rec({divide_(2)}, Order::exponentialBase(2))), std::string("O(2^n)"));
    CHECK_EQ(solve(rec({divide_(2), divide_(3)}, n)), std::string("O(n)"));                 // Akra-Bazzi, p < 1
    CHECK_EQ(solve(rec({divide_(2), divide_(4)}, one)), std::string("O(n^0.69)"));         // 2^-p + 4^-p = 1 gives p = 0.694
}

static void testSolverSubtract() {
    const Order one = Order::constant(), n = Order::linear();
    CHECK_EQ(solve(rec({subtract_(1)}, one)), std::string("O(n)"));                       // factorial
    CHECK_EQ(solve(rec({subtract_(1)}, n)), std::string("O(n^2)"));
    CHECK_EQ(solve(rec({subtract_(1)}, Order::logarithmic())), std::string("O(n log n)"));
    CHECK_EQ(solve(rec({subtract_(1, 2)}, one)), std::string("O(2^n)"));                  // hanoi
    CHECK_EQ(solve(rec({subtract_(1), subtract_(2)}, one)), std::string("O(2^n)"));             // fibonacci, tight base 1.618
    CHECK_EQ(solve(rec({subtract_(1, 3)}, one)), std::string("O(3^n)"));
    CHECK_EQ(solve(rec({subtract_(1, 2)}, n)), std::string("O(2^n)"));
    CHECK_EQ(solve(rec({subtract_(1)}, Order::exponentialBase(2))), std::string("O(2^n)"));
    const Solution fib = solveRecurrence(rec({subtract_(1), subtract_(2)}, one));
    bool mentions_tight = false;
    for (const auto& s : fib.steps) mentions_tight = mentions_tight || s.find("1.618") != std::string::npos;
    CHECK(mentions_tight);
}

static void testSolverEdges() {
    CHECK(solveRecurrence(rec({}, Order::linear())).ok);
    CHECK_EQ(solve(rec({}, Order::linear())), std::string("O(n)"));
    CHECK(!solveRecurrence(rec({divide_(2), subtract_(1)}, Order::constant())).ok);          // mixed shrinking
    CHECK(!solveRecurrence(rec({divide_(1)}, Order::constant())).ok);                  // never shrinks
    CHECK(!solveRecurrence(rec({subtract_(0)}, Order::constant())).ok);
    CHECK(!solveRecurrence(rec({divide_(2, 0)}, Order::constant())).ok);
    CHECK(!solveRecurrence(rec({divide_(2)}, Order::unknownOrder())).ok);
    CHECK(solveRecurrence(rec({divide_(2)}, Order::unknownOrder())).order.unknown);
    const Solution s = solveRecurrence(rec({divide_(2, 2)}, Order::linear()));
    CHECK(s.ok && s.steps.size() >= 3 && s.steps[0] == "T(n) = 2T(n/2) + O(n)" && !s.method.empty());
    CHECK_EQ(describe(rec({subtract_(1), subtract_(2)}, Order::constant())), std::string("T(n) = T(n-1) + T(n-2) + O(1)"));
}

// ---- LoopAnalyzer on real code -----------------------------------------------------------------------------

static nc::ast::Node astOf(const std::string& code) {
    const auto unit = nc::parse::parseUserCode(code);
    return unit ? nc::parse::buildAst(*unit) : nc::ast::Node{};
}

static FunctionLoops analyze(const std::string& code, AnalyzerOptions opt = {}) {
    static std::vector<nc::ast::Node> keep;               // the result refers to no AST data, but keep it simple
    keep.push_back(astOf(code));
    auto all = analyzeAll(keep.back(), opt);
    CHECK(!all.empty());
    return all.empty() ? FunctionLoops{} : all.back();
}

static std::string work(const std::string& code) { return analyze(code).work.toString(); }

static void testLoops() {
    CHECK_EQ(work("int f(int n){ int s=0; for(int i=0;i<n;i++) s+=i; return s; }"), std::string("O(n)"));
    CHECK_EQ(work("int f(int n){ int s=0; for(int i=0;i<n;i++) for(int j=0;j<n;j++) s++; return s; }"), std::string("O(n^2)"));
    CHECK_EQ(work("int f(int n){ int s=0; for(int i=0;i<n;i++) for(int j=i+1;j<n;j++) for(int k=0;k<n;k++) s++; return s; }"), std::string("O(n^3)"));
    CHECK_EQ(work("int f(int n){ int s=0; for(int i=0;i<n;i++) s++; for(int j=0;j<n;j++) s++; return s; }"), std::string("O(n)"));
    CHECK_EQ(work("int f(int n){ int s=0; for(int i=1;i<n;i*=2) s++; return s; }"), std::string("O(log n)"));
    CHECK_EQ(work("int f(int n){ int s=0; for(int i=n;i>0;i/=2) s++; return s; }"), std::string("O(log n)"));
    CHECK_EQ(work("int f(int n){ int s=0; for(int i=1;i<n;i<<=1) s++; return s; }"), std::string("O(log n)"));
    CHECK_EQ(work("int f(int n){ int s=0; while(n>1){ n/=2; s++; } return s; }"), std::string("O(log n)"));
    CHECK_EQ(work("int f(int n){ int s=0; while(n>0){ s+=n%10; n/=10; } return s; }"), std::string("O(log n)"));
    CHECK_EQ(work("int f(int n){ int s=0; for(int i=1;i*i<=n;i++) s++; return s; }"), std::string("O(sqrt(n))"));
    CHECK_EQ(work("int f(int n){ int s=0; for(int i=0;i<n;i++) for(int j=1;j<n;j*=2) s++; return s; }"), std::string("O(n log n)"));
    CHECK_EQ(work("int f(int n){ int s=0; for(int j=0;j<26;j++) s++; return s; }"), std::string("O(1)"));
    CHECK_EQ(work("int f(int n){ int s=0; for(int i=0;i<n;i++) for(int j=0;j<26;j++) s++; return s; }"), std::string("O(n)"));
    CHECK_EQ(work("int f(int n){ const int N=100; int s=0; for(int i=0;i<N;i++) for(int j=0;j<N;j++) s++; return s; }"), std::string("O(1)"));
    CHECK_EQ(work("int f(vector<int>& a){ int s=0; for(int x : a) s+=x; return s; }"), std::string("O(n)"));
    CHECK_EQ(work("int f(vector<int>& a){ int s=0; for(int x : {1,2,3}) s+=x; return s; }"), std::string("O(1)"));
    CHECK_EQ(work("int f(vector<int>& a){ int s=0; for(int i=0;i<(int)a.size();i++) s+=a[i]; return s; }"), std::string("O(n)"));
    CHECK_EQ(work("int f(int n){ int s=0; do { s++; n--; } while(n>0); return s; }"), std::string("O(n)"));
    CHECK_EQ(work("int f(int n){ int s=0; while(n--) s++; return s; }"), std::string("O(n)"));
    CHECK_EQ(work("int f(int n){ int s=0; int i=0; while(i<n){ i+=2; s++; } return s; }"), std::string("O(n)"));
    CHECK_EQ(work("int f(int n){ return n+1; }"), std::string("O(1)"));
}

static void testBinarySearchAndPointers() {
    CHECK_EQ(work("int f(vector<int>& a,int t){ int lo=0,hi=(int)a.size()-1; while(lo<=hi){ int mid=lo+(hi-lo)/2; if(a[mid]==t) return mid; if(a[mid]<t) lo=mid+1; else hi=mid-1; } return -1; }"),
             std::string("O(log n)"));
    CHECK_EQ(work("int f(vector<int>& a,int t){ int lo=0,hi=(int)a.size(); while(lo<hi){ int mid=(lo+hi)>>1; if(a[mid]<t) lo=mid+1; else hi=mid; } return lo; }"),
             std::string("O(log n)"));
    CHECK_EQ(work("bool f(vector<int>& a,int k){ int l=0,r=(int)a.size()-1; while(l<r){ int s=a[l]+a[r]; if(s==k) return true; if(s<k) l++; else r--; } return false; }"),
             std::string("O(n)"));                                                   // two pointers
}

static void testInfoAndExits() {
    const auto f = analyze("int f(vector<int>& a,int n){\n int c=0;\n for(int i=0;i<n;i++){\n  for(int j=i+1;j<n;j++){\n   if(a[i]==a[j]) return 1;\n  }\n }\n return 0;\n}");
    CHECK(f.loops.size() == 2 && f.max_depth == 2);
    if (f.loops.size() == 2) {
        CHECK(f.loops[0].line == 3 && f.loops[0].depth == 1 && f.loops[0].parent == -1);
        CHECK(f.loops[1].line == 4 && f.loops[1].depth == 2 && f.loops[1].parent == 0);
        CHECK(f.loops[0].trip == TripKind::Linear && f.loops[0].variable == "i");
        CHECK(f.loops[1].early_exit && !f.loops[0].early_exit == false);              // return inside the inner nest
        CHECK_EQ(f.loops[0].cost.toString(), std::string("O(n^2)"));
        CHECK_EQ(f.loops[1].body_cost.toString(), std::string("O(1)"));
    }
    CHECK(!f.derivation.empty());
    bool has_line = false, has_nest = false;
    for (const auto& d : f.derivation) { has_line = has_line || d.find("Line 3") != std::string::npos; has_nest = has_nest || d.find("O(n^2)") != std::string::npos; }
    CHECK(has_line && has_nest);

    const auto inf = analyze("int f(int n){ while(true){ n++; } return n; }");
    CHECK(inf.loops.size() == 1 && inf.loops[0].trip == TripKind::Infinite && inf.work.unknown);
    const auto brk = analyze("int f(int n){ while(true){ if(n>3) break; n++; } return n; }");
    CHECK(brk.loops.size() == 1 && brk.loops[0].trip == TripKind::Unknown && brk.loops[0].early_exit);
    const auto unk = analyze("int f(int n){ bool done=false; int s=0; while(!done){ s++; if(s>n) done=true; } return s; }");
    CHECK(unk.loops.size() == 1 && unk.loops[0].trip == TripKind::Unknown && unk.work.unknown);
    const auto drain = analyze("int f(queue<int>& q){ int s=0; while(!q.empty()){ s+=q.front(); q.pop(); } return s; }");
    CHECK(drain.loops.size() == 1 && drain.loops[0].trip == TripKind::Linear && drain.loops[0].assumed);
    const auto both = analyze("int f(int n,int m){ int i=0,j=0; while(i<n && j<5){ i++; j++; } return i; }");
    CHECK(both.loops.size() == 1 && both.loops[0].trip == TripKind::Constant);       // && stops at the first failing side
    CHECK_EQ(analyze("int f(int n){ for(;;){ n++; if(n>9) return n; } }").work.toString(), std::string("O(?)"));
}

static void testCallCost() {
    AnalyzerOptions opt;
    opt.call_cost = [](const nc::ast::Node& c) -> std::optional<Order> {
        if (c.spelling == "sort") return Order::linearithmic();
        if (c.spelling == "slow") return Order::linear();
        return std::nullopt;
    };
    CHECK_EQ(analyze("void f(vector<int>& a){ sort(a.begin(), a.end()); }", opt).work.toString(), std::string("O(n log n)"));
    CHECK_EQ(analyze("void f(vector<int>& a,int n){ for(int i=0;i<n;i++) sort(a.begin(), a.end()); }", opt).work.toString(), std::string("O(n^2 log n)"));
    CHECK_EQ(analyze("void f(vector<int>& a,int n){ for(int i=0;i<n;i++) a[i]++; sort(a.begin(), a.end()); }", opt).work.toString(), std::string("O(n log n)"));
    CHECK_EQ(analyze("int slow(int n); int f(int n){ int s=0; for(int i=0;i<n;i++) s+=slow(i); return s; }", opt).work.toString(), std::string("O(n^2)"));
    CHECK_EQ(analyze("void f(vector<int>& a,int n){ for(int i=0;i<n;i++) a[i]++; }").work.toString(), std::string("O(n)"));   // no callback
}

static void testRecursion() {
    auto fib = analyze("int fib(int n){ if(n<2) return n; return fib(n-1)+fib(n-2); }");
    CHECK(fib.recursive_calls.size() == 2);
    for (const auto& r : fib.recursive_calls) CHECK(r.shrink == RecursiveCall::Shrink::Subtract && r.loop_depth == 0);
    CHECK(fib.work.isConstant() && !fib.maybe_memoized);

    auto fact = analyze("long long f(int n){ if(n<=1) return 1; return n*f(n-1); }");
    CHECK(fact.recursive_calls.size() == 1 && fact.recursive_calls[0].shrink == RecursiveCall::Shrink::Subtract);

    // Binary search: two call sites, one runs.
    auto bs = analyze("int f(vector<int>& a,int t,int lo,int hi){ if(lo>hi) return -1; int mid=lo+(hi-lo)/2; if(a[mid]==t) return mid; if(a[mid]<t) return f(a,t,mid+1,hi); else return f(a,t,lo,mid-1); }");
    CHECK(bs.recursive_calls.size() == 1 && bs.recursive_calls[0].shrink == RecursiveCall::Shrink::Divide);
    CHECK(bs.work.isConstant());

    // Merge sort: both calls run, the merge loop is the work.
    auto ms = analyze("void ms(vector<int>& a,vector<int>& t,int lo,int hi){ if(hi-lo<1) return; int mid=(lo+hi)/2; ms(a,t,lo,mid); ms(a,t,mid+1,hi); int i=lo,j=mid+1,k=lo; while(i<=mid&&j<=hi){ if(a[i]<=a[j]) t[k++]=a[i++]; else t[k++]=a[j++]; } while(i<=mid) t[k++]=a[i++]; while(j<=hi) t[k++]=a[j++]; for(int x=lo;x<=hi;x++) a[x]=t[x]; }");
    CHECK(ms.recursive_calls.size() == 2);
    for (const auto& r : ms.recursive_calls) CHECK(r.shrink == RecursiveCall::Shrink::Divide);
    CHECK_EQ(ms.work.toString(), std::string("O(n)"));
    CHECK(!ms.maybe_memoized);

    // End to end through the solver.
    Recurrence r;
    for (const auto& c : ms.recursive_calls) r.calls.push_back({SubProblem::Kind::Divide, c.factor, 1});
    r.work = ms.work;
    CHECK_EQ(solveRecurrence(r).order.toString(), std::string("O(n log n)"));

    auto gcd = analyze("int g(int a,int b){ if(b==0) return a; return g(b, a%b); }");
    CHECK(gcd.recursive_calls.size() == 1 && gcd.recursive_calls[0].shrink == RecursiveCall::Shrink::Divide);

    auto digits = analyze("int d(int n){ if(n==0) return 0; return 1+d(n/10); }");
    CHECK(digits.recursive_calls.size() == 1 && digits.recursive_calls[0].shrink == RecursiveCall::Shrink::Divide);

    auto in_loop = analyze("int dfs(int u,int n){ int s=0; for(int v=0;v<n;v++) s+=dfs(v,n-1); return s; }");
    CHECK(in_loop.recursive_calls.size() == 1 && in_loop.recursive_calls[0].loop_depth == 1);
    CHECK_EQ(in_loop.recursive_calls[0].loop_factor.toString(), std::string("O(n)"));

    auto unknown = analyze("int q(vector<int>& a,int lo,int hi){ if(lo>=hi) return 0; int p=part(a,lo,hi); return q(a,lo,p-1)+q(a,p+1,hi); }");
    CHECK(unknown.recursive_calls.size() == 2);
    for (const auto& c : unknown.recursive_calls) CHECK(c.shrink == RecursiveCall::Shrink::Subtract || c.shrink == RecursiveCall::Shrink::Unknown);

    auto memo = analyze("long long f(int n,vector<long long>& memo){ if(n<2) return n; if(memo[n]!=-1) return memo[n]; return memo[n]=f(n-1,memo)+f(n-2,memo); }");
    CHECK(memo.maybe_memoized && memo.recursive_calls.size() == 2);

    CHECK(analyze("int f(int n){ return n; }").recursive_calls.empty());
}

static void testStructure() {
    // Two functions, one calling the other: analyzeAll reports both, in source order.
    const auto ast = astOf("int helper(int n){ int s=0; for(int i=0;i<n;i++) s++; return s; }\nint main2(int n){ return helper(n); }");
    const auto all = analyzeAll(ast);
    CHECK(all.size() == 2 && all[0].function == "helper" && all[1].function == "main2");
    CHECK(all.size() == 2 && all[0].work.toString() == "O(n)" && all[1].work.isConstant());
    // Global constants resolve inside functions.
    const auto g = analyzeAll(astOf("const int N = 50;\nint f(int n){ int s=0; for(int i=0;i<N;i++) s++; return s; }"));
    CHECK(g.size() == 1 && g[0].work.isConstant());
    // A declaration without a body yields an empty analysis.
    CHECK(analyzeAll(astOf("int decl(int n);")).empty());
}

int main() {
    testOrder(); testSolver(); testSolverSubtract(); testSolverEdges();
    testLoops(); testBinarySearchAndPointers(); testInfoAndExits(); testCallCost(); testRecursion(); testStructure();
    if (g_fail) { std::cerr << g_fail << " check(s) failed\n"; return 1; }
    std::cout << "all complexity tests passed\n";
    return 0;
}
