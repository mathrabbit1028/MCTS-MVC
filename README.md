> Note: This branch uses edge-based actions and a greedy-heuristic rollout.

# MCTS-MVC: Monte Carlo Tree Search for Minimum Vertex Cover

This repository explores an approximation strategy for the Minimum Vertex Cover (MVC) problem using Monte Carlo Tree Search (MCTS).

The project includes:
- A small C++ library implementing core structures (`Graph`, `State`, `Node`) and the MCTS loop (`run`, `select`, `expand`, `simulate`, `backpropagate`).
- UCT and epsilon-greedy tree policy implementations.
- A dataset generator that produces MVC graph instances and solutions.
- A lightweight test program and performance harness.

## Project structure

- `src/lib/`
  - `utils.hpp` / `utils.cpp`
    - `Graph`: adjacency-list graph structure
      - `Graph(int numVertices)`: construct graph with `numVertices` vertices
      - `void addEdge(int u, int v)`: add an undirected edge
      - `int numVertices`: number of vertices
      - `std::vector<std::vector<int>> adjacencyList`: adjacency lists
      - `Graph loadGraphFromJson(const std::string& path)`: load a graph from `{"num_vertices": N, "edges": [[u,v], ...]}` JSON
    - `State`: holds a partial/completed vertex cover
      - `State()`, `State(int numVertices)`, `State(std::vector<bool> isSelectedInit)`: construct state
      - `std::vector<bool> isSelected`: flags for vertex selection
      - `std::unordered_set<int> selectedVertices`: selected vertex indices
      - `std::unordered_set<int> possibleVertices`: candidate vertices still available for actions
      - `std::pair<int,int> actionEdge`: current edge action (endpoints); `(-1,-1)` indicates no valid action
      - `bool selectActionEdge(const Graph& graph)`: choose a random uncovered/valid edge among `possibleVertices` and set `actionEdge`; returns true if an edge was found, false otherwise
      - `void include(int vertex)`: include/select a vertex into the cover
      - `void exclude(int vertex)`: exclude a vertex from consideration
      - `double evaluate()`: evaluation score (inverse cover size, i.e., `1.0 / |selected|`)
    - `namespace treePolicy`
      - `Node* uctSampling(Node* node, double explorationParam = 0.0)`: pick a child using UCT formula; returns a child pointer
      - `Node* epsilonGreedy(Node* node, double explorationParam = 0.0)`: epsilon-greedy child selection based on `maxValue`
  - `node.hpp` / `node.cpp`
    - `Node`: tree node for MCTS
      - `State state`: selected vertices at this node
      - `Node* parent`: parent pointer
      - `std::vector<Node*> children`: child nodes
      - `int visits`: visit count
      - `double value`: accumulated value (online average of rewards)
      - `double maxValue`: maximum reward observed in this node's subtree (initialized to 0)
      - `int expandable`: number of remaining expandable actions (initialized to 2 for binary branching)
      - `void addChild(Node* child)`: attach a child (and set its parent)
      - `void addExperience(double reward)`: update visits, value (running average), and maxValue (track maximum)
      - `bool full()`: returns true if the node has 2 children (fully expanded for binary edge-based branching)
  - `mcts.hpp` / `mcts.cpp`
    - `class MCTS`
      - `MCTS(Graph& graph, double explorationParam = 0.0)`: initialize with a graph and optional UCT exploration parameter; applies initial kernelization to root
      - `Graph graph`: the problem graph
      - `Node* root`: root of the search tree
      - `double explorationParam`: UCT exploration parameter
      - `int answer`: current best solution size found (initialized to `numVertices`)
      - `void run()`: one MCTS iteration (`select → expand → simulate → backpropagate`)
      - `bool kernelization(Node* node)`: apply reduction rules:
        - Rule 1: Exclude degree-0 vertices (no edges to cover)
        - Rule 2: Include the neighbor of degree-1 vertices
        - Rule 3: Include vertices with degree > current best `answer`
        - Returns true if any rule was applied
      - `State getSolution()`: traverse the tree following best `maxValue` chain (highest reward) and return a completed cover via `simulate`
      - `void setExplorationParam(double param)`: update UCT exploration parameter
      - `void expandableUpdate(Node* node)`: propagate `expandable=0` status upward to parents when a node becomes terminal
      - `Node* select(Node* node)`: descend until reaching a non-full node, using `treePolicy::uctSampling` (or `epsilonGreedy`)
      - `Node* expand(Node* node)`: **one-child-at-a-time edge branching** — creates one child by including `actionEdge.first`, then swaps the edge endpoints for the next expand call; applies kernelization after each inclusion
      - `State simulate(Node* node)`: greedy rollout — completes a vertex cover from the node's state using max-degree heuristic; returns completed state
      - `void backpropagate(Node* node, double reward)`: propagate reward up to root, updating visits, value (average), and maxValue (maximum)

- `src/test/`
  - `test_mcts.cpp`: basic smoke test
    - Loads the first exact dataset instance (`data/exact/inputs/graph_0000.json`)
    - Runs `MCTS::run()` a few iterations and checks the tree grows / visits update
    - Ensures root has at least 4 children via `expand`
    - Sanity-checks `uctSampling` returns one of the root children

- `data/`
  - `generate_mvc_data.py`: dataset generator (Python)
  - `exact/`: exact dataset (moved from `mvc/`, 25 instances with `N ≤ 22`)
    - `inputs/graph_XXXX.json`: graph instances
    - `outputs/graph_XXXX.json`: exact vertex covers
    - `manifest.json`: input/output mapping
  - `large/`: large dataset (heuristic-estimated covers)
    - `inputs/graph_XXXX.json`: larger graph instances
    - `outputs/graph_XXXX.json`: heuristic estimates (min-of-greedy) with `chosen_heuristic` and `heuristics` breakdown
    - `manifest.json`: input/output mapping

## Dataset: Minimum Vertex Cover

Input format (`inputs/graph_XXXX.json`):
```
{
  "num_vertices": 12,
  "edges": [[0,1], [1,2], [2,3], ...]
}
```
- `num_vertices` — number of vertices (0-indexed)
- `edges` — undirected edges (each pair `[u,v]` with `u<v`)

Output format — exact dataset (`exact/outputs/graph_XXXX.json`):
```
{
  "solution_type": "exact",
  "vertex_cover": [1,4,7],
  "size": 3
}
```
- `solution_type` — `exact` for small graphs (up to a threshold)
- `vertex_cover` — vertex indices forming a cover
- `size` — cover size

Output format — large dataset (`large/outputs/graph_XXXX.json`):
```
{
  "solution_type": "heuristic_min_of_greedy",
  "vertex_cover": [ ... ],
  "size": 42,
  "chosen_heuristic": "max_degree",
  "heuristics": [
    { "name": "max_degree", "size": 42 },
    { "name": "two_approx_edge_pair", "size": 50 },
    { "name": "random_edge_high_degree_endpoint", "size": 45 },
    { "name": "min_degree", "size": 60 }
  ]
}
```
- `solution_type` — best among several greedy variants
- `chosen_heuristic` — heuristic used for the final cover
- `heuristics` — sizes from all heuristics for transparency

## Datasets

### Exact dataset (N ≤ 22)
- The `data/exact` folder contains 25 instances where `N ≤ 22`, all solved exactly.
- To regenerate:
```
python3 data/generate_mvc_data.py --count 25 --min-n 8 --max-n 22 --edge-prob 0.2 --exact-limit 22 --dataset-type exact --out-dir data/exact
```

### Large dataset (heuristic min-of-greedy)
- The `data/large` folder contains a separate set of larger graphs with cover size estimated by the minimum among several greedy heuristics.
- To generate (example parameters):
```
python3 data/generate_mvc_data.py --count 50 --min-n 50 --max-n 200 --edge-prob 0.2 --dataset-type large --out-dir data/large
```

Generator parameters:
- `--count` — number of instances
- `--min-n`, `--max-n` — vertex count range
- `--edge-prob` — probability of an edge between two vertices
- `--exact-limit` — max `n` for exact solving (used by `exact` and `auto` modes)
- `--dataset-type` — `exact` (force exact), `large` (multi-greedy), `auto` (exact if `n ≤ exact-limit`, else greedy)
- `--out-dir` — output directory root

## Build and run tests

From the repo root on macOS with clang:
```
clang++ -std=c++17 src/lib/utils.cpp src/lib/node.cpp src/lib/mcts.cpp src/test/test_mcts.cpp -o src/test/test_mcts_bin
src/test/test_mcts_bin
```
Expected output includes a non-zero simulate reward and "All tests passed.".

### Performance harness
The performance harness `src/test/perf_mcts.cpp` reads a manifest and prints per-instance CSV metrics.

Compilation:
```
clang++ -std=c++17 src/lib/utils.cpp src/lib/node.cpp src/lib/mcts.cpp src/test/perf_mcts.cpp -o src/test/perf_mcts_bin
```

- CLI options (all optional):
  - `--manifest <path>`: dataset manifest file. Default `data/exact/manifest.json`.
  - `--iterations <n>`: number of MCTS iterations. Default `10`.
  - `--exploration <c>`: UCT exploration parameter. Default `0`.
  - `--out-dir <path>`: output folder for CSV. Default `./result` (auto-created).

- CSV file naming: `mvc_<tag>_iters-<iterations>_exp-<exploration>.csv`
  - `<tag>` is extracted from the manifest path: for `data/<tag>/manifest.json`, the folder name `<tag>` is used (e.g., `exact`, `large`, `small`).
  - If extraction fails, defaults to `dataset`.

- Examples:
  - Default run: `./src/test/perf_mcts_bin` → writes `./result/mvc_exact_iters-10_exp-0.csv`
  - Large dataset: `./src/test/perf_mcts_bin --manifest data/large/manifest.json --iterations 50 --exploration 0.25` → writes `./result/mvc_large_iters-50_exp-0.25.csv`
  - Small dataset: `./src/test/perf_mcts_bin --manifest data/small/manifest.json --iterations 100 --exploration 0.1 --out-dir ./result-small` → writes `./result-small/mvc_small_iters-100_exp-0.1.csv`
