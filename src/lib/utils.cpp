#include "utils.hpp"
#include "node.hpp"
#define THRESHOLD_EXACT_SOLVE 16

#include <cassert>
#include <random>
#include <ctime>
#include <functional>
#include <iostream>
#include <cmath>
#include <fstream>
#include <sstream>
#include <regex>

namespace {
    // Thread-local RNG to avoid multiple definition and be safe in multithreaded contexts
    thread_local std::mt19937 tl_engine(static_cast<unsigned int>(std::time(nullptr)));
    thread_local std::uniform_real_distribution<double> tl_uniform01(0.0, 1.0);
}

Graph::Graph(int numVertices) : numVertices(numVertices) {
    adjacencyList.resize(numVertices);
    weights.resize(numVertices, 1);  // Default weight is 1 for all vertices
}

Graph::~Graph() {
    // No dynamic memory to free
}

void Graph::addEdge(int u, int v) {
    adjacencyList[u].push_back(v);
    adjacencyList[v].push_back(u);
}

void Graph::print() {
    std::cout << "Graph with " << numVertices << " vertices:\n";
    for (int i = 0; i < numVertices; ++i) {
        std::cout << "Vertex " << i << " (weight " << weights[i] << "): ";
        for (int neighbor : adjacencyList[i]) {
            std::cout << neighbor << " ";
        }
        std::cout << "\n";
    }
}

State::State() : isSelected(), selectedVertices(), possibleVertices() {}

State::State(int numVertices) : isSelected(numVertices, false), selectedVertices(), possibleVertices() {
    for (int i = 0; i < numVertices; ++i) {
        possibleVertices.insert(i);
        expandable++;
    }
}

State::State(std::vector<bool> isSelectedInit)
    : isSelected(isSelectedInit), selectedVertices(), possibleVertices() {
    for (int i = 0; i < static_cast<int>(isSelected.size()); ++i) {
        if (isSelected[i]) {
            selectedVertices.insert(i);
        } else {
            possibleVertices.insert(i);
            expandable++;
        }
    }
}

State::~State() {
    // No dynamic memory to free
}

void State::include(int vertex) {
    if (vertex >= 0 && vertex < static_cast<int>(isSelected.size())) {
        isSelected[vertex] = true;
        selectedVertices.insert(vertex);
        possibleVertices.erase(vertex);
    }
}

void State::exclude(int vertex) {
    if (vertex >= 0 && vertex < static_cast<int>(isSelected.size())) {
        assert(!isSelected[vertex] && "Excluding a vertex that is not selected");
        possibleVertices.erase(vertex);
        expandable--;
    }
}

int State::randomVertex() {
    int r = static_cast<int>(tl_uniform01(tl_engine) * static_cast<double>(this->getActionCounts()));

    for (int i = 0; i < this->isSelected.size(); ++i) {
        if (!this->isSelected[i]) {
            if (r == 0) return i;
            --r;
        }
    }
    assert(false);
    return -1;
}

int State::getActionCounts() {
    return possibleVertices.size();
}

double State::evaluate() {
    assert(!selectedVertices.empty() && "Evaluating state with no selected vertices");
    return 1/static_cast<double>(selectedVertices.size());
}

bool State::isValid(const Graph& graph) const {
    for (int u = 0; u < graph.numVertices; ++u) {
        for (int v : graph.adjacencyList[u]) {
            if (u < v) {
                bool cu = selectedVertices.count(u) > 0;
                bool cv = selectedVertices.count(v) > 0;
                if (!(cu || cv)) return false;
            }
        }
    }
    return true;
}

namespace UCT {
    Node* sampling(std::vector<Node*>& children, double explorationParam) {
        assert(!children.empty());

        double totalVisits = 0.0;
        for (const Node* child : children) {
            totalVisits += static_cast<double>(child->visits);
        }

        std::vector<double> weights;
        weights.reserve(children.size());

        for (const Node* child : children) {
            double uctValue = child->value +
                              2.0 * explorationParam *
                              std::sqrt(2.0 * std::log(std::max(1.0, static_cast<double>(child->visits))) /
                                        std::max(1.0, totalVisits));
            weights.push_back(std::max(0.0, uctValue));
        }

        double sum = 0.0;
        for (double& w : weights) {
            sum += w;
            w = sum;
        }

        // if (sum <= 0.0) {
        //     std::size_t idx = static_cast<std::size_t>(tl_uniform01(tl_engine) * children.size());
        //     if (idx >= children.size()) idx = children.size() - 1;
        //     return children[idx];
        // }

        double r = tl_uniform01(tl_engine) * sum;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            if (r <= weights[i]) return children[i];
        }
        // Numerical edge case: return last
        assert(false);
        // return children.back();
    }
}

// ---- JSON loader for Graph ----
Graph loadGraphFromJson(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        assert(false && "Could not open JSON graph file");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();

    std::smatch m;
    int n = 0;
    std::regex reN("\"num_vertices\"\\s*:\\s*(\\d+)");
    if (std::regex_search(s, m, reN) && m.size() >= 2) {
        n = std::stoi(m[1]);
    } else {
        assert(false && "num_vertices not found in JSON");
    }

    Graph g(n);
    std::regex reEdge("\\[\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\]");
    auto edges_begin = std::sregex_iterator(s.begin(), s.end(), reEdge);
    auto edges_end = std::sregex_iterator();
    for (auto it = edges_begin; it != edges_end; ++it) {
        int u = std::stoi((*it)[1]);
        int v = std::stoi((*it)[2]);
        g.addEdge(u, v);
    }
    return g;
}

std::pair<Graph, std::vector<std::vector<int>>> GraphOracle::coarsenGraph(const Graph& g) {
    const int n = g.numVertices;
    // Step 1: Compute core number c(v) and degree bucket for each vertex
    // Compute degeneracy-based core numbers via lazy min-heap peeling
    std::vector<int> deg(n, 0);
    for (int v = 0; v < n; ++v) deg[v] = static_cast<int>(g.adjacencyList[v].size());

    std::vector<int> core(n, 0);
    std::vector<bool> removed(n, false);
    // Lazy min-heap of (degree, vertex)
    std::priority_queue<std::pair<int,int>, std::vector<std::pair<int,int>>, std::greater<std::pair<int,int>>> pq;
    for (int v = 0; v < n; ++v) pq.emplace(deg[v], v);
    while (!pq.empty()) {
        auto [d, v] = pq.top(); pq.pop();
        if (removed[v] || d != deg[v]) continue; // stale entry
        removed[v] = true;
        core[v] = d; // current degree at removal time is core number
        for (int u : g.adjacencyList[v]) {
            if (!removed[u]) {
                if (deg[u] > 0) deg[u]--;
                pq.emplace(deg[u], u);
            }
        }
    }

    // Build buckets keyed by (core, floor(log2(deg+1)))
    auto degreeBucket = [](int d) {
        return static_cast<int>(std::floor(std::log2(static_cast<double>(d) + 1.0)));
    };

    std::unordered_map<long long, std::vector<int>> buckets; // key = (core<<32) | degBucket
    buckets.reserve(n);
    for (int v = 0; v < n; ++v) {
        int db = degreeBucket(static_cast<int>(g.adjacencyList[v].size()));
        long long key = (static_cast<long long>(core[v]) << 32) | static_cast<unsigned int>(db);
        buckets[key].push_back(v);
    }

    // Helper sets for quick membership tests
    auto makeSet = [](const std::vector<int>& vec) {
        std::unordered_set<int> s; s.reserve(vec.size()*2);
        for (int x : vec) s.insert(x);
        return s;
    };

    // Step 2: Matching within each bucket
    std::vector<std::pair<int,int>> matchedPairs; matchedPairs.reserve(n/2);
    std::vector<int> preservedSingles; preservedSingles.reserve(n/2);
    std::vector<char> isMatched(n, 0);

    for (auto &kv : buckets) {
        const std::vector<int>& B = kv.second;
        if (B.empty()) continue;
        auto Bset = makeSet(B);

        // 2a. locality-aware matching: prefer adjacent pairs in the same bucket
        for (int v : B) {
            if (isMatched[v]) continue;
            for (int u : g.adjacencyList[v]) {
                if (!isMatched[u] && Bset.count(u)) {
                    // match (v,u)
                    isMatched[v] = isMatched[u] = 1;
                    matchedPairs.emplace_back(v, u);
                    break;
                }
            }
        }

        // Collect unmatched from this bucket
        std::vector<int> remain;
        remain.reserve(B.size());
        for (int v : B) if (!isMatched[v]) remain.push_back(v);

        // 2b. 2-hop matching: share a neighbor
        auto inRemain = [&remain](int x){ return std::find(remain.begin(), remain.end(), x) != remain.end(); };
        std::unordered_set<int> remainSet;
        remainSet.reserve(remain.size()*2);
        for (int x : remain) remainSet.insert(x);

        std::vector<char> used(remain.size(), 0);
        for (size_t i = 0; i < remain.size(); ++i) {
            int v = remain[i]; if (used[i]) continue;
            bool paired = false;
            // Check neighbors n of v, and neighbors w of n
            for (int nbh : g.adjacencyList[v]) {
                for (int w : g.adjacencyList[nbh]) {
                    if (w != v && remainSet.count(w)) {
                        // Pair v with w
                        size_t j = std::find(remain.begin(), remain.end(), w) - remain.begin();
                        if (j < remain.size() && !used[j]) {
                            used[i] = used[j] = 1;
                            isMatched[v] = isMatched[w] = 1;
                            matchedPairs.emplace_back(v, w);
                            paired = true;
                            break;
                        }
                    }
                }
                if (paired) break;
            }
        }

        // 2c. Random/sequence matching for any leftover
        std::vector<int> leftovers;
        for (size_t i = 0; i < remain.size(); ++i) if (!used[i] && !isMatched[remain[i]]) leftovers.push_back(remain[i]);
        // If odd, preserve one unmatched
        if (leftovers.size() % 2 == 1) {
            preservedSingles.push_back(leftovers.back());
            leftovers.pop_back();
        }
        for (size_t i = 0; i + 1 < leftovers.size(); i += 2) {
            int a = leftovers[i], b = leftovers[i+1];
            isMatched[a] = isMatched[b] = 1;
            matchedPairs.emplace_back(a, b);
        }
    }

    // Step 3: Contract matched pairs into supernodes; preserve singles
    // Build groups (each group is either {u,v} or {x})
    std::vector<std::vector<int>> groups;
    groups.reserve(matchedPairs.size() + preservedSingles.size());
    for (auto &p : matchedPairs) groups.push_back({p.first, p.second});
    for (int x : preservedSingles) groups.push_back({x});

    int n2 = static_cast<int>(groups.size());
    Graph G2(n2);
    // Initialize weights
    for (int i = 0; i < n2; ++i) {
        int wsum = 0;
        for (int v : groups[i]) wsum += (v >= 0 && v < (int)g.weights.size() ? g.weights[v] : 1);
        G2.weights[i] = wsum;
    }

    // Map old vertex -> new supernode index
    std::vector<int> mapOldToNew(n, -1);
    for (int i = 0; i < n2; ++i) {
        for (int v : groups[i]) mapOldToNew[v] = i;
    }

    // Build edges in coarsened graph
    std::vector<std::unordered_set<int>> nbrSets(n2);
    for (int i = 0; i < n2; ++i) nbrSets[i].reserve(8);
    for (int u = 0; u < n; ++u) {
        int su = mapOldToNew[u];
        for (int v : g.adjacencyList[u]) {
            int sv = mapOldToNew[v];
            if (su == sv) continue; // discard self-loop from matched pair
            nbrSets[su].insert(sv);
            nbrSets[sv].insert(su);
        }
    }
    // Transfer neighbor sets to adjacency lists
    for (int i = 0; i < n2; ++i) {
        for (int j : nbrSets[i]) G2.addEdge(i, j);
    }

    return {G2, groups};
};

State GraphOracle::coarseSolve(const Graph& graph) {
    if (graph.numVertices <= THRESHOLD_EXACT_SOLVE) {
        return exactSolve(graph);
    }
    auto [coarseGraph, groups] = GraphOracle::coarsenGraph(graph);
    if (graph.numVertices == coarseGraph.numVertices) {
        return greedySolve(graph);
    }

    State coarseSol = coarseSolve(coarseGraph);
    
    // lifting
    State graphSol(graph.numVertices);
    for (auto gnum : coarseSol.selectedVertices) {
        for (int v : groups[gnum]) {
            graphSol.include(v);
        }
        // std::shuffle(groups[gnum].begin(), groups[gnum].end(), tl_engine);
        // std::sort(groups[gnum].begin(), groups[gnum].end(), [&](int a, int b) {
        //     return graph.weights[a] < graph.weights[b];
        // });
        // graphSol.include(groups[gnum][0]);
    }

    // define functions and edge list
    const int n = graph.numVertices;
    std::vector<std::pair<int, int>> edges;
    for (int u = 0; u < n; ++u) {
        for (int v : graph.adjacencyList[u]) {
            if (u < v) edges.emplace_back(u, v);
        }
    }
    auto selected = [&](int x) {
        return graphSol.selectedVertices.count(x) > 0;
    };
    auto covered = [&](int u, int v) {
        return selected(u) || selected(v);
    };
    auto uncoveredExists = [&]() {
        for (auto &e : edges) if (!covered(e.first, e.second)) return true;
        return false;
    };

    // Greedy local fix: repeatedly add the max-degree vertex among uncovered edges
    while (uncoveredExists()) {
        std::vector<int> deg(n, 0);
        for (auto &e : edges) {
            int u = e.first, v = e.second;
            if (!covered(u, v)) {
                deg[u]++;
                deg[v]++;
            }
        }
        int w = -1, best = -1;
        for (int i = 0; i < n; ++i) {
            if (!selected(i) && deg[i] > best) { best = deg[i]; w = i; }
        }
        if (w == -1) {
            // Fallback: pick any endpoint of an uncovered edge that isn't selected yet
            for (auto &e : edges) {
                int u = e.first, v = e.second;
                if (!covered(u, v)) {
                    if (!selected(u)) { w = u; break; }
                    if (!selected(v)) { w = v; break; }
                }
            }
        }
        if (w == -1) break; // safety: nothing to add
        graphSol.include(w);
    }

    std::cout << (double)coarseSol.selectedVertices.size()/(double)coarseGraph.numVertices << " -> "
         << (double)graphSol.selectedVertices.size()/(double)graph.numVertices << "\n";

    return graphSol;
}

State GraphOracle::exactSolve(const Graph& graph) {
    // Simple exact solver via brute-force (for small graphs)
    const int n = graph.numVertices;
    State bestState;
    int bestSize = 0;
    for (int i = 0; i < n; ++i) {
        bestSize += graph.weights[i];
    }
    const int total = 1 << n;
    for (int mask = 0; mask < total; ++mask) {
        State currState(n);
        for (int i = 0; i < n; ++i) {
            if (mask & (1 << i)) {
                currState.include(i);
            }
        }
        // Check if currState is a valid vertex cover
        bool valid = true;
        for (int u = 0; u < n; ++u) {
            for (int v : graph.adjacencyList[u]) {
                if (u < v) {
                    if (!currState.selectedVertices.count(u) && !currState.selectedVertices.count(v)) {
                        valid = false;
                        break;
                    }
                }
            }
            if (!valid) break;
        }
        if (valid) {
            int size = 0;
            for (int i = 0; i < n; ++i) {
                if (currState.selectedVertices.count(i)) {
                    size += graph.weights[i];
                }
            }
            if (size < bestSize) {
                bestSize = size;
                bestState = currState;
            }
        }
    }
    return bestState;
};

State GraphOracle::greedySolve(const Graph& graph) {
    // Rough rollout: starting from current selection, greedily add vertices until all edges are covered
    const int n = graph.numVertices;
    std::vector<std::vector<int>> adj = graph.adjacencyList;

    // Track selection as a local copy
    std::vector<bool> sel(n, false);

    auto covered = [&](int u, int v) {
        return sel[u] || sel[v];
    };

    // Build edge list from adjacency (u < v)
    std::vector<std::pair<int,int>> edges;
    for (int u = 0; u < n; ++u) {
        for (int v : adj[u]) {
            if (u < v) edges.emplace_back(u, v);
        }
    }

    // Greedy: add max-degree vertex among uncovered edges until covered
    auto uncoveredExists = [&]() {
        for (auto &e : edges) if (!covered(e.first, e.second)) return true;
        return false;
    };

    while (uncoveredExists()) {
        // Compute degrees on uncovered edges
        std::vector<int> deg(n, 0);
        for (auto &e : edges) {
            int u = e.first, v = e.second;
            if (!covered(u, v)) {
                deg[u]++;
                deg[v]++;
            }
        }
        // pick argmax deg among not-yet-selected
        int w = -1, best = -1;
        for (int i = 0; i < n; ++i) {
            if (!sel[i] && deg[i] > best) { best = deg[i]; w = i; }
        }
        if (w == -1) {
            // fallback: pick any unselected vertex
            for (int i = 0; i < n; ++i) { if (!sel[i]) { w = i; break; } }
        }
        if (w == -1) break; // all selected
        sel[w] = true;
    }

    return State(sel);
}