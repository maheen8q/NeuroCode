// Dominators and Reachability tests. Hand-built graphs need no libclang; the builder section does.
//   tests/build helper: g++ -std=c++20 -Isrc <parse+ast sources> src/engine/ast/Traversal.cpp src/engine/cfg/*.cpp tests/test_dominators.cpp -lclang-18
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "src/engine/ast/Traversal.hpp"
#include "src/engine/cfg/CfgBuilder.hpp"
#include "src/engine/cfg/Dominators.hpp"
#include "src/engine/cfg/Reachability.hpp"
#include "src/engine/parse/AstBuilder.hpp"
#include "src/engine/parse/ClangFrontend.hpp"

using namespace nc::cfg;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { ++g_fail; std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; } } while (0)

// n blocks (0 = entry, n-1 = exit), edges given as pairs.
static Cfg make(int n, std::vector<std::pair<int, int>> es, std::vector<int> return_blocks = {}) {
    Cfg c;
    c.entry = 0; c.exit = n - 1;
    for (int i = 0; i < n; ++i) {
        Block b; b.id = i; b.kind = i == 0 ? "entry" : i == n - 1 ? "exit" : "basic"; b.lines = {i == n - 1 ? 0 : i + 1};
        if (i == n - 1) b.lines.clear();
        c.blocks.push_back(b);
    }
    for (int r : return_blocks) c.blocks[static_cast<size_t>(r)].kind = "return";
    for (auto [a, b] : es) c.edges.push_back({a, b, "next"});
    return c;
}

static void testDiamond() {                      // 0 -> 1 -> {2,3} -> 4 -> 5(exit)
    const Cfg c = make(6, {{0, 1}, {1, 2}, {1, 3}, {2, 4}, {3, 4}, {4, 5}});
    const auto idom = immediateDominators(c);
    CHECK((idom == std::vector<int>{0, 0, 1, 1, 1, 4}));
    CHECK(dominates(idom, 1, 4) && dominates(idom, 0, 5) && dominates(idom, 4, 4));
    CHECK(!dominates(idom, 2, 4) && !dominates(idom, 4, 1));
    CHECK(naturalLoops(c).empty() && isReducible(c));
    const auto tree = dominatorTree(idom);
    CHECK((tree[1] == std::vector<int>{2, 3, 4}));
    CHECK(tree[5].empty() && tree[2].empty());
}

static void testSimpleLoop() {                   // 0 -> 1(cond) -> 2(body) -> 1 ; 1 -> 3(exit)
    const Cfg c = make(4, {{0, 1}, {1, 2}, {2, 1}, {1, 3}});
    const auto loops = naturalLoops(c);
    CHECK(loops.size() == 1);
    CHECK(loops[0].header == 1 && loops[0].latches == std::vector<int>{2});
    CHECK((loops[0].blocks == std::vector<int>{1, 2}));
    CHECK(loops[0].depth == 1 && loops[0].parent == -1);
    CHECK(!loopHasNoExit(c, loops[0]));
    CHECK(maxLoopDepth(loops) == 1 && isReducible(c));
}

static void testNestedAndShared() {              // nested loops; the inner header has two latches (a `continue`)
    // 0 -> 1 ; 1 -> 2 ; 2 -> 3 ; 3 -> 4 ; 4 -> 2 ; 3 -> 2 ; 2 -> 5 ; 5 -> 1 ; 1 -> 6(exit)
    const Cfg c = make(7, {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 2}, {3, 2}, {2, 5}, {5, 1}, {1, 6}});
    const auto loops = naturalLoops(c);
    CHECK(loops.size() == 2);
    CHECK(loops[0].header == 1 && loops[1].header == 2);
    CHECK((loops[1].latches == std::vector<int>{3, 4}));              // merged into one loop
    CHECK((loops[1].blocks == std::vector<int>{2, 3, 4}));
    CHECK((loops[0].blocks == std::vector<int>{1, 2, 3, 4, 5}));
    CHECK(loops[1].parent == 0 && loops[1].depth == 2 && loops[0].depth == 1);
    CHECK(maxLoopDepth(loops) == 2);
}

static void testInfiniteLoop() {                 // 0 -> 1 -> 2 -> 1 ; exit (3) unreachable
    const Cfg c = make(4, {{0, 1}, {1, 2}, {2, 1}});
    const auto loops = naturalLoops(c);
    CHECK(loops.size() == 1 && loopHasNoExit(c, loops[0]));
    CHECK(!exitReachable(c));
    CHECK((blocksThatCannotReachExit(c) == std::vector<int>{0, 1, 2}));
    CHECK(allPathsReturn(c));                       // nothing ever falls off the end
}

static void testUnreachable() {                  // block 2 has no incoming edge
    const Cfg c = make(5, {{0, 1}, {1, 4}, {2, 3}, {3, 4}}, {1});
    const auto idom = immediateDominators(c);
    CHECK(idom[2] == -1 && idom[3] == -1);
    CHECK(!dominates(idom, 2, 3) && !dominates(idom, 0, 3));
    CHECK((unreachableBlocks(c) == std::vector<int>{2, 3}));
    CHECK((unreachableLines(c) == std::vector<int>{3, 4}));
    CHECK(allPathsReturn(c));                       // the only reachable predecessor of exit is a return block
    CHECK(exitReachable(c));
    CHECK(blocksThatCannotReachExit(c).empty());
}

static void testFallsOff() {
    const Cfg c = make(3, {{0, 1}, {1, 2}});         // block 1 is basic, flows straight to exit
    CHECK(!allPathsReturn(c));
    const Cfg d = make(3, {{0, 1}, {1, 2}}, {1});
    CHECK(allPathsReturn(d));
}

static void testIrreducible() {                  // classic: 0 -> 1, 0 -> 2, 1 <-> 2, then exit
    const Cfg c = make(4, {{0, 1}, {0, 2}, {1, 2}, {2, 1}, {2, 3}});
    CHECK(!isReducible(c));
    CHECK(naturalLoops(c).empty());                 // no back edge: neither node dominates the other
}

static void testUnsupportedAndEmpty() {
    Cfg c; c.unsupported = "goto";
    CHECK(immediateDominators(c).empty() && naturalLoops(c).empty() && reachableBlocks(c).empty());
    CHECK(unreachableBlocks(c).empty() && !exitReachable(c) && !allPathsReturn(c) && isReducible(c));
    CHECK(!dominates({}, 0, 0));
    Cfg e;                                           // no blocks at all
    CHECK(immediateDominators(e).empty() && canReachExit(e).empty());
}

static void testSelfLoop() {                     // a block that loops to itself
    const Cfg c = make(3, {{0, 1}, {1, 1}, {1, 2}});
    const auto loops = naturalLoops(c);
    CHECK(loops.size() == 1 && loops[0].header == 1 && (loops[0].blocks == std::vector<int>{1}));
    CHECK(isReducible(c));
}

// ---- real builder output ----
static std::vector<Cfg> cfgsOf(const std::string& code) {
    const auto unit = nc::parse::parseUserCode(code);
    return unit ? buildCfgs(nc::parse::buildAst(*unit)) : std::vector<Cfg>{};
}

static void checkAgainstBuilder(const std::string& what, const std::string& code, int want_loops, int want_depth) {
    const auto cfgs = cfgsOf(code);
    CHECK(cfgs.size() == 1);
    if (cfgs.empty()) return;
    const Cfg& c = cfgs[0];
    const auto loops = naturalLoops(c);
    if (static_cast<int>(loops.size()) != want_loops || maxLoopDepth(loops) != want_depth)
        std::cerr << "  [" << what << "] loops=" << loops.size() << " depth=" << maxLoopDepth(loops) << "\n";
    CHECK(static_cast<int>(loops.size()) == want_loops);
    CHECK(maxLoopDepth(loops) == want_depth);
    CHECK(isReducible(c));
    // The structural back edges must be exactly the edges the builder labelled "back".
    int labelled = 0, structural = 0;
    const auto idom = immediateDominators(c);
    for (const Edge& e : c.edges) {
        if (e.kind == "back") ++labelled;
        if (dominates(idom, e.to, e.from)) ++structural;
    }
    CHECK(labelled == structural);
    // Every block that has lines and is reachable must be dominated by the entry.
    for (const Block& b : c.blocks) CHECK(idom[static_cast<size_t>(b.id)] == -1 || dominates(idom, c.entry, b.id));
}

static void testBuilder() {
    checkAgainstBuilder("pair_sum", "bool f(vector<int>& a,int k){for(int i=0;i<(int)a.size();i++){for(int j=i+1;j<(int)a.size();j++){if(a[i]+a[j]==k)return true;}}return false;}", 2, 2);
    checkAgainstBuilder("none", "int f(int x){ if (x>0) return 1; return 0; }", 0, 0);
    checkAgainstBuilder("while", "int f(int n){ int s=0; while(n>0){ s+=n; n--; } return s; }", 1, 1);
    checkAgainstBuilder("do_while", "int f(int n){ do { n--; } while(n>0); return n; }", 1, 1);
    checkAgainstBuilder("range_for", "int f(vector<int>& a){ int s=0; for(int x : a){ s+=x; } return s; }", 1, 1);
    checkAgainstBuilder("triple", "int f(int n){ int c=0; for(int i=0;i<n;i++) for(int j=0;j<n;j++) for(int k=0;k<n;k++) c++; return c; }", 3, 3);
    checkAgainstBuilder("sequential", "int f(int n){ int c=0; for(int i=0;i<n;i++) c++; for(int j=0;j<n;j++) c++; return c; }", 2, 1);
    checkAgainstBuilder("continue", "int f(int n){ int c=0; for(int i=0;i<n;i++){ if(i%2) continue; c++; } return c; }", 1, 1);
    checkAgainstBuilder("break", "int f(int n){ int c=0; for(int i=0;i<n;i++){ if(i>5) break; c++; } return c; }", 1, 1);

    // Infinite loop: no exit, and the code after it is dead.
    auto inf = cfgsOf("int f(int n){ while(true){ n++; } return n; }");
    CHECK(inf.size() == 1);
    if (!inf.empty()) {
        const auto loops = naturalLoops(inf[0]);
        CHECK(loops.size() == 1 && loopHasNoExit(inf[0], loops[0]));
        CHECK(!unreachableBlocks(inf[0]).empty());
        CHECK(!blocksThatCannotReachExit(inf[0]).empty());
    }
    // `while(true)` with a break terminates.
    auto brk = cfgsOf("int f(int n){ while(true){ if(n>3) break; n++; } return n; }");
    CHECK(brk.size() == 1 && !naturalLoops(brk[0]).empty() && !loopHasNoExit(brk[0], naturalLoops(brk[0])[0]));
    CHECK(brk.empty() || blocksThatCannotReachExit(brk[0]).empty());

    // Dead code after return, and a missing return.
    auto dead = cfgsOf("int f(int x){\n return x;\n x++;\n}\n");
    CHECK(dead.size() == 1 && !dead.empty() && !unreachableLines(dead[0]).empty());
    CHECK(dead.empty() || unreachableLines(dead[0]).front() == 3);
    auto missing = cfgsOf("int f(int x){ if(x>0) return 1; }");
    CHECK(missing.size() == 1 && !allPathsReturn(missing[0]) && missing[0].falls_off_end);
    auto ok = cfgsOf("int f(int x){ if(x>0) return 1; else return 2; }");
    CHECK(ok.size() == 1 && allPathsReturn(ok[0]) && !ok[0].falls_off_end);
    auto go = cfgsOf("int f(int x){ L: x++; if(x<3) goto L; return x; }");
    CHECK(go.size() == 1 && !go[0].unsupported.empty() && naturalLoops(go[0]).empty());

    // Agreement with the AST-level loop depth on a mix of structures.
    const std::string mix = "int f(vector<int>& a,int n){ int s=0; for(int i=0;i<n;i++){ for(int x: a){ s+=x; } while(s>100){ s/=2; } } do { s--; } while(s>0); return s; }";
    const auto unit = nc::parse::parseUserCode(mix);
    CHECK(unit != nullptr);
    if (unit) {
        const auto ast = nc::parse::buildAst(*unit);
        const auto cs = buildCfgs(ast);
        CHECK(cs.size() == 1 && !cs.empty() && maxLoopDepth(naturalLoops(cs[0])) == nc::ast::maxLoopDepth(ast));
        CHECK(cs.empty() || naturalLoops(cs[0]).size() == 4);
    }
}

int main() {
    testDiamond(); testSimpleLoop(); testNestedAndShared(); testInfiniteLoop(); testUnreachable();
    testFallsOff(); testIrreducible(); testUnsupportedAndEmpty(); testSelfLoop(); testBuilder();
    if (g_fail) { std::cerr << g_fail << " check(s) failed\n"; return 1; }
    std::cout << "all dominator and reachability tests passed\n";
    return 0;
}
