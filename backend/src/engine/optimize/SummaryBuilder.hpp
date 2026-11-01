#pragma once
#include <string>

namespace nc::optimize {

// Loads a template file from Templates/ and fills {{PLACEHOLDER}} tokens.
// Keeps rewrite text out of the pass logic.
class SummaryBuilder {
public:
    // templatesDir defaults to the folder next to the binary's working dir.
    // Run binaries from backend/, matching the rest of the project.
    explicit SummaryBuilder(std::string templatesDir = "src/engine/optimize/Templates");

    // Reads <templatesDir>/<fileName>, replaces every {{arr}} -> arr value.
    // Unknown tokens are left as-is. Missing file returns "".
    std::string build(const std::string& fileName,
                      const std::string& arr,
                      const std::string& target) const;

private:
    std::string dir_;
    static std::string readFile(const std::string& path);
    static std::string replaceAll(std::string s,
                                  const std::string& from,
                                  const std::string& to);
};

} // namespace nc::optimize
