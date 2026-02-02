#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <fstream>
#include <sstream>
#include <cmath>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include "../lib/mcts.hpp"
#include "../lib/utils.hpp"

// Simple tqdm-like progress rendering for items and iterations
static void render_progress(std::size_t itemIndex, std::size_t totalItems,
                            int iterIndex, int totalIters) {
    auto makeBar = [](double ratio, int width) {
        if (ratio < 0.0) ratio = 0.0; if (ratio > 1.0) ratio = 1.0;
        int filled = static_cast<int>(ratio * width);
        std::string bar;
        bar.reserve(width + 2);
        bar.push_back('[');
        for (int i = 0; i < width; ++i) bar.push_back(i < filled ? '#' : '.');
        bar.push_back(']');
        return bar;
    };

    double itemRatio = totalItems > 0 ? static_cast<double>(itemIndex + 1) / static_cast<double>(totalItems) : 1.0;
    double iterRatio = totalIters > 0 ? static_cast<double>(iterIndex) / static_cast<double>(totalIters) : 1.0;
    std::string itemBar = makeBar(itemRatio, 20);
    std::string iterBar = makeBar(iterRatio, 20);

    std::cout << "\ritems " << itemBar << " " << (itemIndex + 1) << "/" << totalItems
              << "  iters " << iterBar << " " << iterIndex << "/" << totalIters
              << std::flush;
}

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

static double run_perf(const std::vector<InstancePath>& items, int iterations, double explorationParam, std::ostream& out) {
    // CSV header for per-instance metrics
    // idx: instance index in manifest
    // n: number of vertices
    // edges: number of edges
    // root_children: number of children under root after iterations
    // total_nodes: total nodes in the MCTS tree (root + all descendants)
    // est_cover: estimated cover size from simulate(best)
    // truth_cover: ground-truth cover size from dataset output
    out << "idx,n,edges,root_children,total_nodes,est_cover,truth_cover\n";

    // Track average reward per iteration across instances
    std::vector<double> avgRewards(iterations, 0.0);
    std::vector<int> counts(iterations, 0);

    double cumulativeSeconds = 0.0;

    for (size_t i = 0; i < items.size(); ++i) {
        auto tLoadStart = std::chrono::steady_clock::now();
        Graph g = loadGraphFromJson(items[i].input);
        auto tLoadEnd = std::chrono::steady_clock::now();
        double loadSecs = std::chrono::duration<double>(tLoadEnd - tLoadStart).count();

        MCTS mcts(g, explorationParam);

        // Run and accumulate reward after each iteration
        auto tIterStart = std::chrono::steady_clock::now();
        for (int it = 0; it < iterations; ++it) {
            if (mcts.root->expandable == 0) {
                // Fully expanded, no need to continue
                break;
            }
            mcts.run();
            // tqdm-like progress update for current item
            render_progress(i, items.size(), it + 1, iterations);
        }
        auto tIterEnd = std::chrono::steady_clock::now();
        double iterSecs = std::chrono::duration<double>(tIterEnd - tIterStart).count();
        // Ensure full progress shown for the item before stats
        render_progress(i, items.size(), iterations, iterations);
        std::cout << "\n"; // end progress line for timing output

        // Final tree stats
        auto tStatsStart = std::chrono::steady_clock::now();
        int rootChildren = (int)mcts.root->children.size();
        int totalNodes = count_nodes_recursive(mcts.root);
        int estCover = mcts.answer;
        int truth = load_output_size(items[i].output);
        auto tStatsEnd = std::chrono::steady_clock::now();
        double statsSecs = std::chrono::duration<double>(tStatsEnd - tStatsStart).count();

        cumulativeSeconds += loadSecs + iterSecs + statsSecs;
        double avgIterSecs = iterations > 0 ? iterSecs / (double)iterations : 0.0;

        // Print per-instance timing breakdown with cumulative seconds
        std::cout << std::fixed << std::setprecision(3)
                  << "timing | load=" << loadSecs << "s"
                  << " iter=" << iterSecs << "s (avg=" << avgIterSecs << "s)"
                  << " stats=" << statsSecs << "s"
                  << " | cum=" << cumulativeSeconds << "s\n";

        out << i << "," << g.numVertices << "," << count_edges(g) << "," << rootChildren
            << "," << totalNodes << "," << estCover << "," << truth << "\n";
        out << std::flush;
    }
    // Finish progress line
    std::cout << "\n";
    return cumulativeSeconds;
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

    // Load items (timed)
    auto tManStart = std::chrono::steady_clock::now();
    auto items = load_manifest(manifest);
    auto tManEnd = std::chrono::steady_clock::now();
    double manifestSecs = std::chrono::duration<double>(tManEnd - tManStart).count();
    if (items.empty()) {
        std::cerr << "No instances found in manifest: " << manifest << std::endl;
        return 1;
    }
    std::cout << std::fixed << std::setprecision(3)
              << "Loaded " << items.size() << " instances from manifest in " << manifestSecs << "s\n";

    // Ensure output directory exists
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    // Derive dataset tag from manifest path: extract folder name from "data/<tag>/manifest.json"
    std::string tag = "dataset";
    std::filesystem::path manifestPath(manifest);
    if (manifestPath.parent_path().filename() == "manifest.json") {
        // Handle case where manifest itself might be the filename
        manifestPath = manifestPath.parent_path();
    }
    std::string folderName = manifestPath.parent_path().filename().string();
    if (!folderName.empty() && folderName != "data") {
        tag = folderName;
    }

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

    // Run perf and write CSV (timed per instance internally)
    double runSecs = run_perf(items, iterations, explorationParam, out);
    std::cout << std::fixed << std::setprecision(3)
              << "Total time | manifest=" << manifestSecs << "s"
              << " run=" << runSecs << "s"
              << " | overall=" << (manifestSecs + runSecs) << "s\n";
    return 0;
}