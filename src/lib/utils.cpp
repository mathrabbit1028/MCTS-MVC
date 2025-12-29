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