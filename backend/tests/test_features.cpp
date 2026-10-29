// FeatureExtractor tests. Build from backend/ (see the command in the delivery notes), run from backend/.
// The golden vectors are what T1's tools/features.py produced for every file in tests/fixtures/ast_samples
// (regenerate with: python3 tests/parity_ast.py ... --write-golden tests/fixtures/features_golden.json).
// This test needs no Python: it checks the C++ port against those stored numbers.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/ast/Serializer.hpp"
#include "engine/parse/AstBuilder.hpp"
#include "engine/parse/ClangFrontend.hpp"
#include "ml/FeatureExtractor.hpp"

static int g_fail = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) { ++g_fail; std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; } \
    } while (0)

static std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static nc::ast::Node astOf(const std::string& code) {
    const auto unit = nc::parse::parseUserCode(code);
    return unit ? nc::parse::buildAst(*unit) : nc::ast::Node{};
}

static std::vector<float> featuresOf(const std::string& code) { return nc::computeFeatures(astOf(code)); }

static float at(const std::vector<float>& v, const char* name) {
    const auto& names = nc::featureNames();
    for (size_t i = 0; i < names.size(); ++i) if (std::string(names[i]) == name) return v[i];
    ++g_fail; std::cerr << "unknown feature " << name << "\n";
    return -1.f;
}

int main() {
    // --- 1. shape
    CHECK(nc::kFeatureVectorLength == 53);
    CHECK(nc::featureNames().size() == 53);
    CHECK(std::string(nc::featureNames()[20]) == "cyclomatic");
    CHECK(std::string(nc::featureNames()[52]) == "inner_while_no_reset");
    CHECK(featuresOf("int f(){return 0;}").size() == 53);
    CHECK(nc::computeFeatures(nc::ast::Node{}).size() == 53);          // empty tree: all zeros, no crash

    // --- 2. golden vectors from features.py
    const auto golden = nlohmann::json::parse(readFile("tests/fixtures/features_golden.json"));
    const auto names = golden.at("names").get<std::vector<std::string>>();
    CHECK(names.size() == 53);
    for (size_t i = 0; i < names.size() && i < 53; ++i) CHECK(names[i] == nc::featureNames()[i]);
    int compared = 0;
    for (const auto& [file, vec] : golden.at("samples").items()) {
        const auto expect = vec.get<std::vector<float>>();
        const auto got = featuresOf(readFile("tests/fixtures/ast_samples/" + file));
        CHECK(got.size() == expect.size());
        bool same = got.size() == expect.size();
        for (size_t i = 0; same && i < got.size(); ++i)
            if (got[i] != expect[i]) {
                same = false;
                std::cerr << "  " << file << ": " << names[i] << " expected " << expect[i] << " got " << got[i] << "\n";
            }
        CHECK(same);
        ++compared;
    }
    CHECK(compared >= 20);
    std::cout << "  " << compared << " golden vectors compared\n";

    // --- 3. spot checks on the quirks the model depends on
    {
        const auto v = featuresOf("int f(vector<int>& v,int n){ int s=0; for(int i=0;i<n;i++) for(int j=i+1;j<n;j++) s+=v[i]*v[j]; return s; }");
        CHECK(at(v, "loop_count") == 2 && at(v, "max_nest_depth") == 2 && at(v, "loop_from_i_plus_1") >= 1);
        CHECK(at(v, "array_access_count") == 2);                      // operator[] counts
        CHECK(at(v, "two_index_same_array") == 1);                    // v[i] and v[j]
        CHECK(at(v, "cyclomatic") == 3);                              // 2 loops + 1
        CHECK(at(v, "vector_count") == 1);                            // the parameter only
    }
    {
        const auto v = featuresOf("int f(int n){ return n<2 ? n : f(n-1)+f(n-2); }");
        CHECK(at(v, "has_recursion") == 1 && at(v, "recursive_fn_count") == 1 && at(v, "self_call_count") == 2);
        CHECK(at(v, "ternary_count") == 1 && at(v, "cyclomatic") == 2);
    }

    // --- 4. JSON entry point equals the tree entry point
    {
        const std::string code = readFile("tests/fixtures/ast_samples/two_pointer_sorted.cpp");
        const auto tree = astOf(code);
        CHECK(nc::computeFeaturesFromJson(nc::ast::toJsonString(tree)) == nc::computeFeatures(tree));
        bool threw = false;
        try { nc::computeFeaturesFromJson("not json"); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
    }

    std::cout << (g_fail ? "FEATURE TESTS FAILED: " + std::to_string(g_fail) : std::string("all feature tests passed")) << "\n";
    return g_fail ? 1 : 0;
}
