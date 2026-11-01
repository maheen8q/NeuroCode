// Run from backend/ so Templates/ paths resolve.
#include "engine/optimize/Catalog.hpp"
#include "engine/optimize/SummaryBuilder.hpp"
#include <iostream>
#include <string>

using namespace nc::optimize;

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::cerr << "FAIL: " << (msg) << "\n"; ++failures; } \
    else { std::cout << "ok: " << (msg) << "\n"; } } while (0)

int main() {
    // 1. Catalog registers both built-in passes.
    Catalog cat;
    CHECK(cat.size() == 2, "catalog has 2 passes");

    // 2. With detection stubbed off, run() finds nothing (no false positives).
    PassContext ctx;
    ctx.sourceCode = "int main(){}";
    auto results = cat.run(ctx);
    CHECK(results.empty(), "no matches while detection is stubbed");

    // 3. SummaryBuilder loads a template and substitutes names.
    SummaryBuilder sb;
    std::string hash = sb.build("pair_sum_hash.cpp.tmpl", "nums", "target");
    CHECK(!hash.empty(), "pair_sum template loaded");
    CHECK(hash.find("unordered_map") != std::string::npos, "hash template body present");
    CHECK(hash.find("nums") != std::string::npos, "ARR substituted");
    CHECK(hash.find("target") != std::string::npos, "TARGET substituted");
    CHECK(hash.find("{{ARR}}") == std::string::npos, "no leftover ARR token");

    std::string bin = sb.build("linear_to_binary.cpp.tmpl", "arr", "key");
    CHECK(bin.find("binary_search") != std::string::npos, "binary template body present");

    // 4. Empty names fall back to defaults, never leave a blank identifier.
    std::string def = sb.build("pair_sum_hash.cpp.tmpl", "", "");
    CHECK(def.find("{{") == std::string::npos, "defaults fill empty names");

    // 5. Missing template returns empty, not a crash.
    CHECK(sb.build("nope.tmpl", "a", "k").empty(), "missing template returns empty");

    std::cout << (failures ? "\nFAILED\n" : "\nPASSED\n");
    return failures ? 1 : 0;
}
