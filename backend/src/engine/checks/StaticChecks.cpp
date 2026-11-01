#include "engine/checks/StaticChecks.hpp"

#include <algorithm>
#include <map>

#include "engine/ast/Traversal.hpp"
#include "engine/cfg/CfgBuilder.hpp"
#include "engine/checks/rules/ArithmeticRules.hpp"
#include "engine/checks/rules/ControlFlowRules.hpp"
#include "engine/checks/rules/MemoryRules.hpp"
#include "engine/checks/rules/Rule.hpp"
#include "engine/checks/rules/SuspiciousCodeRules.hpp"
#include "engine/complexity/LoopAnalyzer.hpp"

namespace nc::checks {

namespace {

RuleList defaultRules() {
    RuleList rules;
    addControlFlowRules(rules);
    addArithmeticRules(rules);
    addMemoryRules(rules);
    addSuspiciousCodeRules(rules);
    return rules;
}

int severityRank(const std::string& s) { return s == "high" ? 0 : s == "medium" ? 1 : 2; }

std::vector<Finding> run(const ast::Node& root, const std::vector<cfg::Cfg>* given, const CheckOptions& options) {
    const RuleList rules = defaultRules();
    std::vector<Finding> out;

    // Constants: `const int N = 100;` anywhere. Rules use them to tell a fixed bound from the input.
    complexity::AnalyzerOptions analyzer;
    std::set<std::string> constants;
    for (const ast::Node* v : ast::collect(root, "VAR_DECL"))
        if (v->dtype && v->dtype->rfind("const", 0) == 0 && !v->children.empty() && complexity::isConstantExpression(v->children.back()))
            constants.insert(v->spelling);
    analyzer.constants.assign(constants.begin(), constants.end());

    std::vector<cfg::Cfg> built;
    if (!given) built = cfg::buildCfgs(root);
    const std::vector<cfg::Cfg>& cfgs = given ? *given : built;

    for (const ast::Node* fn : ast::functionsOf(root)) {
        const cfg::Cfg* graph = nullptr;
        for (const cfg::Cfg& c : cfgs)
            if (c.function == fn->spelling && c.line == fn->line && c.unsupported.empty() && !c.blocks.empty()) { graph = &c; break; }
        const complexity::FunctionLoops loops = complexity::analyzeFunction(*fn, analyzer);
        const CheckContext ctx{*fn, graph, loops, constants, returnTypeOf(*fn)};
        for (const auto& rule : rules) {
            if (options.disabled.count(rule->id())) continue;
            rule->check(ctx, out);
        }
    }

    sortFindings(out);
    if (out.size() > options.max_findings) out.resize(options.max_findings);
    return out;
}

} // namespace

std::vector<std::string> ruleIds() {
    std::vector<std::string> ids;
    for (const auto& r : defaultRules()) ids.push_back(r->id());
    return ids;
}

std::vector<Finding> runChecks(const ast::Node& root, const CheckOptions& options) { return run(root, nullptr, options); }

std::vector<Finding> runChecks(const ast::Node& root, const std::vector<cfg::Cfg>& cfgs, const CheckOptions& options) {
    return run(root, &cfgs, options);
}

void sortFindings(std::vector<Finding>& f) {
    std::stable_sort(f.begin(), f.end(), [](const Finding& a, const Finding& b) {
        if (severityRank(a.severity) != severityRank(b.severity)) return severityRank(a.severity) < severityRank(b.severity);
        if (a.line != b.line) return a.line < b.line;
        return a.type < b.type;
    });
    f.erase(std::unique(f.begin(), f.end(), [](const Finding& a, const Finding& b) { return a.line == b.line && a.type == b.type && a.message == b.message; }), f.end());
}

} // namespace nc::checks
