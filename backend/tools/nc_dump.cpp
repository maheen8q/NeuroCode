// nc_dump: write the engine's analysis artifacts for a C++ file.
//   nc_dump file.cpp [--out DIR]     writes DIR/ast.json and DIR/cfg.json (DIR defaults to the current directory)
//
// ast.json is what tools/extract_ast.py would write (raw clang cursor kinds). cfg.json is
// {"schema_version":1,"functions":[...]} from cfg/CfgBuilder.hpp. Parse diagnostics go to stderr with the
// user's own line numbers. Exit codes: 0 ok, 1 the code has syntax errors (files are still written from the
// partial tree), 2 usage / I/O error, 3 libclang failure.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "engine/ast/Serializer.hpp"
#include "engine/cfg/CfgBuilder.hpp"
#include "engine/parse/AstBuilder.hpp"
#include "engine/parse/ClangFrontend.hpp"

static bool writeFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text << "\n";
    return static_cast<bool>(out);
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

    const auto ast = nc::parse::buildAst(*unit);
    const auto cfgs = nc::cfg::buildCfgs(ast);

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    const auto ast_path = std::filesystem::path(out_dir) / "ast.json";
    const auto cfg_path = std::filesystem::path(out_dir) / "cfg.json";
    if (!writeFile(ast_path, nc::ast::toJsonString(ast)) || !writeFile(cfg_path, nc::cfg::cfgsToJsonString(cfgs))) {
        std::cerr << "cannot write into " << out_dir << "\n";
        return 2;
    }
    std::cout << "wrote " << ast_path.string() << " and " << cfg_path.string() << " (" << cfgs.size() << " function(s))\n";
    for (const auto& c : cfgs)
        if (!c.unsupported.empty()) std::cerr << "cfg: " << c.function << " skipped (" << c.unsupported << ")\n";
    return unit->hasUserCodeErrors() ? 1 : 0;
}
