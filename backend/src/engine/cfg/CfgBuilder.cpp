#include "engine/cfg/CfgBuilder.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "engine/ast/Traversal.hpp"

namespace nc::cfg {

using ast::Node;

std::vector<std::vector<int>> Cfg::successors() const {
    std::vector<std::vector<int>> s(blocks.size());
    for (const auto& e : edges) s[static_cast<size_t>(e.from)].push_back(e.to);
    return s;
}

std::vector<std::vector<int>> Cfg::predecessors() const {
    std::vector<std::vector<int>> p(blocks.size());
    for (const auto& e : edges) p[static_cast<size_t>(e.to)].push_back(e.from);
    return p;
}

nlohmann::json cfgsToJson(const std::vector<Cfg>& cfgs) {
    using json = nlohmann::json;
    json fns = json::array();
    for (const auto& c : cfgs) {
        if (!c.unsupported.empty()) {
            fns.push_back({{"name", c.function}, {"line", c.line}, {"unsupported", c.unsupported}});
            continue;
        }
        json blocks = json::array(), edges = json::array();
        for (const auto& b : c.blocks) blocks.push_back({{"id", b.id}, {"kind", b.kind}, {"lines", b.lines}});
        for (const auto& e : c.edges) edges.push_back({{"from", e.from}, {"to", e.to}, {"kind", e.kind}});
        fns.push_back({{"name", c.function}, {"line", c.line}, {"entry", c.entry}, {"exit", c.exit},
                       {"blocks", blocks}, {"edges", edges},
                       {"falls_off_end", c.falls_off_end}, {"cyclomatic", c.cyclomatic}});
    }
    return json{{"schema_version", 1}, {"functions", fns}};
}

std::string cfgsToJsonString(const std::vector<Cfg>& cfgs) { return cfgsToJson(cfgs).dump(); }

namespace {

struct Unsupported : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// A frontier is the set of (block, edge kind) pairs that must be connected to whatever block comes
// next. An empty frontier means "control cannot get here".
using Exit = std::pair<int, std::string>;
using Frontier = std::vector<Exit>;

Frontier concat(Frontier a, const Frontier& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

bool isCompareOp(const std::string& op) {
    return op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=" || op == "&&" || op == "||";
}

bool isDecl(const Node& n) { return n.type == "VAR_DECL" || n.type == "DECL_STMT"; }

// Splits the header of a loop out of its `tokens` text ("for ( int i = 0 ; i < n ; i ++ ) ..."):
// the pieces between the top-level semicolons of the first parenthesis group, spaces removed.
// Empty result when the header is cut off by the 40 token limit.
std::vector<std::string> headerSegments(const std::string& tokens) {
    std::istringstream in(tokens);
    std::string tok;
    int depth = 0;
    bool started = false;
    std::vector<std::string> seg(1);
    while (in >> tok) {
        if (tok == "(") {
            ++depth;
            if (!started) { started = true; continue; }
        } else if (tok == ")") {
            --depth;
            if (started && depth == 0) return seg;
        } else if (started && depth == 1 && tok == ";") {
            seg.emplace_back();
            continue;
        }
        if (started) seg.back() += tok;
    }
    return {};
}

struct ForParts {
    const Node* init = nullptr;
    const Node* cond = nullptr;
    const Node* inc = nullptr;
    bool cond_is_true = false;       // for (;;) or for (...; true; ...)
};

// libclang drops the parts of a `for` header that are missing, so the children are [init?, cond?, inc?, body].
// The loop's tokens say which parts exist; a heuristic on the node kinds is the fallback.
ForParts splitFor(const Node& n) {
    ForParts p;
    const size_t parts = n.children.empty() ? 0 : n.children.size() - 1;     // everything before the body
    const auto seg = headerSegments(n.tokens.value_or(""));
    const Node** target[3] = {&p.init, &p.cond, &p.inc};

    if (seg.size() == 3) {
        std::vector<int> present;
        for (int k = 0; k < 3; ++k) if (!seg[static_cast<size_t>(k)].empty()) present.push_back(k);
        if (present.size() == parts) {
            for (size_t i = 0; i < parts; ++i) *target[present[i]] = &n.children[i];
            p.cond_is_true = seg[1].empty() || seg[1] == "true" || seg[1] == "1";
            return p;
        }
    }
    for (size_t i = 0; i < parts; ++i) {                                         // fallback
        const Node& c = n.children[i];
        const std::string op = c.op.value_or("");
        if (c.type == "DECL_STMT") p.init = &c;
        else if (c.type == "BINARY_OPERATOR" && isCompareOp(op)) p.cond = &c;
        else if (!p.init && !p.cond && c.type == "BINARY_OPERATOR" && op == "=") p.init = &c;
        else p.inc = &c;
    }
    p.cond_is_true = !p.cond;
    return p;
}

bool whileIsInfinite(const Node& n) {
    const auto seg = headerSegments(n.tokens.value_or(""));
    return seg.size() == 1 && (seg[0] == "true" || seg[0] == "1");
}

class Builder {
public:
    Cfg build(const Node& fn) {
        cfg_.function = fn.spelling;
        cfg_.line = fn.line;
        cfg_.cyclomatic = ast::cyclomatic(fn);
        const Node* body = ast::bodyOf(fn);
        if (!body) return std::move(cfg_);
        try {
            cfg_.entry = block("entry", fn.line);
            Frontier end = stmt(*body, {{cfg_.entry, "next"}}, false);
            cfg_.exit = block("exit", -1);
            cfg_.falls_off_end = !end.empty();
            link(end, cfg_.exit);
            for (int r : returns_) edge(r, cfg_.exit, "next");
        } catch (const Unsupported& u) {
            Cfg bad;
            bad.function = fn.spelling;
            bad.line = fn.line;
            bad.cyclomatic = cfg_.cyclomatic;
            bad.unsupported = u.what();
            return bad;
        }
        std::stable_sort(cfg_.edges.begin(), cfg_.edges.end(), [](const Edge& a, const Edge& b) {
            return a.from != b.from ? a.from < b.from : a.to < b.to;
        });
        return std::move(cfg_);
    }

private:
    Cfg cfg_;
    std::vector<int> returns_;
    std::vector<Frontier> breaks_, continues_;

    int block(const char* kind, int line) {
        Block b;
        b.id = static_cast<int>(cfg_.blocks.size());
        b.kind = kind;
        if (line >= 0) b.lines.push_back(line);
        cfg_.blocks.push_back(b);
        return b.id;
    }
    void addLine(int id, int line) {
        auto& ls = cfg_.blocks[static_cast<size_t>(id)].lines;
        if (std::find(ls.begin(), ls.end(), line) == ls.end()) ls.push_back(line);
    }
    void edge(int from, int to, const std::string& kind) { cfg_.edges.push_back({from, to, kind}); }
    void link(const Frontier& f, int to) { for (const auto& [from, kind] : f) edge(from, to, kind); }

    // Straight-line statements share one block. Leaves the frontier at the block holding the statement.
    // With an empty frontier (code after a return) the block simply has no way in: Reachability finds it.
    int basicFor(Frontier& f, int line, bool forceNew) {
        if (!forceNew && f.size() == 1 && f[0].second == "next" &&
            cfg_.blocks[static_cast<size_t>(f[0].first)].kind == "basic") {
            addLine(f[0].first, line);
            return f[0].first;
        }
        const int id = block("basic", line);
        link(f, id);
        f = {{id, "next"}};
        return id;
    }

    Frontier stmts(const std::vector<Node>& v, Frontier f, bool forceNewFirst) {
        bool first = forceNewFirst;
        for (const auto& n : v) {
            f = stmt(n, std::move(f), first);
            first = false;
        }
        return f;
    }

    Frontier stmt(const Node& n, Frontier f, bool forceNew) {
        const std::string& t = n.type;
        if (t == "COMPOUND_STMT") return stmts(n.children, std::move(f), forceNew);
        if (t == "NULL_STMT") return f;
        if (t == "IF_STMT") return ifStmt(n, std::move(f));
        if (t == "FOR_STMT") return forStmt(n, std::move(f), forceNew);
        if (t == "WHILE_STMT" || t == "CXX_FOR_RANGE_STMT") return whileLike(n, std::move(f));
        if (t == "DO_STMT") return doWhile(n, std::move(f));
        if (t == "SWITCH_STMT") return switchStmt(n, std::move(f));
        if (t == "RETURN_STMT" || t == "CXX_THROW_EXPR") {
            const int id = block("return", n.line);
            link(f, id);
            returns_.push_back(id);
            return {};
        }
        if (t == "BREAK_STMT") {
            if (!breaks_.empty()) breaks_.back() = concat(breaks_.back(), f);
            return {};
        }
        if (t == "CONTINUE_STMT") {
            if (!continues_.empty()) continues_.back() = concat(continues_.back(), f);
            return {};
        }
        if (t == "GOTO_STMT" || t == "INDIRECT_GOTO_STMT") throw Unsupported("goto");
        if (t == "CXX_TRY_STMT" || t == "SEH_TRY_STMT") throw Unsupported("try/catch");
        if (t == "LABEL_STMT") {
            if (n.children.empty()) return f;
            return stmt(n.children.back(), std::move(f), forceNew);
        }
        if (t == "CONDITIONAL_OPERATOR" && n.children.size() == 3) return ternary(n, std::move(f));
        // declarations and expression statements (calls, assignments, ++i, lambdas, ...)
        basicFor(f, n.line, forceNew);
        return f;
    }

    Frontier ternary(const Node& n, const Frontier& f) {
        const int br = block("branch", n.line);
        link(f, br);
        const int a = block("basic", n.children[1].line > 0 ? n.children[1].line : n.line);
        const int b = block("basic", n.children[2].line > 0 ? n.children[2].line : n.line);
        edge(br, a, "true");
        edge(br, b, "false");
        return {{a, "next"}, {b, "next"}};
    }

    Frontier ifStmt(const Node& n, Frontier f) {
        // children: [init-or-condition-variable ...] condition, then [, else]
        const auto& k = n.children;
        size_t skip = 0;
        while (k.size() - skip > 3 || (k.size() - skip == 3 && isDecl(k[skip]))) ++skip;
        for (size_t i = 0; i < skip; ++i) basicFor(f, k[i].line > 0 ? k[i].line : n.line, false);
        if (k.size() - skip < 2) {                                // not a shape we know: treat as one statement
            basicFor(f, n.line, false);
            return f;
        }
        const int br = block("branch", n.line);
        link(f, br);
        const Frontier then_out = stmt(k[skip + 1], {{br, "true"}}, false);
        const Frontier else_out = k.size() - skip == 3 ? stmt(k[skip + 2], {{br, "false"}}, false)
                                                       : Frontier{{br, "false"}};
        return concat(then_out, else_out);
    }

    Frontier switchStmt(const Node& n, Frontier f) {
        const int sw = block("branch", n.line);
        link(f, sw);
        breaks_.emplace_back();
        Frontier fall;                                            // fall-through from the previous label
        bool has_default = false;

        const Node& body = n.children.back();
        const std::vector<Node> single = {};
        const std::vector<Node>& items = body.type == "COMPOUND_STMT" ? body.children : single;
        auto one = [&](const Node& s0) {
            const Node* s = &s0;
            Frontier in = fall;
            bool labelled = false;
            while (s->type == "CASE_STMT" || s->type == "DEFAULT_STMT") {       // `case 1: case 2: x;` nests
                const bool is_default = s->type == "DEFAULT_STMT";
                has_default = has_default || is_default;
                in.emplace_back(sw, is_default ? "default" : "case");
                labelled = true;
                if (s->children.empty()) { s = nullptr; break; }
                s = &s->children.back();
            }
            if (labelled) fall = s ? stmt(*s, in, true) : in;
            else fall = stmt(*s, fall, false);                                  // an ordinary statement in the body
        };
        if (body.type == "COMPOUND_STMT") for (const auto& s : items) one(s);
        else one(body);

        Frontier out = concat(fall, breaks_.back());
        breaks_.pop_back();
        if (!has_default) out.emplace_back(sw, "default");
        return out;
    }

    Frontier forStmt(const Node& n, Frontier f, bool forceNew) {
        const ForParts parts = splitFor(n);
        if (parts.init) basicFor(f, n.line, forceNew);
        const int cond = block("loop_cond", n.line);
        link(f, cond);

        breaks_.emplace_back();
        continues_.emplace_back();
        const Frontier body = n.children.empty() ? Frontier{{cond, "true"}}
                                                 : stmt(n.children.back(), {{cond, "true"}}, false);
        const Frontier brk = breaks_.back();
        const Frontier cnt = continues_.back();
        breaks_.pop_back();
        continues_.pop_back();

        const Frontier to_inc = concat(body, cnt);
        if (!to_inc.empty()) {
            const int inc = block("loop_inc", n.line);
            link(to_inc, inc);
            edge(inc, cond, "back");
        }
        Frontier out = brk;
        if (!parts.cond_is_true) out.emplace_back(cond, "false");
        return out;
    }

    Frontier whileLike(const Node& n, Frontier f) {                // while, range-for
        const bool infinite = n.type == "WHILE_STMT" && whileIsInfinite(n);
        const int cond = block("loop_cond", n.line);
        link(f, cond);

        breaks_.emplace_back();
        continues_.emplace_back();
        const Frontier body = n.children.empty() ? Frontier{{cond, "true"}}
                                                 : stmt(n.children.back(), {{cond, "true"}}, false);
        const Frontier brk = breaks_.back();
        const Frontier cnt = continues_.back();
        breaks_.pop_back();
        continues_.pop_back();

        for (const auto& [from, kind] : concat(body, cnt)) { (void)kind; edge(from, cond, "back"); }
        Frontier out = brk;
        if (!infinite) out.emplace_back(cond, "false");
        return out;
    }

    Frontier doWhile(const Node& n, const Frontier& f) {
        const size_t before = cfg_.blocks.size();
        breaks_.emplace_back();
        continues_.emplace_back();
        const Frontier body = n.children.empty() ? f : stmt(n.children.front(), f, true);
        const Frontier brk = breaks_.back();
        const Frontier cnt = continues_.back();
        breaks_.pop_back();
        continues_.pop_back();

        const int cond = block("loop_cond", n.line);
        link(concat(body, cnt), cond);
        const int first = cfg_.blocks.size() > before + 1 ? static_cast<int>(before) : cond;
        edge(cond, first, "back");

        Frontier out = brk;
        out.emplace_back(cond, "false");
        return out;
    }
};

} // namespace

Cfg buildCfg(const Node& function) { return Builder().build(function); }

std::vector<Cfg> buildCfgs(const Node& root) {
    std::vector<Cfg> out;
    for (const Node* fn : ast::functionsOf(root)) out.push_back(buildCfg(*fn));
    return out;
}

} // namespace nc::cfg
