// Static checks: every rule must find its bug and stay silent on correct code. Parses real code with libclang.
#include <iostream>
#include <string>
#include <vector>

#include "engine/checks/StaticChecks.hpp"
#include "engine/cfg/CfgBuilder.hpp"
#include "engine/parse/AstBuilder.hpp"
#include "engine/parse/ClangFrontend.hpp"

using namespace nc;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; } } while (0)

static std::vector<Finding> run(const std::string& code, const checks::CheckOptions& o = {}) {
    const auto unit = parse::parseUserCode(code);
    if (!unit) { std::cerr << "parse failed: " << code << "\n"; ++g_fail; return {}; }
    return checks::runChecks(parse::buildAst(*unit), o);
}

static int count(const std::vector<Finding>& f, const std::string& type) {
    int n = 0;
    for (const Finding& x : f) n += x.type == type;
    return n;
}

static std::string dump(const std::vector<Finding>& f) {
    std::string s;
    for (const Finding& x : f) s += "    line " + std::to_string(x.line) + " " + x.severity + " " + x.type + ": " + x.message + "\n";
    return s;
}

// The code has exactly `want` findings of `type`.
#define EXPECT(code, type, want) do { const auto f = run(code); if (count(f, type) != (want)) { ++g_fail; std::cerr << "FAIL line " << __LINE__ << ": expected " << (want) << " '" << type << "' in:\n  " << code << "\n" << dump(f); } } while (0)
// Correct code: no findings at all.
#define CLEAN(code) do { const auto f = run(code); if (!f.empty()) { ++g_fail; std::cerr << "FAIL line " << __LINE__ << ": correct code got findings:\n  " << code << "\n" << dump(f); } } while (0)

static void testControlFlow() {
    EXPECT("int f(int x){\n return x;\n x++;\n}", "unreachable", 1);
    EXPECT("int f(int x){ while(true){ x++; if(x>3) break; } return x; }", "unreachable", 0);
    EXPECT("int f(int n){ for(int i=0;i<n;i++){ if(i>2) continue; n--; } return n; }", "unreachable", 0);
    EXPECT("int f(int x){ if(x>0) return 1; }", "missing_return", 1);
    EXPECT("int f(int x){ if(x>0) return 1; else return 2; }", "missing_return", 0);
    EXPECT("void f(int x){ x++; }", "missing_return", 0);
    EXPECT("int main(){ int x=1; x++; }", "missing_return", 0);                       // main may omit it
    EXPECT("int f(int x){ if(x>0) return 1; abort(); }", "missing_return", 0);
    EXPECT("int f(int n){ while(true){ n++; } }", "infinite_loop", 1);
    EXPECT("int f(int n){ int i=0,s=0; while(i<n){ s++; } return s; }", "infinite_loop", 1);          // i never changes
    EXPECT("int f(int n){ int i=0,s=0; while(i<n){ s++; i++; } return s; }", "infinite_loop", 0);
    EXPECT("int f(int n){ while(n>1){ n/=2; } return n; }", "infinite_loop", 0);
    EXPECT("int f(int n){ for(;;){ if(n>5) return n; n++; } }", "infinite_loop", 0);
    EXPECT("int f(int n){ int i=0; while(i<n){ g(i); i++; } return i; }", "infinite_loop", 0);
    EXPECT("int f(vector<int>& a){ int i=0; while(i<(int)a.size()){ i++; } return i; }", "infinite_loop", 0);
    EXPECT("int f(int n){ return f(n); }", "infinite_recursion", 1);
    EXPECT("int f(int n){ if(n<0) return 0; return f(n); }", "infinite_recursion", 1);
    EXPECT("int f(int n){ if(n<=0) return 0; return f(n-1); }", "infinite_recursion", 0);
    EXPECT("int fib(int n){ if(n<2) return n; return fib(n-1)+fib(n-2); }", "infinite_recursion", 0);
}

static void testArithmetic() {
    EXPECT("int f(vector<int>& a,int lo,int hi){ int mid=(lo+hi)/2; return a[mid]; }", "overflow_risk", 1);
    EXPECT("int f(vector<int>& a,int lo,int hi){ int mid=lo+(hi-lo)/2; return a[mid]; }", "overflow_risk", 0);
    EXPECT("long long f(vector<long long>& a,long long lo,long long hi){ long long mid=(lo+hi)/2; return a[mid]; }", "overflow_risk", 0);   // 64 bit: fine
    EXPECT("long long f(int a,int b){ long long x = a*b; return x; }", "overflow_risk", 1);
    EXPECT("long long f(int a,int b){ return a*b; }", "overflow_risk", 1);
    EXPECT("long long f(int a,int b){ long long x = (long long)a*b; return x; }", "overflow_risk", 0);
    EXPECT("long long f(int a,int b){ long long x = 1LL*a*b; return x; }", "overflow_risk", 0);
    EXPECT("int f(int n){ int s=1; for(int i=1;i<=n;i++) s*=i; return s; }", "overflow_risk", 1);
    EXPECT("int f(vector<int>& a){ int s=0; for(int x : a) s+=x; return s; }", "overflow_risk", 1);                    // low hint
    EXPECT("long long f(vector<int>& a){ long long s=0; for(int x : a) s+=x; return s; }", "overflow_risk", 0);
    EXPECT("int f(int n){ int c=0; for(int i=0;i<n;i++) c+=1; return c; }", "overflow_risk", 0);                       // adding a constant
    EXPECT("int f(vector<int>& a){ int s=0; for(int x : a) s+=x; return s; }", "division_by_zero", 0);
    EXPECT("int f(vector<int>& a){ int s=0; for(int x : a) s+=x; return s / a.size(); }", "division_by_zero", 1);
    EXPECT("int f(vector<int>& a){ if(a.empty()) return 0; int s=0; for(int x : a) s+=x; return s / a.size(); }", "division_by_zero", 0);
    EXPECT("int f(vector<int>& a){ int s=0; for(int x : a) s+=x; return s % (a.size()-1); }", "division_by_zero", 1);
    EXPECT("int f(vector<int>& a){ if(a.size()<2) return 0; return a[0] / (a.size()-1); }", "division_by_zero", 0);
}

static void testMemory() {
    EXPECT("int f(vector<int>& a){ int s=0; for(int i=0;i<=a.size();i++) s+=a[i]; return s; }", "out_of_bounds", 1);
    EXPECT("int f(vector<int>& a){ int s=0; for(int i=0;i<a.size();i++) s+=a[i]; return s; }", "out_of_bounds", 0);
    EXPECT("int f(vector<int>& a){ return a[a.size()]; }", "out_of_bounds", 1);
    EXPECT("int f(vector<int>& a){ return a[a.size()-1]; }", "out_of_bounds", 0);
    EXPECT("int f(vector<int>& a){ return a[-1]; }", "out_of_bounds", 1);
    EXPECT("int f(int n){ vector<int> v(n); v[n]=1; return v[0]; }", "out_of_bounds", 1);
    EXPECT("int f(int n){ vector<int> v(n+1); v[n]=1; return v[0]; }", "out_of_bounds", 0);
    EXPECT("int f(int n){ vector<int> v(n); for(int i=0;i<=n;i++) v[i]=i; return v[0]; }", "out_of_bounds", 1);
    EXPECT("int f(int n){ vector<int> v(n); for(int i=0;i<n;i++) v[i]=i; return v[0]; }", "out_of_bounds", 0);
    EXPECT("int f(int n){ int x; return x+1; }", "uninitialized_variable", 1);
    EXPECT("int f(int n){ int x; x=5; return x+1; }", "uninitialized_variable", 0);
    EXPECT("int f(int n){ int x=0; return x+1; }", "uninitialized_variable", 0);
    EXPECT("int f(int n){ int x; if(n>0) x=1; else x=2; return x; }", "uninitialized_variable", 0);
    EXPECT("int f(int n){ int x; scanf(\"%d\",&x); return x; }", "uninitialized_variable", 0);
    EXPECT("int f(int n){ int last; int s=0; for(int i=0;i<n;i++){ if(i>0) s+=last; last=i; } return s; }", "uninitialized_variable", 0);
    EXPECT("int f(int n){ int x; int y = sizeof(x); return y; }", "uninitialized_variable", 0);
    EXPECT("int f(int n){ int* p = new int[5]; p[0]=1; return p[0]; }", "memory_leak", 1);
    EXPECT("int f(int n){ int* p = new int[5]; p[0]=1; int r=p[0]; delete[] p; return r; }", "memory_leak", 0);
    EXPECT("int* f(int n){ int* p = new int[5]; p[0]=1; return p; }", "memory_leak", 0);
}

static void testSuspicious() {
    EXPECT("int f(int x){ if(x=5) return 1; return 0; }", "assignment_in_condition", 1);
    EXPECT("int f(int x){ if((x=5)) return 1; return 0; }", "assignment_in_condition", 0);
    EXPECT("int f(int x){ if(x==5) return 1; return 0; }", "assignment_in_condition", 0);
    EXPECT("int f(int x){ while(x=next(x)) x++; return x; }", "assignment_in_condition", 1);
    EXPECT("int f(int n){ int s=0; for(int i=0;i<n;i++); return s; }", "empty_loop_body", 1);
    EXPECT("int f(int n){ int s=0; for(int i=0;i<n;i++){} return s; }", "empty_loop_body", 0);
    EXPECT("int f(int n){ int s=0; for(int i=0;i<n;i++) s++; return s; }", "empty_loop_body", 0);
    EXPECT("int f(int n){ int unused=3; return n; }", "unused_variable", 1);
    EXPECT("int f(int n){ int used=3; return n+used; }", "unused_variable", 0);
    EXPECT("int f(int n){ vector<int> v; return n; }", "unused_variable", 1);
    EXPECT("int f(int n){ int _scratch=3; return n; }", "unused_variable", 0);
}

static void testCorrectCodeIsQuiet() {
    CLEAN("bool pairSum(vector<int>& a,int k){ for(int i=0;i<(int)a.size();i++) for(int j=i+1;j<(int)a.size();j++) if(a[i]+a[j]==k) return true; return false; }");
    CLEAN("int bs(vector<int>& a,int t){ int lo=0,hi=(int)a.size()-1; while(lo<=hi){ int mid=lo+(hi-lo)/2; if(a[mid]==t) return mid; if(a[mid]<t) lo=mid+1; else hi=mid-1; } return -1; }");
    CLEAN("bool twoSum(vector<int>& a,int k){ unordered_set<long long> seen; for(int x : a){ if(seen.count((long long)k-x)) return true; seen.insert(x); } return false; }");
    CLEAN("bool twoPtr(vector<int>& a,int k){ int l=0,r=(int)a.size()-1; while(l<r){ long long s=(long long)a[l]+a[r]; if(s==k) return true; if(s<k) l++; else r--; } return false; }");
    CLEAN("int fib(int n){ if(n<2) return n; return fib(n-1)+fib(n-2); }");
    CLEAN("vector<long long> pref(vector<int>& a){ vector<long long> p(a.size()+1); for(size_t i=0;i<a.size();i++) p[i+1]=p[i]+a[i]; return p; }");
    CLEAN("void ms(vector<int>& a,vector<int>& t,int lo,int hi){ if(hi-lo<1) return; int mid=lo+(hi-lo)/2; ms(a,t,lo,mid); ms(a,t,mid+1,hi); int i=lo,j=mid+1,k=lo; while(i<=mid&&j<=hi){ if(a[i]<=a[j]) t[k++]=a[i++]; else t[k++]=a[j++]; } while(i<=mid) t[k++]=a[i++]; while(j<=hi) t[k++]=a[j++]; for(int x=lo;x<=hi;x++) a[x]=t[x]; }");
    CLEAN("int gcd2(int a,int b){ while(b){ int t=a%b; a=b; b=t; } return a; }");
    CLEAN("int digits(int n){ int c=0; while(n>0){ c++; n/=10; } return c; }");
    CLEAN("long long fact(int n){ if(n<=1) return 1; return n*fact(n-1); }");
    CLEAN("int maxOf(vector<int>& a){ int m=a[0]; for(size_t i=1;i<a.size();i++) if(a[i]>m) m=a[i]; return m; }");
    CLEAN("double avg(vector<int>& a){ if(a.empty()) return 0; long long s=0; for(int x : a) s+=x; return (double)s/a.size(); }");
    CLEAN("int cnt(string& s){ int c[26]={0}; for(char ch : s) c[ch-'a']++; int m=0; for(int i=0;i<26;i++) if(c[i]>m) m=c[i]; return m; }");
    CLEAN("long long f(int n){ int* p = new int[n]; long long r=0; for(int i=0;i<n;i++){ p[i]=i; r+=p[i]; } delete[] p; return r; }");
    CLEAN("int f(int n){ int arr[10]; for(int i=0;i<(int)(sizeof(arr)/sizeof(arr[0]));i++) arr[i]=i; return arr[0]+n; }");
}

static void testFacade() {
    // ids, order, severity sorting, dedupe, options
    const auto ids = checks::ruleIds();
    CHECK(ids.size() == 12);
    CHECK(std::find(ids.begin(), ids.end(), "unreachable") != ids.end() && std::find(ids.begin(), ids.end(), "unused_variable") != ids.end());

    const std::string mixed = "int f(int x,vector<int>& a){\n int unused=1;\n if(x=2) x++;\n return a[a.size()];\n}\nint g(int x){ if(x>0) return 1; }";
    const auto f = run(mixed);
    CHECK(f.size() >= 4);
    for (size_t i = 1; i < f.size(); ++i) {
        const auto rank = [](const std::string& s) { return s == "high" ? 0 : s == "medium" ? 1 : 2; };
        CHECK(rank(f[i - 1].severity) <= rank(f[i].severity));
    }
    CHECK(f.front().severity == "high");
    for (const Finding& x : f) CHECK(x.line > 0 && !x.message.empty() && !x.type.empty());

    checks::CheckOptions off;
    off.disabled = {"unused_variable", "assignment_in_condition"};
    const auto g = run(mixed, off);
    CHECK(count(g, "unused_variable") == 0 && count(g, "assignment_in_condition") == 0 && count(g, "out_of_bounds") == 1);

    checks::CheckOptions cap;
    cap.max_findings = 2;
    const auto h = run(mixed, cap);
    CHECK(h.size() == 2 && h[0].severity == "high");                       // the most severe survive the cut

    // given graphs are used and match by function
    const auto unit = parse::parseUserCode(mixed);
    const ast::Node ast = parse::buildAst(*unit);
    const auto cfgs = cfg::buildCfgs(ast);
    const auto k = checks::runChecks(ast, cfgs);
    CHECK(k.size() == f.size());
    CHECK(checks::runChecks(ast, std::vector<cfg::Cfg>{}).size() < f.size());    // no graphs: control flow rules are skipped

    // goto: no graph, the other rules still run
    const auto go = run("int f(int x){ int unused=2; L: x++; if(x<3) goto L; return x; }");
    CHECK(count(go, "unused_variable") == 1);
    CHECK(run("int decl(int x);").empty());
    CHECK(run("int x = 3;").empty());
}

int main() {
    testControlFlow(); testArithmetic(); testMemory(); testSuspicious(); testCorrectCodeIsQuiet(); testFacade();
    if (g_fail) { std::cerr << g_fail << " check(s) failed\n"; return 1; }
    std::cout << "all static check tests passed\n";
    return 0;
}
