#include "engine/checks/rules/Rule.hpp"

#include <algorithm>
#include <cctype>

#include "engine/ast/Traversal.hpp"

namespace nc::checks {

using ast::Node;
using complexity::referencedNames;

Finding makeFinding(int line, const char* severity, const char* type, std::string message) {
    Finding f;
    f.line = line;
    f.severity = severity;
    f.type = type;
    f.message = std::move(message);
    return f;
}

const Node& skipImplicit(const Node& n) {
    const Node* p = &n;
    while (p->type == "UNEXPOSED_EXPR" && p->children.size() == 1) p = &p->children[0];
    return *p;
}

const Node& core(const Node& n) { return complexity::unwrapExpr(n); }

std::string compact(const std::string& s) {
    std::string out;
    for (char c : s) if (!std::isspace(static_cast<unsigned char>(c))) out += c;
    return out;
}

std::string returnTypeOf(const Node& fn) {
    if (!fn.dtype) return {};
    const std::string& t = *fn.dtype;
    int depth = 0;
    for (size_t i = 0; i < t.size(); ++i) {                    // the first '(' outside <...> starts the parameter list
        if (t[i] == '<') ++depth;
        else if (t[i] == '>') --depth;
        else if (t[i] == '(' && depth == 0) {
            std::string r = t.substr(0, i);
            while (!r.empty() && std::isspace(static_cast<unsigned char>(r.back()))) r.pop_back();
            return r;
        }
    }
    return {};
}

bool isScalarType(const std::string& t) {
    static const std::set<std::string> scalars = {
        "int", "long", "long long", "short", "char", "bool", "float", "double", "long double", "unsigned", "unsigned int", "unsigned long",
        "unsigned long long", "unsigned short", "unsigned char", "signed char", "size_t", "int64_t", "int32_t", "uint64_t", "uint32_t",
        "ptrdiff_t", "long int", "long long int", "short int", "long unsigned int"};
    if (scalars.count(t)) return true;
    return !t.empty() && t.back() == '*' && t.find('(') == std::string::npos;
}

bool isNarrowIntType(const std::string& t) {
    static const std::set<std::string> narrow = {"int", "unsigned", "unsigned int", "short", "unsigned short", "char", "unsigned char",
                                                  "signed char", "int32_t", "uint32_t", "short int"};
    return narrow.count(t) > 0;
}

bool isWideIntType(const std::string& t) {
    static const std::set<std::string> wide = {"long", "long long", "unsigned long", "unsigned long long", "int64_t", "uint64_t", "size_t",
                                                "long int", "long long int", "long unsigned int", "ptrdiff_t"};
    return wide.count(t) > 0;
}

std::string exprText(const Node& raw) {
    const Node& n = core(raw);
    const std::string& op = n.op ? *n.op : std::string();
    if (n.type == "DECL_REF_EXPR") return n.spelling;
    if (n.type == "INTEGER_LITERAL") return "c";
    if ((n.type == "ARRAY_SUBSCRIPT_EXPR" || (n.type == "CALL_EXPR" && n.spelling == "operator[]")) && n.base_text && n.index_text)
        return compact(*n.base_text) + "[" + compact(*n.index_text) + "]";
    if (n.type == "BINARY_OPERATOR" && n.children.size() == 2 && !op.empty()) return exprText(n.children[0]) + " " + op + " " + exprText(n.children[1]);
    return "...";
}

std::string receiverName(const Node& call) {
    if (call.children.empty()) return {};
    const Node* n = &call.children[0];
    while (n) {
        const Node& u = core(*n);
        if (u.type == "DECL_REF_EXPR") return u.spelling;
        n = u.children.empty() ? nullptr : &u.children[0];
    }
    return {};
}

const Node* conditionOf(const Node& s) {
    if (s.type == "WHILE_STMT" && s.children.size() >= 2) return &s.children[s.children.size() - 2];
    if (s.type == "DO_STMT" && s.children.size() >= 2) return &s.children[1];
    if (s.type == "IF_STMT") {
        const size_t tail = s.has_else.value_or(false) ? 3 : 2;       // [init?, cond, then, else?]
        if (s.children.size() >= tail) return &s.children[s.children.size() - tail];
    }
    return nullptr;
}

const Node* bodyOfLoop(const Node& loop) { return loop.children.empty() ? nullptr : &loop.children.back(); }

bool mayModify(const Node& n, const std::set<std::string>& names) {
    const std::string& op = n.op ? *n.op : std::string();
    auto isName = [&](const Node& x) {
        const Node& c = core(x);
        return c.type == "DECL_REF_EXPR" && names.count(c.spelling) > 0;
    };
    if (n.type == "BINARY_OPERATOR" && op == "=" && !n.children.empty() && isName(n.children[0])) return true;
    if (n.type == "COMPOUND_ASSIGNMENT_OPERATOR" && !n.children.empty() && isName(n.children[0])) return true;
    if (n.type == "UNARY_OPERATOR" && (op == "++" || op == "--" || op == "&") && !n.children.empty() && isName(n.children[0])) return true;
    if (n.type == "CALL_EXPR")
        for (const Node& c : n.children) if (isName(c)) return true;           // may be passed by reference
    for (const Node& c : n.children) if (mayModify(c, names)) return true;
    return false;
}

bool hasExit(const Node& n) {
    struct Walk {
        static bool go(const Node& x, bool breakable) {
            if (x.type == "RETURN_STMT" || x.type == "GOTO_STMT" || x.type == "CXX_THROW_EXPR") return true;
            if (x.type == "BREAK_STMT" && !breakable) return true;
            const bool inner = breakable || ast::isLoop(x.type) || x.type == "SWITCH_STMT";
            for (const Node& c : x.children) if (go(c, inner)) return true;
            return false;
        }
    };
    for (const Node& c : n.children) if (Walk::go(c, false)) return true;
    return Walk::go(n, false);
}

} // namespace nc::checks
