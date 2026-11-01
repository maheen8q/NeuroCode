#include "engine/cfg/Reachability.hpp"

#include <algorithm>

namespace nc::cfg {

namespace {

bool usable(const Cfg& cfg) {
    return cfg.unsupported.empty() && !cfg.blocks.empty() && cfg.entry >= 0 && cfg.exit >= 0 &&
           cfg.entry < static_cast<int>(cfg.blocks.size()) && cfg.exit < static_cast<int>(cfg.blocks.size());
}

// Iterative search over `adj` from `start`; marks every visited block.
std::vector<bool> search(const std::vector<std::vector<int>>& adj, int start) {
    std::vector<bool> seen(adj.size(), false);
    std::vector<int> stack{start};
    seen[static_cast<size_t>(start)] = true;
    while (!stack.empty()) {
        const int u = stack.back();
        stack.pop_back();
        for (int v : adj[static_cast<size_t>(u)])
            if (!seen[static_cast<size_t>(v)]) {
                seen[static_cast<size_t>(v)] = true;
                stack.push_back(v);
            }
    }
    return seen;
}

} // namespace

std::vector<bool> reachableBlocks(const Cfg& cfg) {
    if (!usable(cfg)) return {};
    return search(cfg.successors(), cfg.entry);
}

std::vector<bool> canReachExit(const Cfg& cfg) {
    if (!usable(cfg)) return {};
    return search(cfg.predecessors(), cfg.exit);
}

std::vector<int> unreachableBlocks(const Cfg& cfg) {
    std::vector<int> out;
    const auto reach = reachableBlocks(cfg);
    for (size_t i = 0; i < reach.size(); ++i)
        if (!reach[i] && !cfg.blocks[i].lines.empty()) out.push_back(cfg.blocks[i].id);
    return out;
}

std::vector<int> unreachableLines(const Cfg& cfg) {
    std::vector<int> lines;
    for (int id : unreachableBlocks(cfg)) {
        const auto& ls = cfg.blocks[static_cast<size_t>(id)].lines;
        lines.insert(lines.end(), ls.begin(), ls.end());
    }
    std::sort(lines.begin(), lines.end());
    lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
    return lines;
}

std::vector<int> blocksThatCannotReachExit(const Cfg& cfg) {
    std::vector<int> out;
    const auto reach = reachableBlocks(cfg), toExit = canReachExit(cfg);
    for (size_t i = 0; i < reach.size(); ++i)
        if (reach[i] && !toExit[i]) out.push_back(cfg.blocks[i].id);
    return out;
}

bool exitReachable(const Cfg& cfg) {
    const auto reach = reachableBlocks(cfg);
    return !reach.empty() && reach[static_cast<size_t>(cfg.exit)];
}

bool allPathsReturn(const Cfg& cfg) {
    const auto reach = reachableBlocks(cfg);
    if (reach.empty()) return false;
    for (const Edge& e : cfg.edges)
        if (e.to == cfg.exit && reach[static_cast<size_t>(e.from)] && cfg.blocks[static_cast<size_t>(e.from)].kind != "return")
            return false;
    return true;
}

} // namespace nc::cfg
