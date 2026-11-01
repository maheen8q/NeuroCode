#pragma once
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace nc::cfg {

struct Block {
    int id = 0;
    std::string kind;            // entry, exit, basic, loop_cond, loop_inc, branch, return
    std::vector<int> lines;      // source lines covered, [] for the synthetic exit
};

struct Edge {
    int from = 0, to = 0;
    std::string kind;            // next, true, false, back, case, default
};

// One control flow graph per function: a directed graph of basic blocks.
struct Cfg {
    std::string function;
    int line = 0;                // line of the function in the user's code
    int entry = 0, exit = 0;
    std::vector<Block> blocks;
    std::vector<Edge> edges;
    bool falls_off_end = false;  // execution can reach the end of the body without a return
    int cyclomatic = 1;          // decision points + 1, same definition as the feature vector
    std::string unsupported;     // non-empty: no graph was built ("goto", "try/catch"); blocks/edges are empty

    std::vector<std::vector<int>> successors() const;     // adjacency lists, indexed by block id
    std::vector<std::vector<int>> predecessors() const;
};

// {"schema_version":1,"functions":[{name,line,entry,exit,blocks,edges,falls_off_end,cyclomatic} | {name,line,unsupported}]}
nlohmann::json cfgsToJson(const std::vector<Cfg>& cfgs);
std::string cfgsToJsonString(const std::vector<Cfg>& cfgs);

} // namespace nc::cfg
