// DEPENDS ON T4: engine/cfg/CfgBuilder.hpp, Dominators.hpp, Reachability.hpp.
// Does not compile until those exist. Encodes the contract from
// docs/ast-cfg-schema.md so detection + complexity have a stable target.
#include "engine/cfg/CfgBuilder.hpp"
#include "engine/cfg/Dominators.hpp"
#include "engine/cfg/Reachability.hpp"
#include <iostream>
#include <string>

using namespace nc::cfg;

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::cerr << "FAIL: " << (msg) << "\n"; ++failures; } \
    else { std::cout << "ok: " << (msg) << "\n"; } } while (0)

// Helper assumed on CfgBuilder: build a CFG from source (already I/O-stripped).
static Cfg buildFrom(const std::string& src) {
    CfgBuilder b;
    return b.build(src);
}

int main() {
    // 1. Straight-line code: one entry, one exit, no branches.
    {
        Cfg g = buildFrom("int f(){ int x = 1; return x; }");
        CHECK(g.entry() != nullptr, "straight-line: has entry");
        CHECK(g.exit() != nullptr, "straight-line: has exit");
        CHECK(g.blockCount() >= 1, "straight-line: at least one block");
    }

    // 2. A single if adds a branch: entry dominates every block.
    {
        Cfg g = buildFrom("int f(int n){ if(n>0) return 1; return 0; }");
        Dominators dom(g);
        CHECK(dom.dominates(g.entry(), g.exit()), "if: entry dominates exit");
        CHECK(g.branchCount() == 1, "if: one branch");
    }

    // 3. A single loop back-edge is detected.
    {
        Cfg g = buildFrom("int f(int n){ int s=0; for(int i=0;i<n;++i) s+=i; return s; }");
        CHECK(g.loopDepth() == 1, "for: loop depth 1");
    }

    // 4. Nested loops report depth 2 (drives O(n^2) complexity).
    {
        Cfg g = buildFrom(
          "int f(int n){int c=0;for(int i=0;i<n;++i)for(int j=0;j<n;++j)++c;return c;}");
        CHECK(g.loopDepth() == 2, "nested for: loop depth 2");
    }

    // 5. Code after an unconditional return is unreachable.
    {
        Cfg g = buildFrom("int f(){ return 1; int dead = 2; return dead; }");
        Reachability rc(g);
        CHECK(rc.hasUnreachable(), "return: dead code is unreachable");
    }

    std::cout << (failures ? "\nFAILED\n" : "\nPASSED\n");
    return failures ? 1 : 0;
}
