// Prints the code with its I/O boilerplate removed (parse/IoStripper.hpp), exactly as
// tools/build_dataset.py's strip_io() does (minus the preamble it prepends).
//   nc_strip file.cpp        stripped code on stdout, counts on stderr
//   nc_strip < file.cpp      same, from stdin
//   nc_strip --json          stdin: a JSON array of strings; stdout: a JSON array of the stripped strings
// Used by tests/parity_strip.py and tests/fuzz_strip.py to compare against build_dataset.py.
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

#include "engine/parse/IoStripper.hpp"

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--json") {
        const auto in = nlohmann::json::parse(std::cin);
        nlohmann::json out = nlohmann::json::array();
        for (const auto& s : in) out.push_back(nc::parse::stripIoCode(s.get<std::string>()));
        std::cout << out.dump() << "\n";
        return 0;
    }
    std::string code;
    if (argc > 1) {
        std::ifstream in(argv[1], std::ios::binary);
        if (!in) { std::cerr << "cannot read " << argv[1] << "\n"; return 2; }
        code.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    } else {
        code.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    }
    const auto r = nc::parse::stripIo(code);
    std::cout << r.code;
    std::cerr << "[includes removed " << r.includes_removed << ", I/O statements removed " << r.io_statements_removed
              << ", loops removed " << r.loops_removed << "]\n";
    return 0;
}
