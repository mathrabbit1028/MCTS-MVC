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

static int max_depth_recursive(Node* node) {
    if (!node) return 0;
    int best = 1;
    for (Node* c : node->children) {
        best = std::max(best, 1 + max_depth_recursive(c));
    }
    return best;
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
    out << "idx,n,edges,root_children,total_nodes,max_depth,est_cover,truth_cover\n";

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
        int maxDepth = max_depth_recursive(mcts.root);
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
            << "," << totalNodes << "," << maxDepth << "," << estCover << "," << truth << "\n";
        out << std::flush;
    }
    // Finish progress line
    std::cout << "\n";
    return cumulativeSeconds;
}

// ======= heuristic ======== //
/* void init_estimate_policy() {
    treePolicy::setEstimatePolicy([](const State& state, const Graph& graph, bool include) {
        double prob = 0.0; // probability of including state->actionVertex

        int v = state.actionVertex;
        if (v != -1) {
            double deg_score = 0.0;
            double neighbor_score = 0.0;
            double redundancy = 0.0;
            
            std::vector<int> neighbors;
            // 1) degree 및 이웃의 역차수(1/deg(u)) 계산
            for (int u : graph.adjacencyList[v]) {
                if (state.possibleVertices.count(u)) {
                    neighbors.push_back(u);
                    
                    // 이웃 u의 남은 서브그래프에서의 degree 계산
                    int deg_u = 0;
                    for (int w : graph.adjacencyList[u]) {
                        if (state.possibleVertices.count(w)) deg_u++;
                    }
                    
                    if (deg_u > 0) {
                        neighbor_score += 1.0 / static_cast<double>(deg_u);
                    }
                }
            }
            deg_score = static_cast<double>(neighbors.size());
            
            // 2) 이웃 간의 겹침 (Redundancy / Triangle) 계산
            for (size_t i = 0; i < neighbors.size(); ++i) {
                for (size_t j = i + 1; j < neighbors.size(); ++j) {
                    int n1 = neighbors[i];
                    int n2 = neighbors[j];
                    
                    // n1과 n2가 연결되어 있는지 확인
                    for (int w : graph.adjacencyList[n1]) {
                        if (w == n2) {
                            redundancy += 1.0;
                            break;
                        }
                    }
                }
            }
            
            // 3) 최종 휴리스틱 스코어
            double score = deg_score + 0.7 * neighbor_score - 0.5 * redundancy;
            
            // 4) 확률 정규화 (선형 비례 방법 B 활용)
            // actionVertex는 항상 탐색 가능한 최상위 후보이므로,
            // 자체적으로 가질 수 있는 최대 점수(모든 이웃이 degree=1인 leaf_like 상상) 대비 퍼센티지로 정규화
            double max_expected_score = deg_score + 0.7 * deg_score; 
            if (max_expected_score > 0) {
                prob = score / max_expected_score;
            }
            
            // 극단적 값을 피하기 위해 안전한 확률 구간으로 클리핑 (0.01 ~ 0.99)
            prob = std::max(0.01, std::min(0.99, prob)); 
        }

        return include ? prob : 1 - prob;
    });
} */

// ======= perturbation-LP estimator ======== //
void init_estimate_policy() {
    treePolicy::setEstimatePolicy([](const State& state, const Graph& graph, bool include) {
        // Perturbation-LP estimator:
        // Repeatedly solve a perturbed LP relaxation of MVC on the current active core,
        // then estimate probability by frequency of x_v > 0.5.
        double prob = 0.5;

        const int v = state.actionVertex;
        if (v >= 0 && v < graph.numVertices && state.possibleVertices.count(v)) {
            // Build active vertex index mapping for current core
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
                // Build edge list in active core (u < w) using local indices
                std::vector<std::pair<int, int>> activeEdges;
                for (int uGlobal : activeVerts) {
                    int u = idxOf[uGlobal];
                    for (int wGlobal : graph.adjacencyList[uGlobal]) {
                        int w = (wGlobal >= 0 && wGlobal < graph.numVertices) ? idxOf[wGlobal] : -1;
                        if (w >= 0 && u < w) activeEdges.push_back({u, w});
                    }
                }

                if (!activeEdges.empty()) {
                    // Solve perturbed LP approximately via projected gradient on
                    // min sum_i c_i x_i + mu * sum_(u,w) [max(0, 1 - x_u - x_w)]^2, 0<=x_i<=1
                    auto solvePerturbedLP = [&](int trial) {
                        std::vector<double> c(n, 1.0);
                        constexpr double delta = 0.20;
                        for (int i = 0; i < n; ++i) {
                            int vg = activeVerts[i];
                            double phase = static_cast<double>((vg + 1) * 131 + (trial + 1) * 977);
                            c[i] = 1.0 + delta * std::sin(phase);
                        }

                        std::vector<double> x(n, 0.5);
                        constexpr int kIters = 140;
                        constexpr double lr = 0.03;
                        constexpr double mu = 8.0;

                        for (int it = 0; it < kIters; ++it) {
                            std::vector<double> grad = c;
                            for (const auto& e : activeEdges) {
                                int a = e.first;
                                int b = e.second;
                                double viol = 1.0 - x[a] - x[b];
                                if (viol > 0.0) {
                                    double g = -2.0 * mu * viol;
                                    grad[a] += g;
                                    grad[b] += g;
                                }
                            }
                            for (int i = 0; i < n; ++i) {
                                x[i] -= lr * grad[i];
                                if (x[i] < 0.0) x[i] = 0.0;
                                if (x[i] > 1.0) x[i] = 1.0;
                            }
                        }

                        // quick feasibility repair for tiny residual violations
                        for (const auto& e : activeEdges) {
                            int a = e.first;
                            int b = e.second;
                            double deficit = 1.0 - x[a] - x[b];
                            if (deficit > 0.0) {
                                double add = 0.5 * deficit;
                                x[a] = std::min(1.0, x[a] + add);
                                x[b] = std::min(1.0, x[b] + add);
                            }
                        }

                        return x;
                    };

                    constexpr int kTrials = 12;
                    int greaterHalf = 0;
                    for (int t = 0; t < kTrials; ++t) {
                        std::vector<double> x = solvePerturbedLP(t);
                        if (x[actionIdx] > 0.5) ++greaterHalf;
                    }

                    prob = static_cast<double>(greaterHalf) / static_cast<double>(kTrials);
                    prob = std::max(0.05, std::min(0.95, prob));
                }
            }
        }

        return include ? prob : 1 - prob;
    });
}

// ====== randomized rounding estimator ======== //
/* void init_estimate_policy() {
    treePolicy::setEstimatePolicy([](const State& state, const Graph& graph, bool include) {
        // Strategy #3: randomized rounding
        // 1) get fractional x in [0,1]
        // 2) sample each vertex with probability 1/2
        // 3) repair uncovered edges by adding one endpoint
        // 4) prob = frequency that actionVertex is selected
        double prob = 0.5;

        const int v = state.actionVertex;
        if (v >= 0 && v < graph.numVertices && state.possibleVertices.count(v)) {
            // Build active vertex index mapping for current core
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
                // Build edge list in active core (u < w) using local indices
                std::vector<std::pair<int, int>> activeEdges;
                for (int uGlobal : activeVerts) {
                    int u = idxOf[uGlobal];
                    for (int wGlobal : graph.adjacencyList[uGlobal]) {
                        int w = (wGlobal >= 0 && wGlobal < graph.numVertices) ? idxOf[wGlobal] : -1;
                        if (w >= 0 && u < w) activeEdges.push_back({u, w});
                    }
                }

                if (!activeEdges.empty()) {
                    // Build a fractional LP-like vector x in [0,1] using relaxed optimization
                    std::vector<double> x(n, 0.5);

                    // Randomized rounding + repair
                    constexpr int kTrials = 200;
                    int selectedCount = 0;
                    std::mt19937 rng(20260401 + v * 911);
                    std::uniform_real_distribution<double> unif01(0.0, 1.0);

                    for (int t = 0; t < kTrials; ++t) {
                        std::vector<bool> chosen(n, false);

                        // Round each vertex with probability x_i
                        for (int i = 0; i < n; ++i) {
                            chosen[i] = (unif01(rng) < x[i]);
                        }

                        // Repair uncovered edges: pick one endpoint (biased by larger x)
                        for (const auto& e : activeEdges) {
                            int a = e.first;
                            int b = e.second;
                            if (!chosen[a] && !chosen[b]) {
                                double denom = x[a] + x[b];
                                double pa = (denom > 1e-12) ? (x[a] / denom) : 0.5;
                                if (unif01(rng) < pa) chosen[a] = true;
                                else chosen[b] = true;
                            }
                        }

                        if (chosen[actionIdx]) ++selectedCount;
                    }

                    prob = static_cast<double>(selectedCount) / static_cast<double>(kTrials);
                    prob = std::max(0.01, std::min(0.99, prob));
                }
            }
        }

        return include ? prob : 1 - prob;
    });
} */

// ====== MCMC Gibbs sampling estimator ====== //
/* void init_estimate_policy() {
	treePolicy::setEstimatePolicy([](const State& state, const Graph& graph, bool include) {
        // Strategy #6 (Gibbs / MCMC):
        // Sample feasible vertex covers S from P(S) ∝ exp(-beta * |S|)
        // and estimate p_v = Pr[v in S].
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
                // Build local adjacency on active core
                std::vector<std::vector<int>> localAdj(n);
                int edgeCount = 0;
                for (int ug : activeVerts) {
                    int u = idxOf[ug];
                    for (int vg : graph.adjacencyList[ug]) {
                        int w = (vg >= 0 && vg < graph.numVertices) ? idxOf[vg] : -1;
                        if (w >= 0) {
                            localAdj[u].push_back(w);
                            if (u < w) ++edgeCount;
                        }
                    }
                }

                if (edgeCount == 0) {
                    prob = 0.01; // no edge pressure => typically excluded
                } else {
                    // Start from a feasible cover: all active vertices selected
                    std::vector<bool> selected(n, true);

                    auto canRemove = [&](int i) {
                        // Removing i keeps feasibility iff every incident edge (i, j)
                        // is still covered by j being selected.
                        for (int j : localAdj[i]) {
                            if (!selected[j]) return false;
                        }
                        return true;
                    };

                    constexpr double beta = 1.6; // larger => more mass on smaller covers
                    const double addAcceptProb = std::exp(-beta); // 0->1 acceptance
                    constexpr int burnIn = 400;
                    constexpr int totalSteps = 2200;
                    constexpr int sampleStride = 6;

                    std::mt19937 rng(static_cast<unsigned int>(
                        20260401u + static_cast<unsigned int>(v) * 1009u + static_cast<unsigned int>(n) * 9176u));
                    std::uniform_int_distribution<int> pickVertex(0, n - 1);
                    std::uniform_real_distribution<double> unif01(0.0, 1.0);

                    int hit = 0;
                    int sampled = 0;
                    for (int step = 0; step < totalSteps; ++step) {
                        int i = pickVertex(rng);
                        if (selected[i]) {
                            if (canRemove(i)) selected[i] = false; // always accept energy-decreasing feasible removal
                        } else {
                            if (unif01(rng) < addAcceptProb) selected[i] = true;
                        }

                        if (step >= burnIn && ((step - burnIn) % sampleStride == 0)) {
                            ++sampled;
                            if (selected[actionIdx]) ++hit;
                        }
                    }

                    if (sampled > 0) {
                        prob = static_cast<double>(hit) / static_cast<double>(sampled);
                    }
                    prob = std::max(0.01, std::min(0.99, prob));
                }
            }
        }

        return include ? prob : 1 - prob;
    });
} */


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
    init_estimate_policy();
    double runSecs = run_perf(items, iterations, explorationParam, out);
    std::cout << std::fixed << std::setprecision(3)
              << "Total time | manifest=" << manifestSecs << "s"
              << " run=" << runSecs << "s"
              << " | overall=" << (manifestSecs + runSecs) << "s\n";
    return 0;
}