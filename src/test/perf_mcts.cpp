#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <fstream>
#include <sstream>
#include <cmath>
#include <filesystem>
#include "../lib/mcts.hpp"
#include "../lib/utils.hpp"

struct InstancePath {
    std::string input;
    std::string output;
};

static std::vector<InstancePath> load_manifest(const std::string& path) {
    std::ifstream in(path);
    if (!in) { std::cerr << "Failed to open manifest: " << path << std::endl; std::exit(1); }
    std::ostringstream ss; ss << in.rdbuf();
    std::string s = ss.str();
    std::regex reItem("\\{\\s*\\\"input\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"\\s*,\\s*\\\"output\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"\\s*\\}");
    std::vector<InstancePath> items;
    for (std::sregex_iterator it(s.begin(), s.end(), reItem), end; it != end; ++it) {
        items.push_back({ (*it)[1], (*it)[2] });
    }
    return items;
}

static int load_output_size(const std::string& path) {
    std::ifstream in(path);
    if (!in) return -1;
    std::ostringstream ss; ss << in.rdbuf();
    std::string s = ss.str();
    std::smatch m;
    std::regex reSize("\\\"size\\\"\\s*:\\s*(\\d+)");
    if (std::regex_search(s, m, reSize) && m.size() >= 2) {
        return std::stoi(m[1]);
    }
    return -1;
}

static int count_edges(const Graph& g) {
    int c = 0;
    for (int u = 0; u < g.numVertices; ++u) c += (int)g.adjacencyList[u].size();
    return c / 2;
}

static int count_nodes_recursive(Node* node) {
    int total = 1;
    for (Node* c : node->children) total += count_nodes_recursive(c);
    return total;
}

static void run_perf(const std::vector<InstancePath>& items, int iterations, double explorationParam, std::ostream& out) {
    // CSV header for per-instance metrics
    // idx: instance index in manifest
    // n: number of vertices
    // edges: number of edges
    // root_children: number of children under root after iterations
    // total_nodes: total nodes in the MCTS tree (root + all descendants)
    // best_visits: visits of the best child (by value, tie-break by visits)
    // best_value: value of the best child
    // est_cover: estimated cover size from simulate(best)
    // truth_cover: ground-truth cover size from dataset output
    out << "idx,n,edges,root_children,total_nodes,best_visits,best_value,est_cover,truth_cover\n";

    // Track average reward per iteration across instances
    std::vector<double> avgRewards(iterations, 0.0);
    std::vector<int> counts(iterations, 0);

    for (size_t i = 0; i < items.size(); ++i) {
        Graph g = loadGraphFromJson(items[i].input);
        MCTS mcts(g, explorationParam);

        // Run and accumulate reward after each iteration
        for (int it = 0; it < iterations; ++it) mcts.run();

        // Final tree stats
        int rootChildren = (int)mcts.root->children.size();
        int totalNodes = count_nodes_recursive(mcts.root);
        Node* best = nullptr;
        for (Node* c : mcts.root->children) {
            if (!best || c->value > best->value || (c->value == best->value && c->visits > best->visits)) {
                best = c;
            }
        }
        double bestVal = best ? best->value : 0.0;
        int bestVisits = best ? best->visits : 0;
        double reward = mcts.simulate(best ? best : mcts.root).evaluate();
        int estCover = reward > 0.0 ? (int)std::round(1.0 / reward) : 0;
        int truth = load_output_size(items[i].output);
        out << i << "," << g.numVertices << "," << count_edges(g) << "," << rootChildren
            << "," << totalNodes << "," << bestVisits << "," << bestVal << "," << estCover << "," << truth << "\n";
    }
}

int main(int argc, char** argv) {
    // Defaults
    std::string manifest = "data/exact/manifest.json"; // default to exact
    int iterations = 10; // default iterations
    double explorationParam = 0.0; // default exploration param
    std::string outDir = "./result"; // default results folder

    // Simple CLI parsing
    // --manifest <path> --iterations <n> --exploration <c> --out-dir <path>
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--manifest" && i + 1 < argc) {
            manifest = argv[++i];
        } else if (arg == "--iterations" && i + 1 < argc) {
            iterations = std::stoi(argv[++i]);
        } else if (arg == "--exploration" && i + 1 < argc) {
            explorationParam = std::stod(argv[++i]);
        } else if (arg == "--out-dir" && i + 1 < argc) {
            outDir = argv[++i];
        }
    }

    // Load items
    auto items = load_manifest(manifest);
    if (items.empty()) {
        std::cerr << "No instances found in manifest: " << manifest << std::endl;
        return 1;
    }

    // Ensure output directory exists
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    // Derive dataset tag from manifest path (e.g., "exact" or "large")
    std::string tag = "dataset";
    if (manifest.find("exact") != std::string::npos) tag = "exact";
    else if (manifest.find("large") != std::string::npos) tag = "large";

    // Compose output filename
    std::ostringstream fname;
    fname << outDir << "/mvc_" << tag << "_iters-" << iterations << "_exp-" << explorationParam << ".csv";
    std::string outPath = fname.str();

    std::ofstream out(outPath);
    if (!out) {
        std::cerr << "Failed to open output file: " << outPath << std::endl;
        return 1;
    }

    // Info
    std::cout << "Writing results to: " << outPath << std::endl;

    // Run perf and write CSV
    run_perf(items, iterations, explorationParam, out);
    return 0;
}
