#include "engine/parse/IoStripper.hpp"

#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

// Hand-written matchers instead of std::regex: the originals are Python regexes with nested repetition,
// and std::regex recurses once per character on those (a long statement overflows the stack). Each
// matcher below follows its regex exactly, including the leftmost-match and ordered-alternative rules of
// Python's re.sub. tests/fuzz_strip.py compares this file against the Python on random inputs.

namespace nc::parse {

namespace {

constexpr size_t npos = std::string::npos;

bool isWord(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }   // \w (non-ASCII counts as a letter, as in Python)
bool isSpace(unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }       // \s for ASCII: space \t \n \v \f \r

bool boundaryAt(const std::string& s, size_t i) {                                    // \b
    const bool prev = i > 0 && isWord(static_cast<unsigned char>(s[i - 1]));
    const bool cur = i < s.size() && isWord(static_cast<unsigned char>(s[i]));
    return prev != cur;
}

bool startsWith(const std::string& s, size_t i, const char* lit) {
    for (; *lit; ++lit, ++i)
        if (i >= s.size() || s[i] != *lit) return false;
    return true;
}

size_t skipSpaces(const std::string& s, size_t i) {                                  // \s*
    while (i < s.size() && isSpace(static_cast<unsigned char>(s[i]))) ++i;
    return i;
}

// STR = "(?:\\.|[^"\\])*"   at s[i] == '"'. `.` does not match a newline, so a backslash before one fails.
size_t matchString(const std::string& s, size_t i) {
    ++i;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '"') return i + 1;
        if (c == '\\') {
            if (i + 1 >= s.size() || s[i + 1] == '\n') return npos;
            i += 2;
        } else {
            ++i;
        }
    }
    return npos;
}

// (?:STR|[^;"])*;   from i. Returns the index after the `;`, or npos.
size_t matchToSemicolon(const std::string& s, size_t i) {
    while (i < s.size()) {
        const char c = s[i];
        if (c == ';') return i + 1;
        if (c == '"') {
            i = matchString(s, i);
            if (i == npos) return npos;
        } else {
            ++i;
        }
    }
    return npos;
}

// (?:\bstd::)?\b(?:cin|cout|cerr)\s*(?:>>|<<)(?:STR|[^;"])*;
size_t matchStreamStatement(const std::string& s, size_t i) {
    for (const size_t word_at : {boundaryAt(s, i) && startsWith(s, i, "std::") ? i + 5 : npos, i}) {
        if (word_at == npos || !boundaryAt(s, word_at)) continue;
        for (const char* w : {"cin", "cout", "cerr"}) {
            if (!startsWith(s, word_at, w)) continue;
            size_t j = skipSpaces(s, word_at + std::string(w).size());
            if (startsWith(s, j, ">>") || startsWith(s, j, "<<")) {
                const size_t end = matchToSemicolon(s, j + 2);
                if (end != npos) return end;
            }
        }
    }
    return npos;
}

// \b(?:scanf|printf|puts|freopen)\s*\((?:STR|[^;"])*;
size_t matchCFunction(const std::string& s, size_t i) {
    if (!boundaryAt(s, i)) return npos;
    for (const char* w : {"scanf", "printf", "puts", "freopen"}) {
        if (!startsWith(s, i, w)) continue;
        const size_t j = skipSpaces(s, i + std::string(w).size());
        if (j < s.size() && s[j] == '(') {
            const size_t end = matchToSemicolon(s, j + 1);
            if (end != npos) return end;
        }
    }
    return npos;
}

// ... \s*\([^)]*\)\s*;   from the position just before the `\s*(`
size_t matchParenCall(const std::string& s, size_t j) {
    j = skipSpaces(s, j);
    if (j >= s.size() || s[j] != '(') return npos;
    const size_t close = s.find(')', j + 1);
    if (close == npos) return npos;
    j = skipSpaces(s, close + 1);
    return j < s.size() && s[j] == ';' ? j + 1 : npos;
}

// \b(?:ios_base|ios)::sync_with_stdio\s*\([^)]*\)\s*;
size_t matchSync(const std::string& s, size_t i) {
    if (!boundaryAt(s, i)) return npos;
    for (const char* w : {"ios_base", "ios"}) {
        if (!startsWith(s, i, w)) continue;
        const std::string head = std::string(w) + "::sync_with_stdio";
        if (startsWith(s, i, head.c_str())) {
            const size_t end = matchParenCall(s, i + head.size());
            if (end != npos) return end;
        }
    }
    return npos;
}

// \b(?:cin|cout)\.tie\s*\([^)]*\)\s*;
size_t matchTie(const std::string& s, size_t i) {
    if (!boundaryAt(s, i)) return npos;
    for (const char* w : {"cin", "cout"}) {
        const std::string head = std::string(w) + ".tie";
        if (startsWith(s, i, head.c_str())) {
            const size_t end = matchParenCall(s, i + head.size());
            if (end != npos) return end;
        }
    }
    return npos;
}

// \bgetline\s*\([^;]*;
size_t matchGetline(const std::string& s, size_t i) {
    if (!boundaryAt(s, i) || !startsWith(s, i, "getline")) return npos;
    const size_t j = skipSpaces(s, i + 7);
    if (j >= s.size() || s[j] != '(') return npos;
    const size_t semi = s.find(';', j + 1);
    return semi == npos ? npos : semi + 1;
}

// IO_STMT: the alternatives in the order of the Python pattern.
size_t matchIoStatement(const std::string& s, size_t i) {
    size_t e;
    if ((e = matchStreamStatement(s, i)) != npos) return e;
    if ((e = matchCFunction(s, i)) != npos) return e;
    if ((e = matchSync(s, i)) != npos) return e;
    if ((e = matchTie(s, i)) != npos) return e;
    return matchGetline(s, i);
}

// EMPTY_LOOP = \b(?:for|while)\s*\((?:[^()]|\([^()]*\))*\)\s*;
size_t matchEmptyLoop(const std::string& s, size_t i) {
    if (!boundaryAt(s, i)) return npos;
    for (const char* w : {"for", "while"}) {
        if (!startsWith(s, i, w)) continue;
        size_t j = skipSpaces(s, i + std::string(w).size());
        if (j >= s.size() || s[j] != '(') continue;
        for (++j; j < s.size();) {
            const char c = s[j];
            if (c == '(') {                                   // one level of nesting: \([^()]*\)
                size_t k = j + 1;
                while (k < s.size() && s[k] != '(' && s[k] != ')') ++k;
                if (k >= s.size() || s[k] != ')') return npos;
                j = k + 1;
            } else if (c == ')') {
                j = skipSpaces(s, j + 1);
                return j < s.size() && s[j] == ';' ? j + 1 : npos;
            } else {
                ++j;
            }
        }
        return npos;
    }
    return npos;
}

// re.sub(pattern, ";", s) with a hand-written matcher. Returns the number of replacements.
template <class Matcher>
int substituteAll(std::string& s, Matcher match) {
    std::string out;
    out.reserve(s.size());
    int n = 0;
    for (size_t i = 0; i < s.size();) {
        const size_t end = match(s, i);
        if (end != npos && end > i) {
            out += ';';
            i = end;
            ++n;
        } else {
            out += s[i++];
        }
    }
    s = std::move(out);
    return n;
}

// str.splitlines(): \n, \r\n, \r, \v, \f, \x1c-\x1e, and U+0085, U+2028, U+2029 (as UTF-8).
std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    std::string cur;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t brk = 0;
        if (c == '\r') brk = (i + 1 < s.size() && s[i + 1] == '\n') ? 2 : 1;
        else if (c == '\n' || c == '\v' || c == '\f' || (c >= 0x1c && c <= 0x1e)) brk = 1;
        else if (c == 0xc2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x85) brk = 2;
        else if (c == 0xe2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80 &&
                 (static_cast<unsigned char>(s[i + 2]) == 0xa8 || static_cast<unsigned char>(s[i + 2]) == 0xa9)) brk = 3;
        if (brk) {
            lines.push_back(cur);
            cur.clear();
            i += brk;
        } else {
            cur += s[i++];
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

// INCLUDE_LINE = ^\s*#\s*include   (re.match: anchored at the start of the line only)
bool isIncludeLine(const std::string& line) {
    size_t i = skipSpaces(line, 0);
    if (i >= line.size() || line[i] != '#') return false;
    i = skipSpaces(line, i + 1);
    return startsWith(line, i, "include");
}

} // namespace

StripResult stripIo(const std::string& source) {
    StripResult r;
    std::string code;
    bool first = true;
    for (const std::string& line : splitLines(source)) {
        if (isIncludeLine(line)) { ++r.includes_removed; continue; }
        if (!first) code += '\n';
        code += line;
        first = false;
    }
    r.io_statements_removed = substituteAll(code, matchIoStatement);
    for (;;) {
        const int n = substituteAll(code, matchEmptyLoop);
        if (n == 0) break;
        r.loops_removed += n;
    }
    r.code = std::move(code);
    return r;
}

bool needsStrip(const std::string& source) {
    for (size_t i = 0; i < source.size(); ++i)
        if (matchIoStatement(source, i) != npos) return true;
    return false;
}

const char* trainingPreamble() {
    return "#include <algorithm>\n#include <iostream>\n#include <cstdio>\n#include <cstdlib>\n#include <numeric>\n"
           "#include <climits>\n#include <cmath>\n#include <cstring>\n#include <deque>\n#include <map>\n#include <queue>\n"
           "#include <set>\n#include <stack>\n#include <string>\n#include <unordered_map>\n#include <unordered_set>\n"
           "#include <utility>\n#include <vector>\nusing namespace std;\n";
}

} // namespace nc::parse
