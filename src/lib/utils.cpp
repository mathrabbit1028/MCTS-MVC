#include "utils.hpp"
#include "node.hpp"
#include <cassert>
#include <random>
#include <ctime>
#include <functional>
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
}

Graph::~Graph() {
    // No dynamic memory to free
}

void Graph::addEdge(int u, int v) {
    adjacencyList[u].push_back(v);
    adjacencyList[v].push_back(u);
}

State::State() : isSelected(), selectedVertices(), possibleVertices() {}

State::State(int numVertices) : isSelected(numVertices, false), selectedVertices(), possibleVertices() {
    for (int i = 0; i < numVertices; ++i) {
        possibleVertices.insert(i);
    }
}

State::State(std::vector<bool> isSelectedInit)
    : isSelected(isSelectedInit), selectedVertices(), possibleVertices() {
    for (int i = 0; i < static_cast<int>(isSelected.size()); ++i) {
        if (isSelected[i]) {
            selectedVertices.insert(i);
        } else {
            possibleVertices.insert(i);
        }
    }
}

State::~State() {
    // No dynamic memory to free
}

bool State::selectActionEdge(const Graph& graph) {
    std::vector<std::pair<int, int>> validEdges;
    std::vector<int> degree(graph.numVertices, 0);
    for (int u = 0; u < graph.numVertices; ++u) {
        if (possibleVertices.count(u)) {
            for (int v : graph.adjacencyList[u]) {
                if (possibleVertices.count(v) && u < v) {
                    validEdges.emplace_back(u, v);
                    degree[u]++;
                    degree[v]++;
                }
            }
        }
    }
    if (!validEdges.empty()) {
        actionEdge = validEdges[0];
        for (const auto& edge : validEdges) {
            int u = edge.first;
            int v = edge.second;
            if (std::abs(degree[u] - degree[v]) > std::abs(degree[actionEdge.first] - degree[actionEdge.second])) {
                actionEdge = edge;
            }
        }
        return true;
    } else {
        actionEdge = {-1, -1}; // No valid edge
        return false;
    }
}

void State::include(int vertex) {
    if (vertex >= 0 && vertex < static_cast<int>(isSelected.size())) {
        assert(possibleVertices.count(vertex) && "Error: including a vertex that is not in the possible set");
        isSelected[vertex] = true;
        selectedVertices.insert(vertex);
        possibleVertices.erase(vertex);
    }
}

void State::exclude(int vertex) {
    if (vertex >= 0 && vertex < static_cast<int>(isSelected.size())) {
        assert(possibleVertices.count(vertex) && "Error: excluding a vertex that is not in the possible set");
        possibleVertices.erase(vertex);
    }
}

double State::evaluate() {
    assert(!selectedVertices.empty() && "Error: evaluating state with no selected vertices");
    return 1/static_cast<double>(selectedVertices.size());
}

namespace treePolicy {
    Node* uctSampling(Node* node, double explorationParam) {
        const std::vector<Node*>& children = node->children;
        assert(!children.empty());

        // Compute state values
        std::vector<double> stateValues;
        stateValues.reserve(children.size());

        int totalVisits = node->visits;
        assert(totalVisits > 0 && "Total visits must be positive for UCT sampling");

        std::vector<double> weights;
        weights.reserve(children.size());

        for (const Node* child : children) {
            double uctValue = child->value +
                              2.0 * explorationParam *
                              std::sqrt(2.0 * std::log(totalVisits) / (0.000001 + static_cast<double>(child->visits))
                              );
            weights.push_back(std::max(0.0, uctValue));
        }

        double sum = 0.0;
        for (double& w : weights) {
            sum += w;
            w = sum;
        }

        double r = tl_uniform01(tl_engine) * sum;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            if (r <= weights[i]) return children[i];
        }
        // Numerical edge case: return last
        assert(false);
        // return children.back();
    }

    Node* epsilonGreedy(Node* node, double explorationParam) {
        const std::vector<Node*>& children = node->children;
        assert(!children.empty());

        // Compute state values
        std::vector<double> stateValues;
        stateValues.reserve(children.size());

        int totalVisits = node->visits;
        assert(totalVisits > 0 && "Total visits must be positive for UCT sampling");

        for (const Node* child : children) {
            // stateValues.push_back(child->maxValue);
            double uctValue = child->value +
                              2.0 * explorationParam *
                              std::sqrt(2.0 * std::log(totalVisits) / (0.000001 + static_cast<double>(child->visits))
                              );
            stateValues.push_back(uctValue);
        }

        // Epsilon-greedy selection
        double epsilon = 0.1; // Fixed epsilon value
        double r = tl_uniform01(tl_engine);
        if (r < epsilon) {
            // Explore: random choice
            std::size_t idx = static_cast<std::size_t>(tl_uniform01(tl_engine) * children.size());
            if (idx >= children.size()) idx = children.size() - 1;
            return children[idx];
        } else {
            // Exploit: best state value
            std::size_t bestIdx = 0;
            double bestValue = stateValues[0];
            for (std::size_t i = 1; i < stateValues.size(); ++i) {
                if (stateValues[i] > bestValue) {
                    bestValue = stateValues[i];
                    bestIdx = i;
                }
            }
            return children[bestIdx];
        }
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