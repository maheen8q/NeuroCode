#include "SummaryBuilder.hpp"
#include <fstream>
#include <sstream>

namespace nc::optimize {

SummaryBuilder::SummaryBuilder(std::string templatesDir)
    : dir_(std::move(templatesDir)) {}

std::string SummaryBuilder::readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string SummaryBuilder::replaceAll(std::string s,
                                       const std::string& from,
                                       const std::string& to) {
    if (from.empty()) return s;
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string SummaryBuilder::build(const std::string& fileName,
                                  const std::string& arr,
                                  const std::string& target) const {
    std::string code = readFile(dir_ + "/" + fileName);
    if (code.empty()) return "";
    code = replaceAll(code, "{{ARR}}", arr.empty() ? "a" : arr);
    code = replaceAll(code, "{{TARGET}}", target.empty() ? "k" : target);
    return code;
}

} // namespace nc::optimize
