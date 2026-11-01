// IoStripper tests. No libclang needed. Run from backend/ (reads tests/fixtures/strip_golden.json):
//   g++ -std=c++20 -Isrc src/engine/parse/IoStripper.cpp tests/test_io_stripper.cpp -o test_io_stripper
// strip_golden.json holds what tools/build_dataset.py's strip_io() produced for each case
// (regenerate with tests/gen_strip_golden.py); the C++ output must match it character for character.
// tests/fuzz_strip.py does the same comparison on thousands of random inputs.
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

#include "engine/parse/IoStripper.hpp"

using nc::parse::stripIo;

static int g_fail = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) { ++g_fail; std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; } \
    } while (0)

int main() {
    std::ifstream in("tests/fixtures/strip_golden.json");
    CHECK(static_cast<bool>(in));
    const auto golden = nlohmann::json::parse(in);

    // --- 1. the training preamble is the one build_dataset.py used
    CHECK(std::string(nc::parse::trainingPreamble()) == golden.at("preamble").get<std::string>());

    // --- 2. every golden case, character for character
    int cases = 0;
    for (const auto& c : golden.at("cases")) {
        const std::string name = c.at("name").get<std::string>();
        const std::string got = stripIo(c.at("input").get<std::string>()).code;
        const std::string want = c.at("expected").get<std::string>();
        if (got != want) {
            ++g_fail;
            std::cerr << "FAIL " << name << "\n  want: " << nlohmann::json(want).dump() << "\n  got:  " << nlohmann::json(got).dump() << "\n";
        }
        ++cases;
    }
    CHECK(cases >= 20);
    std::cout << "  " << cases << " golden cases compared\n";

    // --- 3. counts and needsStrip
    {
        const auto r = stripIo("#include <iostream>\n#include <vector>\nint main(){ int n; cin >> n; for (int i = 0; i < n; i++) cin >> a[i]; cout << n; }");
        CHECK(r.includes_removed == 2 && r.io_statements_removed == 3 && r.loops_removed == 1);
        CHECK(nc::parse::needsStrip("int main(){ cout << 1; }"));
        CHECK(nc::parse::needsStrip("int main(){ printf(\"x\"); }"));
        CHECK(!nc::parse::needsStrip("#include <vector>\nint f(vector<int>& a){ return a.size(); }"));
        CHECK(!nc::parse::needsStrip("int f(){ mycout << 1; return 0; }"));
        const auto none = stripIo("int f(vector<int>& a){ int s = 0; for (int x : a) s += x; return s; }");
        CHECK(none.includes_removed == 0 && none.io_statements_removed == 0 && none.loops_removed == 0);
    }

    // --- 4. odd input terminates and does not crash (long runs, unbalanced everything)
    {
        (void)stripIo("");
        (void)stripIo("cout <<");
        (void)stripIo("for (((((((");
        (void)stripIo("\"unterminated string; cout << 1;");
        (void)stripIo(std::string(200000, '('));
        std::string big = "int main(){";
        for (int i = 0; i < 20000; ++i) big += "cout << \"line;\" << " + std::to_string(i) + ";";
        const auto r = stripIo(big + "}");
        CHECK(r.io_statements_removed == 20000);
        (void)stripIo("cout << " + std::string(300000, 'x') + ";");              // one very long statement
    }

    std::cout << (g_fail ? "IO STRIPPER TESTS FAILED: " + std::to_string(g_fail) : std::string("all IoStripper tests passed")) << "\n";
    return g_fail ? 1 : 0;
}
