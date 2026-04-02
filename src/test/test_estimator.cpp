#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>
#include <unordered_set>
#include <cstdint>
#include <random>
#include <queue>

#include "../lib/utils.hpp"

static std::vector<std::pair<int, int>> build_edges(const Graph& graph,
                                                    const std::unordered_set<int>* activeSet = nullptr) {
    std::vector<std::pair<int, int>> edges;
    for (int u = 0; u < graph.numVertices; ++u) {
        if (activeSet && !activeSet->count(u)) continue;
        for (int v : graph.adjacencyList[u]) {
            if (activeSet && !activeSet->count(v)) continue;
            if (u < v) edges.push_back({u, v});
        }
    }
    return edges;
}

static bool all_edges_covered(const std::vector<std::pair<int, int>>& edges,
                              const std::vector<bool>& selected) {
    for (const auto& e : edges) {
        if (!selected[e.first] && !selected[e.second]) return false;
    }
    return true;
}

static int selected_count(const std::vector<bool>& selected) {
    int c = 0;
    for (bool b : selected) if (b) ++c;
    return c;
}

static int first_uncovered_edge_index(const std::vector<std::pair<int, int>>& edges,
                                      const std::vector<bool>& selected) {
    for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
        const auto& e = edges[i];
        if (!selected[e.first] && !selected[e.second]) return i;
    }
    return -1;
}

static uint64_t to_mask_u64(const std::vector<bool>& selected) {
    uint64_t mask = 0ULL;
    for (int i = 0; i < static_cast<int>(selected.size()); ++i) {
        if (selected[i]) mask |= (1ULL << i);
    }
    return mask;
}

static void enumerate_all_mvc_bruteforce(const Graph& graph,
                                         const std::unordered_set<int>& activeSet,
                                         long long& totalMvcCount,
                                         std::vector<long long>& vertexInMvcCount,
                                         int& mvcSize) {
    totalMvcCount = 0;
    vertexInMvcCount.assign(graph.numVertices, 0);
    mvcSize = -1;

    if (activeSet.size() > 20) {
        return; // bitmask 기반 브루트포스 한계
    }

    const auto edges = build_edges(graph, &activeSet);
    std::vector<bool> selected(graph.numVertices, false);
    int bestSize = std::numeric_limits<int>::max();
    std::unordered_set<uint64_t> bestMasks;

    std::function<void()> dfs = [&]() {
        int curSize = selected_count(selected);
        if (curSize > bestSize) return;

        int uncoveredIdx = first_uncovered_edge_index(edges, selected);
        if (uncoveredIdx == -1) {
            if (curSize < bestSize) {
                bestSize = curSize;
                bestMasks.clear();
            }
            if (curSize == bestSize) {
                bestMasks.insert(to_mask_u64(selected));
            }
            return;
        }

        const auto [u, v] = edges[uncoveredIdx];

        selected[u] = true;
        dfs();
        selected[u] = false;

        selected[v] = true;
        dfs();
        selected[v] = false;
    };

    dfs();

    if (bestSize == std::numeric_limits<int>::max()) {
        mvcSize = 0;
        totalMvcCount = 1;
        return;
    }

    mvcSize = bestSize;
    totalMvcCount = static_cast<long long>(bestMasks.size());
    for (uint64_t mask : bestMasks) {
        for (int v = 0; v < graph.numVertices; ++v) {
            if (mask & (1ULL << v)) {
                vertexInMvcCount[v]++;
            }
        }
    }
}

// Keep this in sync with current estimatePolicy used in perf_mcts.cpp
static void init_estimate_policy() {
	treePolicy::setEstimatePolicy([](const State& state, const Graph& graph, bool include) {
        // Strategy #8 (Dual-based probability):
        // Approximate LP-dual edge packing y_e with constraints
        //   sum_{e incident v} y_e <= 1, y_e >= 0
        // and set p_v proportional to incident dual mass:
        //   p_v ∝ sum_{e incident v} y_e.
        double prob = 0.5;

        const int v = state.actionVertex;
        if (v >= 0 && v < graph.numVertices && state.possibleVertices.count(v)) {
            // Active core indexing
            std::vector<int> activeVerts;
            activeVerts.reserve(state.possibleVertices.size());
            std::vector<int> idxOf(graph.numVertices, -1);
            for (int u : state.possibleVertices) {
                idxOf[u] = static_cast<int>(activeVerts.size());
                activeVerts.push_back(u);
            }

            const int n = static_cast<int>(activeVerts.size());
            const int actionIdx = idxOf[v];
            if (n > 0 && actionIdx >= 0) {
                // Build active edge list (local indices)
                std::vector<std::pair<int, int>> edges;
                for (int ug : activeVerts) {
                    int u = idxOf[ug];
                    for (int vg : graph.adjacencyList[ug]) {
                        int w = (vg >= 0 && vg < graph.numVertices) ? idxOf[vg] : -1;
                        if (w >= 0 && u < w) edges.push_back({u, w});
                    }
                }

                if (edges.empty()) {
                    prob = 0.01; // no edge pressure => typically excluded
                } else {
                    // Greedy dual packing approximation
                    std::vector<double> rem(n, 1.0);     // remaining dual capacity per vertex
                    std::vector<double> load(n, 0.0);    // sum incident y_e per vertex

                    // Multiple light passes to reduce order bias
                    constexpr int kPasses = 3;
                    for (int pass = 0; pass < kPasses; ++pass) {
                        for (const auto& e : edges) {
                            int a = e.first;
                            int b = e.second;
                            if (rem[a] <= 1e-12 || rem[b] <= 1e-12) continue;

                            // Add only a fraction to share capacity across edges
                            double delta = 0.5 * std::min(rem[a], rem[b]);
                            if (delta <= 1e-12) continue;

                            rem[a] -= delta;
                            rem[b] -= delta;
                            load[a] += delta;
                            load[b] += delta;
                        }
                    }

                    // p_v proportional to incident dual mass
                    double maxLoad = 0.0;
                    for (double x : load) maxLoad = std::max(maxLoad, x);
                    if (maxLoad > 1e-12) {
                        prob = load[actionIdx] / maxLoad;
                    } else {
                        prob = 0.5;
                    }

                    prob = std::max(0.01, std::min(0.99, prob));
                }
            }
        }

        return include ? prob : 1 - prob;
    });
}

namespace {
class NemhauserTrotter {
    int n;
    const std::vector<std::vector<int>>& adj;
    const std::unordered_set<int>& possible;
    std::vector<int> pairU;
    std::vector<int> pairV;
    std::vector<int> dist;

public:
    NemhauserTrotter(int n,
                     const std::vector<std::vector<int>>& adj,
                     const std::unordered_set<int>& possible)
        : n(n), adj(adj), possible(possible), pairU(n, -1), pairV(n, -1), dist(n, 0) {}

    bool bfs() {
        std::queue<int> q;
        int distNIL = std::numeric_limits<int>::max();
        for (int u = 0; u < n; ++u) {
            if (!possible.count(u)) continue;
            if (pairU[u] == -1) {
                dist[u] = 0;
                q.push(u);
            } else {
                dist[u] = std::numeric_limits<int>::max();
            }
        }

        while (!q.empty()) {
            int u = q.front();
            q.pop();
            if (dist[u] < distNIL) {
                for (int v : adj[u]) {
                    if (!possible.count(v)) continue;
                    if (pairV[v] == -1) {
                        if (distNIL == std::numeric_limits<int>::max()) {
                            distNIL = dist[u] + 1;
                        }
                    } else if (dist[pairV[v]] == std::numeric_limits<int>::max()) {
                        dist[pairV[v]] = dist[u] + 1;
                        q.push(pairV[v]);
                    }
                }
            }
        }
        return distNIL != std::numeric_limits<int>::max();
    }

    bool dfs(int u) {
        if (u != -1) {
            for (int v : adj[u]) {
                if (!possible.count(v)) continue;
                if (pairV[v] == -1 || (dist[pairV[v]] == dist[u] + 1 && dfs(pairV[v]))) {
                    pairV[v] = u;
                    pairU[u] = v;
                    return true;
                }
            }
            dist[u] = std::numeric_limits<int>::max();
            return false;
        }
        return true;
    }

    void computeMaxMatching() {
        while (bfs()) {
            for (int u = 0; u < n; ++u) {
                if (possible.count(u) && pairU[u] == -1) dfs(u);
            }
        }
    }

    void getKernelNodes(std::vector<int>& toInclude, std::vector<int>& toExclude) {
        computeMaxMatching();

        std::vector<bool> ZL(n, false), ZR(n, false);
        std::queue<int> q;
        for (int u = 0; u < n; ++u) {
            if (possible.count(u) && pairU[u] == -1) {
                ZL[u] = true;
                q.push(u);
            }
        }

        while (!q.empty()) {
            int u = q.front();
            q.pop();
            for (int v : adj[u]) {
                if (!possible.count(v)) continue;
                if (pairU[u] == v) continue;
                if (!ZR[v]) {
                    ZR[v] = true;
                    if (pairV[v] != -1) {
                        int w = pairV[v];
                        if (!ZL[w]) {
                            ZL[w] = true;
                            q.push(w);
                        }
                    }
                }
            }
        }

        for (int u = 0; u < n; ++u) {
            if (!possible.count(u)) continue;
            bool inCL = !ZL[u];
            bool inCR = ZR[u];
            if (inCL && inCR) toInclude.push_back(u);
            else if (!inCL && !inCR) toExclude.push_back(u);
        }
    }
};
}

static void apply_crown_decomposition(State& state, const Graph& graph) {
    bool changed = true;
    while (changed) {
        changed = false;
        if (state.possibleVertices.empty()) break;

        NemhauserTrotter nt(graph.numVertices, graph.adjacencyList, state.possibleVertices);
        std::vector<int> toInclude;
        std::vector<int> toExclude;
        nt.getKernelNodes(toInclude, toExclude);

        if (!toInclude.empty() || !toExclude.empty()) {
            for (int u : toInclude) {
                if (state.possibleVertices.count(u)) state.include(u);
            }
            for (int u : toExclude) {
                if (state.possibleVertices.count(u)) state.exclude(u);
            }
            changed = true;
        }
    }
}

int main(int argc, char** argv) {
	std::string graphPath = "data/exact/inputs/graph_0000.json";
	if (argc >= 2) {
		graphPath = argv[1];
	}

	Graph graph = loadGraphFromJson(graphPath);
	init_estimate_policy();

    State coreState(graph.numVertices);
    apply_crown_decomposition(coreState, graph);

    std::unordered_set<int> crownCore = coreState.possibleVertices;

    long long totalMvcCount = 0;
    std::vector<long long> vertexInMvcCount;
    int mvcSize = -1;
    enumerate_all_mvc_bruteforce(graph, crownCore, totalMvcCount, vertexInMvcCount, mvcSize);

	std::cout << "Graph: " << graphPath << "\n";
	std::cout << "num_vertices=" << graph.numVertices << "\n";
    std::cout << "crown_core_vertices=" << crownCore.size() << "\n";
    if (crownCore.size() > 20) {
        std::cout << "mvc_size=NA (crown_core_vertices > 20)\n";
        std::cout << "total_mvc_count=NA (crown_core_vertices > 20)\n";
    } else {
        std::cout << "mvc_size=" << mvcSize << "\n";
        std::cout << "total_mvc_count=" << totalMvcCount << "\n";
    }
    std::cout << "vertex,prob_include,mvc_inclusion_count\n";

	State state(graph.numVertices);
    state.possibleVertices = crownCore; // run only on crown core vertices
	for (int v = 0; v < graph.numVertices; ++v) {
		if (!crownCore.count(v)) continue;
		state.actionVertex = v;
		double pInclude = treePolicy::estimatePolicy(state, graph, true);
        long long c = (v < static_cast<int>(vertexInMvcCount.size())) ? vertexInMvcCount[v] : -1;
        std::cout << v << "," << std::fixed << std::setprecision(6) << pInclude << "," << c << "\n";
	}

	return 0;
}
