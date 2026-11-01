#include "engine/cfg/Dominators.hpp"

#include <algorithm>
#include <map>

namespace nc::cfg {

namespace {

bool usable(const Cfg& cfg) {
    return cfg.unsupported.empty() && !cfg.blocks.empty() && cfg.entry >= 0 &&
           cfg.entry < static_cast<int>(cfg.blocks.size());
}

// Reverse post-order of the blocks reachable from the entry (iterative DFS).
std::vector<int> reversePostOrder(const std::vector<std::vector<int>>& succ, int entry) {
    std::vector<int> order;
    std::vector<char> seen(succ.size(), 0);
    std::vector<std::pair<int, size_t>> stack{{entry, 0}};
    seen[static_cast<size_t>(entry)] = 1;
    while (!stack.empty()) {
        auto& [u, next] = stack.back();
        if (next < succ[static_cast<size_t>(u)].size()) {
            const int v = succ[static_cast<size_t>(u)][next++];
            if (!seen[static_cast<size_t>(v)]) {
                seen[static_cast<size_t>(v)] = 1;
                stack.push_back({v, 0});
            }
        } else {
            order.push_back(u);
            stack.pop_back();
        }
    }
    std::reverse(order.begin(), order.end());
    return order;
}

} // namespace

std::vector<int> immediateDominators(const Cfg& cfg) {
    if (!usable(cfg)) return {};
    const auto succ = cfg.successors(), pred = cfg.predecessors();
    const auto rpo = reversePostOrder(succ, cfg.entry);
    std::vector<int> number(cfg.blocks.size(), -1);                 // position in reverse post-order
    for (size_t i = 0; i < rpo.size(); ++i) number[static_cast<size_t>(rpo[i])] = static_cast<int>(i);

    std::vector<int> idom(cfg.blocks.size(), -1);
    idom[static_cast<size_t>(cfg.entry)] = cfg.entry;

    auto intersect = [&](int a, int b) {
        while (a != b) {
            while (number[static_cast<size_t>(a)] > number[static_cast<size_t>(b)]) a = idom[static_cast<size_t>(a)];
            while (number[static_cast<size_t>(b)] > number[static_cast<size_t>(a)]) b = idom[static_cast<size_t>(b)];
        }
        return a;
    };

    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 1; i < rpo.size(); ++i) {                   // rpo[0] is the entry
            const int b = rpo[i];
            int best = -1;
            for (int p : pred[static_cast<size_t>(b)]) {
                if (idom[static_cast<size_t>(p)] == -1) continue;   // unreachable or not processed yet
                best = best == -1 ? p : intersect(p, best);
            }
            if (best != idom[static_cast<size_t>(b)]) {
                idom[static_cast<size_t>(b)] = best;
                changed = true;
            }
        }
    }
    return idom;
}

bool dominates(const std::vector<int>& idom, int a, int b) {
    const int n = static_cast<int>(idom.size());
    if (a < 0 || b < 0 || a >= n || b >= n || idom[static_cast<size_t>(a)] == -1 || idom[static_cast<size_t>(b)] == -1)
        return false;
    for (int x = b;; x = idom[static_cast<size_t>(x)]) {
        if (x == a) return true;
        if (idom[static_cast<size_t>(x)] == x) return false;        // reached the entry without meeting a
    }
}

std::vector<std::vector<int>> dominatorTree(const std::vector<int>& idom) {
    std::vector<std::vector<int>> children(idom.size());
    for (size_t b = 0; b < idom.size(); ++b)
        if (idom[b] != -1 && idom[b] != static_cast<int>(b)) children[static_cast<size_t>(idom[b])].push_back(static_cast<int>(b));
    return children;
}

std::vector<NaturalLoop> naturalLoops(const Cfg& cfg) {
    std::vector<NaturalLoop> loops;
    const auto idom = immediateDominators(cfg);
    if (idom.empty()) return loops;
    const auto pred = cfg.predecessors();

    std::map<int, std::vector<int>> latches;                        // header -> back-edge sources
    for (const Edge& e : cfg.edges)
        if (dominates(idom, e.to, e.from)) latches[e.to].push_back(e.from);

    for (auto& [header, srcs] : latches) {
        std::sort(srcs.begin(), srcs.end());
        srcs.erase(std::unique(srcs.begin(), srcs.end()), srcs.end());
        NaturalLoop loop;
        loop.header = header;
        loop.latches = srcs;
        // The loop body: everything that reaches a latch without passing through the header.
        std::vector<char> in(cfg.blocks.size(), 0);
        in[static_cast<size_t>(header)] = 1;
        std::vector<int> stack;
        for (int l : srcs)
            if (!in[static_cast<size_t>(l)]) { in[static_cast<size_t>(l)] = 1; stack.push_back(l); }
        while (!stack.empty()) {
            const int u = stack.back();
            stack.pop_back();
            for (int p : pred[static_cast<size_t>(u)])
                if (!in[static_cast<size_t>(p)] && idom[static_cast<size_t>(p)] != -1) { in[static_cast<size_t>(p)] = 1; stack.push_back(p); }
        }
        for (size_t i = 0; i < in.size(); ++i)
            if (in[i]) loop.blocks.push_back(static_cast<int>(i));
        loops.push_back(std::move(loop));
    }

    // Nesting: the parent is the smallest other loop that contains this loop's header.
    for (size_t i = 0; i < loops.size(); ++i) {
        int best = -1;
        for (size_t j = 0; j < loops.size(); ++j) {
            if (i == j || !std::binary_search(loops[j].blocks.begin(), loops[j].blocks.end(), loops[i].header)) continue;
            if (best == -1 || loops[j].blocks.size() < loops[static_cast<size_t>(best)].blocks.size()) best = static_cast<int>(j);
        }
        loops[i].parent = best;
    }
    for (size_t i = 0; i < loops.size(); ++i) {
        int depth = 1;
        for (int p = loops[i].parent; p != -1; p = loops[static_cast<size_t>(p)].parent) ++depth;
        loops[i].depth = depth;
    }
    return loops;
}

bool loopHasNoExit(const Cfg& cfg, const NaturalLoop& loop) {
    for (const Edge& e : cfg.edges)
        if (std::binary_search(loop.blocks.begin(), loop.blocks.end(), e.from) &&
            !std::binary_search(loop.blocks.begin(), loop.blocks.end(), e.to))
            return false;
    return true;
}

int maxLoopDepth(const std::vector<NaturalLoop>& loops) {
    int d = 0;
    for (const NaturalLoop& l : loops) d = std::max(d, l.depth);
    return d;
}

bool isReducible(const Cfg& cfg) {
    const auto idom = immediateDominators(cfg);
    if (idom.empty()) return true;
    const auto succ = cfg.successors();
    std::vector<char> state(cfg.blocks.size(), 0);                  // 0 new, 1 on the DFS stack, 2 done
    std::vector<std::pair<int, size_t>> stack{{cfg.entry, 0}};
    state[static_cast<size_t>(cfg.entry)] = 1;
    while (!stack.empty()) {
        auto& [u, next] = stack.back();
        if (next < succ[static_cast<size_t>(u)].size()) {
            const int v = succ[static_cast<size_t>(u)][next++];
            if (state[static_cast<size_t>(v)] == 1) {
                if (!dominates(idom, v, u)) return false;           // retreating edge that is not a back edge
            } else if (state[static_cast<size_t>(v)] == 0) {
                state[static_cast<size_t>(v)] = 1;
                stack.push_back({v, 0});
            }
        } else {
            state[static_cast<size_t>(u)] = 2;
            stack.pop_back();
        }
    }
    return true;
}

} // namespace nc::cfg
