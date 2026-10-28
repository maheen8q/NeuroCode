// nc_dump: dump what the engine knows about a C++ file.
//
//   nc_dump file.cpp [--out DIR]
//
// Writes DIR/ast.json (the AST, same shape as tools/extract_ast.py) and DIR/cfg.json (one control-flow
// graph per function plus cyclomatic complexity). DIR defaults to the current directory. A one-line
// summary of each function and the complexity verdict is printed on stdout.
//
// Parse diagnostics go to stderr with the user's own line numbers. Exit codes:
//   0 ok   1 the code has syntax errors (files are still written from the partial tree)
//   2 usage or I/O error   3 libclang could not create a translation unit
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "engine/ast/Serializer.hpp"
#include "engine/cfg/CfgBuilder.hpp"
#include "engine/cfg/CfgJson.hpp"
#include "engine/cfg/Reachability.hpp"
#include "engine/parse/AstBuilder.hpp"
#include "engine/parse/ClangFrontend.hpp"

namespace {
bool writeFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content << "\n";
    return static_cast<bool>(out);
}
}

int main(int argc, char** argv) {
    std::string path, out_dir = ".";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
        else path = a;
    }
    if (path.empty()) { std::cerr << "usage: nc_dump file.cpp [--out DIR]\n"; return 2; }

    std::ifstream in(path, std::ios::binary);
    if (!in) { std::cerr << "cannot read " << path << "\n"; return 2; }
    const std::string code((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const auto unit = nc::parse::parseUserCode(code);
    if (!unit) { std::cerr << "libclang could not create a translation unit\n"; return 3; }
    for (const auto& d : unit->diagnostics())
        if (d.in_user_code) std::cerr << "line " << d.line << ": " << d.message << "\n";

    const nc::ast::Node ast = nc::parse::buildAst(*unit);
    const auto cfgs = nc::cfg::buildCfgs(ast);

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    const auto ast_path = std::filesystem::path(out_dir) / "ast.json";
    const auto cfg_path = std::filesystem::path(out_dir) / "cfg.json";
    if (!writeFile(ast_path, nc::ast::toJsonString(ast)) ||
        !writeFile(cfg_path, nc::cfg::toJsonString(cfgs, nc::cfg::fileCyclomatic(ast)))) {
        std::cerr << "cannot write into " << out_dir << "\n";
        return 2;
    }
    std::cout << "wrote " << ast_path.string() << "\nwrote " << cfg_path.string() << "\n";

    for (const auto& c : cfgs) {
        if (!c.unsupported.empty()) { std::cout << c.function << ": CFG not built (" << c.unsupported << " is not supported)\n"; continue; }
        std::cout << c.function << ": " << c.blocks.size() << " blocks, " << c.edges.size()
                  << " edges, cyclomatic " << c.cyclomatic << "\n";
        for (const auto& f : nc::cfg::reachabilityFindings(c))
            std::cout << "  line " << f.line << " [" << f.type << "] " << f.message << "\n";
    }
    return unit->hasUserCodeErrors() ? 1 : 0;
}
