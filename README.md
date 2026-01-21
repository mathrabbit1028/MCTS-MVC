> Note: This branch uses **vertex-based actions** and a **compressed-heuristic rollout**.

# MCTS-MVC: Monte Carlo Tree Search for Minimum Vertex Cover

This repository explores an approximation strategy for the Minimum Vertex Cover (MVC) problem using Monte Carlo Tree Search (MCTS).

The project includes:
- A small C++ library implementing core structures (`Graph`, `State`, `Node`) and the MCTS loop (`run`, `select`, `expand`, `simulate`, `backpropagate`).
- A simple UCT-based child sampling routine.
- A dataset generator that produces MVC graph instances and solutions.
- A lightweight test program.

## Project structure

- `src/lib/`
  - `utils.hpp` / `utils.cpp`
    - `Graph`: adjacency-list graph structure
      - `Graph(int numVertices)`: construct graph with `numVertices` vertices
      - `void addEdge(int u, int v)`: add an undirected edge
      - `int numVertices`: number of vertices
      - `std::vector<std::vector<int>> adjacencyList`: adjacency lists
    - `State`: holds a partial/completed vertex cover
      - `State()`, `State(int numVertices)`: construct an empty state sized to the graph
      - `std::vector<bool> isSelected`: flags for vertex selection
      - `std::unordered_set<int> selectedVertices`: selected vertex indices
      - `std::unordered_set<int> possibleVertices`: candidate vertices still available for actions
      - `void include(int vertex)`: include/select a vertex into the cover
      - `void exclude(int vertex)`: exclude a vertex from consideration
      - `int randomVertex()`: pick a random currently-unselected vertex (uniform)
      - `int getActionCounts()`: number of remaining actions (unselected vertices)
      - `double evaluate()`: evaluation score (inverse cover size, i.e., `1.0 / |selected|`)
    - `namespace UCT`
      - `Node* sampling(std::vector<Node*>& children, double explorationParam = 0.0)`: pick a child according to a UCT-inspired weight; returns a child pointer
  - `node.hpp` / `node.cpp`
    - `Node`: tree node for MCTS
      - `State state`: selected vertices at this node
      - `Node* parent`: parent pointer
      - `std::vector<Node*> children`: child nodes
      - `int visits`: visit count
      - `double value`: accumulated value (online average of rewards)
      - `void addChild(Node* child)`: attach a child (and set its parent)
      - `void addExperience(double reward)`: update visits and value
      - `bool full()`: indicates whether all actions from this state have been expanded (based on `getActionCounts()` vs `children.size()`)
  - `mcts.hpp` / `mcts.cpp`
    - `class MCTS`
      - `MCTS(Graph& graph, double explorationParam = 0.0)`: initialize with a graph and optional UCT exploration parameter; root state is sized accordingly
      - `void run()`: one step of MCTS (currently selects a leaf)
      - `Node* select(Node* node)`: descend until reaching a non-full node, using `UCT::sampling`
      - `Node* expand(Node* node)`: create a new child by copying parent state and selecting one random unselected vertex
      - `double simulate(Node* node)`: rough heuristic rollout — completes a vertex cover from the node’s current selection; reward is `1.0 / final_cover_size`
      - `void backpropagate(Node* node, double reward)`: propagate reward up to root

- `src/test/`
  - `test_mcts.cpp`: basic smoke test
    - Loads the first exact dataset instance (`data/exact/inputs/graph_0000.json`)
    - Creates `MCTS`, checks root state sizing
    - Calls `expand`, verifies child linkage and selection
    - Calls `simulate`, expects non-negative reward
    - Calls `backpropagate`, checks visit increment
    - Adds extra children and checks `UCT::sampling` returns one of them

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
Expected output includes and "All tests passed.".

### Performance harness
The performance harness `src/test/perf_mcts.cpp` reads a manifest and prints per-instance CSV metrics.

- CLI options (all optional):
  - `--manifest <path>`: dataset manifest file. Default `data/exact/manifest.json`.
  - `--iterations <n>`: number of MCTS iterations. Default `10`.
  - `--exploration <c>`: UCT exploration parameter. Default `0`.
  - `--out-dir <path>`: output folder for CSV. Default `./result` (auto-created).

- CSV file naming: `mvc_<tag>_iters-<iterations>_exp-<exploration>.csv`
  - `<tag>` is inferred from the manifest path: if it contains `exact` → `exact`, if it contains `large` → `large`.
  - If manifest path does not include `exact`/`large`, treated as `dataset`.

- Examples:
  - Default run: `./src/test/perf_mcts_bin` → writes `./result/mvc_exact_iters-10_exp-0.csv`
  - Large dataset: `./src/test/perf_mcts_bin --manifest data/large/manifest.json --iterations 50 --exploration 0.25 --out-dir ./result-large`

<!-- ## Coarsening Ideas
1. 두개씩 묶기
  + 홀수개 버킷에서 남는걸 그냥 남겨놨더니 16개 이하에 도달 X
  + supernode 다 썼더니 거의 MVC를 얻긴 하는데 너무 많은 노드 사용
  + supernode 중에 하나만 골랐더니 MVC보다 한참 작음 + 그리디보다 성능이 안좋음
2. MVC에 포함될 확률이 비슷한 노드끼리 압축하기
3. LP를 돌려서 0.5인것만 남기는 방식으로 압축하기 -->