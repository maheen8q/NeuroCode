#pragma once
#include <string>

namespace nc::parse {

// C++ port of strip_io() in tools/build_dataset.py, which cleaned the contest programs the model was
// trained on. The model only ever saw features of code stripped THIS way, so for contest-style input the
// feature path must strip the same way, quirks included. Port, not redesign:
//
//   1. lines starting with  #include  are dropped
//   2. every I/O statement is replaced by `;`:   cin/cout/cerr >> or <<  ...;   (with optional std::),
//      scanf/printf/puts/freopen (...) ...;   ios_base::/ios::sync_with_stdio(...);   cin./cout.tie(...);
//      getline(...) ...;      the match runs to the first `;` outside a string literal
//   3. `for (...) ;` and `while (...) ;` (a loop whose body is empty) become `;`, repeated until nothing
//      changes, which also removes the loops that only read input
//
// Quirks that come with the original and are kept on purpose (tests/fixtures/strip_golden.json pins them):
//   - the match runs to the next `;`, so `while (cin >> x) { s += x; }` swallows `s += x;` and the result
//     no longer parses. The training pipeline dropped such samples ("parse_errors_after_strip")
//   - `std::ios::sync_with_stdio(false);` leaves a stray `std::`, `cin.tie(0)->sync_with_stdio(0);`
//     and `fprintf(...)` are not matched, and `do { } while (x);` loses its `while (x);`
//   - training code got `trainingPreamble()` prepended; the parse preamble here (ClangFrontend) supplies
//     the headers instead. Type spellings do not depend on which headers are included
//
// Use it on the FEATURE path only (line numbers of the result are not the user's), and keep parsing the
// original code for the AST, CFG and findings shown in the UI. build_dataset.py applied it to external
// (contest) samples only, not to the hand-written seeds: use needsStrip() to decide.
struct StripResult {
    std::string code;                // the stripped text, WITHOUT the training preamble
    int includes_removed = 0;        // #include lines dropped
    int io_statements_removed = 0;   // I/O statements replaced by `;`
    int loops_removed = 0;           // empty-body loops replaced by `;` (counted over all passes)
};

StripResult stripIo(const std::string& source);

inline std::string stripIoCode(const std::string& source) { return stripIo(source).code; }

// True if stripping would change anything beyond includes: some I/O statement matches.
bool needsStrip(const std::string& source);

// The PREAMBLE build_dataset.py prepended to stripped code (headers + `using namespace std;`).
const char* trainingPreamble();

} // namespace nc::parse
