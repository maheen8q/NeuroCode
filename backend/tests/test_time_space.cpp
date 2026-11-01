// TimeComplexity and SpaceComplexity tests: real code through libclang, answers checked as strings.
#include <iostream>
#include <string>
#include <vector>

#include "engine/complexity/SpaceComplexity.hpp"
#include "engine/complexity/TimeComplexity.hpp"
#include "engine/parse/AstBuilder.hpp"
#include "engine/parse/ClangFrontend.hpp"

using namespace nc::complexity;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; } } while (0)
#define CHECK_EQ(a, b) do { const auto va = (a); const auto vb = (b); if (!(va == vb)) { ++g_fail; std::cerr << "FAIL line " << __LINE__ << ": " #a " = " << va << ", expected " << vb << "\n"; } } while (0)

static nc::ast::Node astOf(const std::string& code) {
    const auto unit = nc::parse::parseUserCode(code);
    return unit ? nc::parse::buildAst(*unit) : nc::ast::Node{};
}

static std::string T(const std::string& code, const std::string& entry = "") {
    TimeOptions o; o.entry = entry;
    return analyzeTime(astOf(code), o).time.toString();
}
static std::string S(const std::string& code, const std::string& entry = "") {
    SpaceOptions o; o.entry = entry;
    return analyzeSpace(astOf(code), o).space.toString();
}

static void testIndex() {
    const auto ast = astOf("int helper(int n){ return n; }\nint solve(int n){ return helper(n)+1; }\nint main(){ return solve(3); }");
    const ProgramIndex ix = buildIndex(ast);
    CHECK(ix.functions.size() == 3);
    CHECK(ix.roots == std::vector<std::string>{"main"});
    CHECK_EQ(chooseEntry(ix), std::string("main"));                       // the only root
    CHECK_EQ(chooseEntry(ix, "solve"), std::string("solve"));
    CHECK_EQ(chooseEntry(ix, "nope"), std::string("main"));               // a bad hint falls back
    const ProgramIndex ix2 = buildIndex(astOf("int helper(int n){ return n; }\nint solve(int n){ return helper(n); }\nint main(){ return 0; }"));
    CHECK_EQ(chooseEntry(ix2), std::string("solve"));                     // roots are solve and main: main is skipped
    CHECK_EQ(chooseEntry(buildIndex(astOf("int decl(int n);"))), std::string(""));
    CHECK_EQ(chooseEntry(buildIndex(astOf("int a(int n);int f(int n){return n<=0?0:g(n-1);} int g(int n){return f(n);}"))), std::string("g"));
}

static void testTimeLoops() {
    CHECK_EQ(T("int f(int n){ int s=0; for(int i=0;i<n;i++) s+=i; return s; }"), std::string("O(n)"));
    CHECK_EQ(T("bool f(vector<int>& a,int k){ for(int i=0;i<(int)a.size();i++) for(int j=i+1;j<(int)a.size();j++) if(a[i]+a[j]==k) return true; return false; }"),
             std::string("O(n^2)"));
    CHECK_EQ(T("int f(int n){ return n*2; }"), std::string("O(1)"));
    CHECK_EQ(T("bool f(vector<int>& a,int k){ unordered_set<int> seen; for(int x : a){ if(seen.count(k-x)) return true; seen.insert(x); } return false; }"),
             std::string("O(n)"));
    CHECK_EQ(T("bool f(vector<int>& a,int k){ set<int> seen; for(int x : a){ if(seen.count(k-x)) return true; seen.insert(x); } return false; }"),
             std::string("O(n log n)"));                                  // a tree set costs log n per operation
    CHECK_EQ(T("bool f(vector<int>& a,int k){ sort(a.begin(),a.end()); int l=0,r=(int)a.size()-1; while(l<r){ int s=a[l]+a[r]; if(s==k) return true; if(s<k) l++; else r--; } return false; }"),
             std::string("O(n log n)"));
    CHECK_EQ(T("int f(vector<int>& a,int t){ int lo=0,hi=(int)a.size()-1; while(lo<=hi){ int mid=lo+(hi-lo)/2; if(a[mid]==t) return mid; if(a[mid]<t) lo=mid+1; else hi=mid-1; } return -1; }"),
             std::string("O(log n)"));
    CHECK_EQ(T("int f(vector<int>& a,int t){ return lower_bound(a.begin(),a.end(),t)-a.begin(); }"), std::string("O(log n)"));
    CHECK_EQ(T("int f(vector<int>& a,int t){ for(int i=0;i<(int)a.size();i++) if(a[i]==t) return i; return -1; }"), std::string("O(n)"));
    CHECK_EQ(T("int f(vector<int>& a,int n){ int c=0; for(int i=0;i<n;i++) c+=count(a.begin(),a.end(),i); return c; }"), std::string("O(n^2)"));
    CHECK_EQ(T("int f(vector<int>& a){ return *max_element(a.begin(),a.end()); }"), std::string("O(n)"));
    CHECK_EQ(T("void f(vector<int>& a,int n){ for(int i=0;i<n;i++) a.insert(a.begin(),i); }"), std::string("O(n^2)"));    // vector insert shifts
    CHECK_EQ(T("void f(vector<int>& a,int n){ for(int i=0;i<n;i++) a.push_back(i); }"), std::string("O(n)"));
    CHECK_EQ(T("void f(map<int,int>& m,int n){ for(int i=0;i<n;i++) m[i]++; }"), std::string("O(n log n)"));
    CHECK_EQ(T("void f(unordered_map<int,int>& m,int n){ for(int i=0;i<n;i++) m[i]++; }"), std::string("O(n)"));
    CHECK_EQ(T("int f(int n){ priority_queue<int> q; for(int i=0;i<n;i++) q.push(i); int s=0; while(!q.empty()){ s+=q.top(); q.pop(); } return s; }"),
             std::string("O(n log n)"));
    CHECK_EQ(T("int f(string s){ int c=0; for(int i=0;i<(int)s.size();i++) for(int j=i;j<(int)s.size();j++) if(s.substr(i,j-i+1)==\"ab\") c++; return c; }"),
             std::string("O(n^3)"));                                      // substr and == are both linear
}

static void testTimeConstructors() {
    CHECK_EQ(T("int f(int n){ vector<int> v(n); return (int)v.size(); }"), std::string("O(n)"));
    CHECK_EQ(T("int f(int n){ vector<int> v(5); return (int)v.size(); }"), std::string("O(1)"));
    CHECK_EQ(T("int f(int n,int m){ vector<vector<int>> dp(n, vector<int>(m,0)); return dp[0][0]; }"), std::string("O(n^2)"));
    CHECK_EQ(T("int f(vector<int>& a){ vector<int> b = a; return (int)b.size(); }"), std::string("O(n)"));
    CHECK_EQ(T("int f(vector<int>& a){ unordered_set<int> s(a.begin(), a.end()); return (int)s.size(); }"), std::string("O(n)"));
    CHECK_EQ(T("int f(int n){ string s = \"abc\"; return (int)s.size(); }"), std::string("O(1)"));
    CHECK_EQ(T("long long f(int n){ vector<long long> dp(n+1); dp[0]=0; dp[1]=1; for(int i=2;i<=n;i++) dp[i]=dp[i-1]+dp[i-2]; return dp[n]; }"), std::string("O(n)"));
    CHECK_EQ(T("int f(int n){ vector<vector<int>> dp(n+1, vector<int>(n+1)); for(int i=1;i<=n;i++) for(int j=1;j<=n;j++) dp[i][j]=dp[i-1][j]+dp[i][j-1]; return dp[n][n]; }"),
             std::string("O(n^2)"));
}

static void testTimeCalls() {
    const char* helper = "int slow(int n){ int s=0; for(int i=0;i<n;i++) s+=i; return s; }\n";
    CHECK_EQ(T(std::string(helper) + "int f(int n){ return slow(n); }", "f"), std::string("O(n)"));
    CHECK_EQ(T(std::string(helper) + "int f(int n){ int s=0; for(int i=0;i<n;i++) s+=slow(i); return s; }", "f"), std::string("O(n^2)"));
    CHECK_EQ(T(std::string(helper) + "int f(int n){ return slow(n); }\nint g(int n){ return 1; }", "g"), std::string("O(1)"));
    CHECK_EQ(T(std::string(helper) + "int f(int n){ return slow(n); }"), std::string("O(n)"));                       // entry chosen: f is the root
    // mutual recursion: unknown, and it says so
    const auto a = analyzeTime(astOf("int g(int n); int f(int n){ if(n<=0) return 0; return g(n-1); } int g(int n){ if(n<=0) return 0; return f(n-1); }"));
    CHECK(a.time.unknown && !a.confident);
    // an unknown external function is assumed O(1) and reported
    const auto b = analyzeTime(astOf("int ext(int); int f(int n){ return ext(n); }"));
    CHECK(b.time.isConstant() && !b.confident && !b.assumptions.empty());
    // a known O(1) function leaves no assumption
    const auto c = analyzeTime(astOf("int f(int a,int b){ return max(a,b)+abs(a); }"));
    CHECK(c.confident && c.assumptions.empty());
}

static void testTimeRecursion() {
    CHECK_EQ(T("int fib(int n){ if(n<2) return n; return fib(n-1)+fib(n-2); }"), std::string("O(2^n)"));
    CHECK_EQ(T("long long f(int n){ if(n<=1) return 1; return n*f(n-1); }"), std::string("O(n)"));
    CHECK_EQ(T("void h(int n,int a,int b,int c){ if(n==0) return; h(n-1,a,c,b); h(n-1,c,b,a); }"), std::string("O(2^n)"));
    CHECK_EQ(T("int f(vector<int>& a,int t,int lo,int hi){ if(lo>hi) return -1; int mid=lo+(hi-lo)/2; if(a[mid]==t) return mid; if(a[mid]<t) return f(a,t,mid+1,hi); else return f(a,t,lo,mid-1); }"),
             std::string("O(log n)"));
    CHECK_EQ(T("void ms(vector<int>& a,vector<int>& t,int lo,int hi){ if(hi-lo<1) return; int mid=(lo+hi)/2; ms(a,t,lo,mid); ms(a,t,mid+1,hi); int i=lo,j=mid+1,k=lo; while(i<=mid&&j<=hi){ if(a[i]<=a[j]) t[k++]=a[i++]; else t[k++]=a[j++]; } while(i<=mid) t[k++]=a[i++]; while(j<=hi) t[k++]=a[j++]; for(int x=lo;x<=hi;x++) a[x]=t[x]; }"),
             std::string("O(n log n)"));
    CHECK_EQ(T("int g(int a,int b){ if(b==0) return a; return g(b,a%b); }"), std::string("O(log n)"));
    CHECK_EQ(T("int d(int n){ if(n==0) return 0; return 1+d(n/10); }"), std::string("O(log n)"));
    CHECK_EQ(T("int sum(vector<int>& a,int i){ if(i==(int)a.size()) return 0; return a[i]+sum(a,i+1); }"), std::string("O(n)"));
    // quicksort: worst case, with the average noted
    const auto qs = analyzeTime(astOf("int part(vector<int>& a,int lo,int hi){ int p=a[hi],i=lo; for(int j=lo;j<hi;j++) if(a[j]<p) swap(a[i++],a[j]); swap(a[i],a[hi]); return i; }\n"
                                       "void q(vector<int>& a,int lo,int hi){ if(lo>=hi) return; int p=part(a,lo,hi); q(a,lo,p-1); q(a,p+1,hi); }"), {"q"});
    CHECK_EQ(qs.time.toString(), std::string("O(n^2)"));
    bool avg = false;
    for (const auto& d : qs.derivation) avg = avg || d.find("O(n log n)") != std::string::npos;
    CHECK(avg && !qs.confident);
    // a cache guard: states, not a call tree
    CHECK_EQ(T("long long f(int n,vector<long long>& memo){ if(n<2) return n; if(memo[n]!=-1) return memo[n]; return memo[n]=f(n-1,memo)+f(n-2,memo); }"), std::string("O(n)"));
    // recursion inside a loop: honest unknown
    CHECK(analyzeTime(astOf("int dfs(int u,int n){ int s=0; for(int v=0;v<n;v++) s+=dfs(v,n-1); return s; }")).time.unknown);
    // the divisor is assumed to be 2 and the result says so
    const auto three = analyzeTime(astOf("int f(int n){ if(n<2) return 1; return f(n/3)+f(n/3)+f(n/3); }"));
    CHECK(!three.assumptions.empty() && !three.confident);
}

static void testSpace() {
    CHECK_EQ(S("int f(int n){ int s=0; for(int i=0;i<n;i++) s+=i; return s; }"), std::string("O(1)"));
    CHECK_EQ(S("bool f(vector<int>& a,int k){ for(int i=0;i<(int)a.size();i++) for(int j=i+1;j<(int)a.size();j++) if(a[i]+a[j]==k) return true; return false; }"), std::string("O(1)"));
    CHECK_EQ(S("bool f(vector<int>& a,int k){ unordered_set<int> seen; for(int x : a){ if(seen.count(k-x)) return true; seen.insert(x); } return false; }"), std::string("O(n)"));
    CHECK_EQ(S("int f(int n){ vector<int> v(n); return (int)v.size(); }"), std::string("O(n)"));
    CHECK_EQ(S("int f(int n){ vector<int> v; for(int i=0;i<n;i++) v.push_back(i); return (int)v.size(); }"), std::string("O(n)"));
    CHECK_EQ(S("int f(int n){ vector<int> v; v.push_back(1); return (int)v.size(); }"), std::string("O(1)"));
    CHECK_EQ(S("int f(int n){ vector<int> v; for(int i=0;i<n;i++) for(int j=0;j<n;j++) v.push_back(i); return (int)v.size(); }"), std::string("O(n^2)"));
    CHECK_EQ(S("int f(int n){ int c=0; for(int i=0;i<n;i++){ vector<int> row; for(int j=0;j<n;j++) row.push_back(j); c+=row.size(); } return c; }"), std::string("O(n)"));   // rebuilt per pass
    CHECK_EQ(S("int f(int n,int m){ vector<vector<int>> dp(n, vector<int>(m,0)); return dp[0][0]; }"), std::string("O(n^2)"));
    CHECK_EQ(S("int f(string s){ int cnt[26]={0}; for(char c : s) cnt[c-'a']++; return cnt[0]; }"), std::string("O(n)"));          // by-value string copy dominates
    CHECK_EQ(S("int f(const string& s){ int cnt[26]={0}; for(char c : s) cnt[c-'a']++; return cnt[0]; }"), std::string("O(1)"));
    CHECK_EQ(S("int f(int n){ int dp[100005]; dp[0]=1; return dp[0]; }"), std::string("O(n)"));
    CHECK_EQ(S("int f(int n){ int dp[1005][1005]; dp[0][0]=1; return dp[0][0]; }"), std::string("O(n^2)"));
    CHECK_EQ(S("int f(int n){ int a[10]; a[0]=1; return a[0]; }"), std::string("O(1)"));
    CHECK_EQ(S("int f(int n){ int a[256]; a[0]=1; return a[0]; }"), std::string("O(1)"));
    CHECK_EQ(S("int f(int n){ int a[257]; a[0]=1; return a[0]; }"), std::string("O(n)"));
    CHECK_EQ(S("int f(int n){ int a[26][1005]; a[0][0]=1; return a[0][0]; }"), std::string("O(n)"));
    CHECK_EQ(S("int f(int n){ int* p = new int[n]; p[0]=1; int r=p[0]; delete[] p; return r; }"), std::string("O(n)"));
    CHECK_EQ(S("int f(int n){ int* p = new int; *p=1; int r=*p; delete p; return r; }"), std::string("O(1)"));
    CHECK_EQ(S("int dp[100005];\nint f(int n){ return dp[0]; }"), std::string("O(n)"));                                                  // a global array
    CHECK_EQ(S("void f(vector<int>& a){ sort(a.begin(),a.end()); }"), std::string("O(1)"));
    CHECK_EQ(S("void f(vector<int>& a){ stable_sort(a.begin(),a.end()); }"), std::string("O(n)"));
    CHECK_EQ(S("int f(vector<int>& a,int n){ map<int,int> m; for(int i=0;i<n;i++) m[a[i]]++; return (int)m.size(); }"), std::string("O(n)"));
    CHECK_EQ(S("vector<long long> f(vector<int>& a){ vector<long long> pre(a.size()+1); for(size_t i=0;i<a.size();i++) pre[i+1]=pre[i]+a[i]; return pre; }"), std::string("O(n)"));
    // callees: the largest live callee counts, not the sum
    const char* helper = "int big(int n){ vector<int> v(n); return (int)v.size(); }\n";
    CHECK_EQ(S(std::string(helper) + "int f(int n){ return big(n)+big(n); }", "f"), std::string("O(n)"));
}

static void testSpaceRecursion() {
    CHECK_EQ(S("int fib(int n){ if(n<2) return n; return fib(n-1)+fib(n-2); }"), std::string("O(n)"));                  // depth n, not 2^n
    CHECK_EQ(S("long long f(int n){ if(n<=1) return 1; return n*f(n-1); }"), std::string("O(n)"));
    CHECK_EQ(S("int f(vector<int>& a,int t,int lo,int hi){ if(lo>hi) return -1; int mid=lo+(hi-lo)/2; if(a[mid]==t) return mid; if(a[mid]<t) return f(a,t,mid+1,hi); else return f(a,t,lo,mid-1); }"),
             std::string("O(log n)"));
    CHECK_EQ(S("void ms(vector<int>& a,int lo,int hi){ if(hi-lo<1) return; int mid=(lo+hi)/2; ms(a,lo,mid); ms(a,mid+1,hi); vector<int> t(hi-lo+1); for(int i=lo;i<=hi;i++) t[i-lo]=a[i]; for(int i=lo;i<=hi;i++) a[i]=t[i-lo]; }"),
             std::string("O(n)"));                                                                                          // halving frames of size n, n/2, ...
    CHECK_EQ(S("int g(int a,int b){ if(b==0) return a; return g(b,a%b); }"), std::string("O(log n)"));
    CHECK_EQ(S("int f(int n){ if(n==0) return 0; vector<int> v(n); return f(n-1)+(int)v.size(); }"), std::string("O(n^2)"));       // a copy per call
    CHECK_EQ(S("long long f(int n,vector<long long>& memo){ if(n<2) return n; if(memo[n]!=-1) return memo[n]; return memo[n]=f(n-1,memo)+f(n-2,memo); }"), std::string("O(n)"));
    const auto rec = analyzeSpace(astOf("int dfs(int u,int n){ int s=0; for(int v=0;v<n;v++) s+=dfs(v,n-1); return s; }"));
    CHECK_EQ(rec.space.toString(), std::string("O(n)"));
    CHECK(!rec.assumptions.empty() && !rec.confident);
}

static void testFacade() {
    const auto ast = astOf("bool twoSum(vector<int>& a,int k){ unordered_set<int> seen; for(int x : a){ if(seen.count(k-x)) return true; seen.insert(x); } return false; }");
    const nc::Complexity c = analyzeComplexity(ast);
    CHECK_EQ(c.time, std::string("O(n)"));
    CHECK_EQ(c.space, std::string("O(n)"));
    CHECK(c.derivation.size() >= 6);
    CHECK(c.derivation.front() == "Time:");
    bool has_space_heading = false, has_alloc = false;
    for (const auto& d : c.derivation) { has_space_heading = has_space_heading || d == "Space:"; has_alloc = has_alloc || d.find("unordered_set seen") != std::string::npos; }
    CHECK(has_space_heading && has_alloc);

    const nc::Complexity u = analyzeComplexity(astOf("int g(int n); int f(int n){ if(n<=0) return 0; return g(n-1); } int g(int n){ if(n<=0) return 0; return f(n-1); }"));
    CHECK_EQ(u.time, std::string("unknown"));
    bool assumption = false;
    for (const auto& d : u.derivation) assumption = assumption || d.rfind("Assumption:", 0) == 0;
    CHECK(assumption);

    const nc::Complexity none = analyzeComplexity(astOf("int x = 3;"));
    CHECK_EQ(none.time, std::string("unknown"));
    CHECK(!none.derivation.empty());
}

static void testDerivationReadable() {
    const auto t = analyzeTime(astOf("int f(vector<int>& a,int n){ int c=0; for(int i=0;i<n;i++) c+=count(a.begin(),a.end(),i); return c; }"));
    bool call_line = false, total = false;
    for (const auto& d : t.derivation) { call_line = call_line || d.find("count costs O(n)") != std::string::npos; total = total || d.find("Total running time of f: O(n^2)") != std::string::npos; }
    CHECK(call_line && total);
    const auto s = analyzeSpace(astOf("int f(int n){ int s=0; return s; }"));
    bool none = false;
    for (const auto& d : s.derivation) none = none || d.find("allocates nothing") != std::string::npos;
    CHECK(none);
}

int main() {
    testIndex(); testTimeLoops(); testTimeConstructors(); testTimeCalls(); testTimeRecursion();
    testSpace(); testSpaceRecursion(); testFacade(); testDerivationReadable();
    if (g_fail) { std::cerr << g_fail << " check(s) failed\n"; return 1; }
    std::cout << "all time and space tests passed\n";
    return 0;
}
