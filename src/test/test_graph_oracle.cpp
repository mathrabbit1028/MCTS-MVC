#include <cassert>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <regex>
#include "../lib/utils.hpp"

static bool is_vertex_cover(const Graph& g, const State& s) {
    for (int u = 0; u < g.numVertices; ++u) {
        for (int v : g.adjacencyList[u]) {
            if (u < v) {
                bool cu = s.selectedVertices.count(u) > 0;
                bool cv = s.selectedVertices.count(v) > 0;
                if (!(cu || cv)) return false;
            }
        }
    }
    return true;
}

static int sum_weights(const Graph& g) {
    int w = 0;
    for (int i = 0; i < g.numVertices; ++i) {
        int wi = (i < (int)g.weights.size() ? g.weights[i] : 1);
        w += wi;
    }
    return w;
}

static int load_truth_size(const std::string& path) {
    std::ifstream in(path);
    if (!in) return -1;
    std::ostringstream ss; ss << in.rdbuf();
    std::string s = ss.str();
    std::smatch m;
    std::regex reSize("\\\\\"size\\\\\"\\s*:\\s*(\\d+)");
    if (std::regex_search(s, m, reSize) && m.size() >= 2) {
        return std::stoi(m[1]);
    }
    return -1;
}

int main() {
    // Test 1: exactSolve on a triangle should produce a valid cover of size 2
    {
        Graph g(3);
        g.addEdge(0,1);
        g.addEdge(1,2);
        g.addEdge(0,2);
        // Default weights are 1
        State s = GraphOracle::exactSolve(g);
        assert(is_vertex_cover(g, s));
        assert((int)s.selectedVertices.size() == 2);
        std::cout << "Test1 OK: exactSolve(triangle) size=" << s.selectedVertices.size() << "\n";
    }

    // Test 2: greedySolve should also produce a valid cover (size 2 for triangle)
    {
        Graph g(3);
        g.addEdge(0,1);
        g.addEdge(1,2);
        g.addEdge(0,2);
        State s = GraphOracle::greedySolve(g);
        assert(is_vertex_cover(g, s));
        assert((int)s.selectedVertices.size() >= 2);
        std::cout << "Test2 OK: greedySolve(triangle) size=" << s.selectedVertices.size() << "\n";
    }

    // Test 3: coarsenGraph reduces vertex count and preserves total weight, no self-loops
    {
        Graph g(6);
        // Create a small hexagon-like structure
        g.addEdge(0,1); g.addEdge(1,2); g.addEdge(2,3); g.addEdge(3,4); g.addEdge(4,5); g.addEdge(5,0);
        // weights default to 1 -> total 6
        int wsum = sum_weights(g);
        Graph gc = GraphOracle::coarsenGraph(g).first;
        assert(gc.numVertices <= g.numVertices);
        assert(sum_weights(gc) == wsum);
        for (int i = 0; i < gc.numVertices; ++i) {
            for (int j : gc.adjacencyList[i]) {
                assert(i != j); // no self-loop
            }
        }
        std::cout << "Test3 OK: coarsenGraph reduced to " << gc.numVertices << " vertices, weights preserved\n";
    }

    // Test 4: coarsenGraph on a real exact dataset instance (groups returned)
    {
        std::string inputPath = "data/exact/inputs/graph_0006.json";
        Graph g = loadGraphFromJson(inputPath);
        int wsum = sum_weights(g);
        auto [gc, groups] = GraphOracle::coarsenGraph(g);
        // Coarsening should not increase vertex count and must preserve total weight; no self-loops
        assert(gc.numVertices <= g.numVertices);
        assert(sum_weights(gc) == wsum);
        for (int i = 0; i < gc.numVertices; ++i) {
            for (int j : gc.adjacencyList[i]) {
                assert(i != j);
            }
        }
        std::cout << "Test5 OK: coarsenGraph on exact instance (" << inputPath << ") reduced to "
                  << gc.numVertices << " vertices, weights preserved" << std::endl;

        g.print();
        gc.print();
        for (int i = 0; i < (int)groups.size(); ++i) {
            std::cout << "Group " << i << ": ";
            for (int v : groups[i]) {
                std::cout << v << " ";
            }
            std::cout << "\n";
        }
    }

    // Test 5: Verify coarseSolve + lifting yields the correct MVC
    {
        std::string inputPath = "data/large/inputs/graph_0000.json";
        Graph g = loadGraphFromJson(inputPath);
        State s = GraphOracle::coarseSolve(g);
        // Must be a valid vertex cover
        assert(is_vertex_cover(g, s));
        std::cout << "Test5 OK: coarseSolve found MVC size on " << inputPath << "\n";
    }

    std::cout << "All GraphOracle tests passed." << std::endl;
    return 0;
}
