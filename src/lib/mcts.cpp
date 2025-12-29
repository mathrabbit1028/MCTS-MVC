#include "mcts.hpp"

MCTS::MCTS(Graph& graph, double explorationParam)
    : root(new Node())
    , graph(graph)
    , explorationParam(explorationParam) {
    root->state = State(graph.numVertices);
}

MCTS::~MCTS() {
    delete root;
}

void MCTS::run() {
    Node* leaf = this->select(root);
    Node* child = this->expand(leaf);
    double reward = this->simulate(child).evaluate();
    this->backpropagate(child, reward);
}

bool MCTS::kernelization(Node* node) {
    // Rule 1: If there is a vertex of degree 0, remove it from the graph (no need to select it)
    for (int v = 0; v < this->graph.numVertices; ++v) {
        // Consider only vertices that are still possible to act on and not already selected
        if (node->state.possibleVertices.count(v)) {
            int degree = 0;
            for (int u : this->graph.adjacencyList[v]) {
                // Degree counts only neighbors that are also still possible and not selected
                if (node->state.possibleVertices.count(u)) {
                    degree++;
                }
            }
            if (degree == 0) {
                // Remove vertex v from the remaining graph (make it impossible to select)
                node->state.exclude(v);
                return true;
            }
        }
    }

    // Rule 2: If there is a vertex of degree 1, select its neighbor
    for (int v = 0; v < this->graph.numVertices; ++v) {
        if (node->state.possibleVertices.count(v)) {
            int degree = 0;
            int neighbor = -1;
            for (int u : this->graph.adjacencyList[v]) {
                if (node->state.possibleVertices.count(u)) {
                    degree++;
                    neighbor = u;
                }
            }
            if (degree == 1 && neighbor != -1) {
                // Select the neighbor vertex (only if it's still possible)
                if (node->state.possibleVertices.count(neighbor)) {
                    node->state.include(neighbor);
                    return true;
                }
            }
        }
    }

    // Rule 3: If there is a vertex with degree greater than k (where k is the size of the current solution), select it
    int k = getSolution().selectedVertices.size();
    for (int v = 0; v < this->graph.numVertices; ++v) {
        if (node->state.possibleVertices.count(v)) {
            int degree = 0;
            for (int u : this->graph.adjacencyList[v]) {
                if (node->state.possibleVertices.count(u)) {
                    degree++;
                }
            }
            if (degree > k) {
                // Select vertex v
                node->state.include(v);
                return true;
            }
        }
    }

    return false;
}

State MCTS::getSolution() {
    Node* node = root;
    // Traverse down while there are children; pick the best each step
    while (!node->children.empty()) {
        Node* bestChild = nullptr;
        for (Node* c : node->children) {
            if (!bestChild || c->value > bestChild->value || 
                (c->value == bestChild->value && c->visits > bestChild->visits)) {
                bestChild = c;
            }
        }
        node = bestChild;
    }
    return simulate(node);
}

Node* MCTS::select(Node* node) {
    if (node->state.getActionCounts() == 0) return node; // (todo: non-expandable handling)
    if (!node->full()) return node;
    return select(UCT::sampling(node->children, this->explorationParam));
}

Node* MCTS::expand(Node* node) {
    // If no actions remain, do not expand; treat node as the leaf
    if (node->state.getActionCounts() == 0) {
        return node;
    } // (todo: non-expandable handling)

    Node* newChild = new Node();
    newChild->state = node->state;
    int action = newChild->state.randomVertex(); // (todo: sample-without-replacement)
    newChild->state.include(action);
    while (this->kernelization(newChild));
    node->addChild(newChild);
    return newChild;
}

State MCTS::simulate(Node* node) {

    /* ============================================[for testing]============================================ */
    // Rough rollout: starting from current selection, greedily add vertices until all edges are covered
    const int n = this->graph.numVertices;
    std::vector<std::vector<int>> adj = this->graph.adjacencyList;

    // Track selection as a local copy
    std::vector<bool> sel(n, false);
    for (int i = 0; i < n; ++i) {
        sel[i] = (node->state.selectedVertices.find(i) != node->state.selectedVertices.end());
    }

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

    /* ============================================[research]============================================ */
    // (todo)
    // Graph smallGraph = encode(node->graph);
    // State sol = exactSolve(smallGraph);
    // State finalSol = decode(sol, node->graph);
    // return finalSol;
}

void MCTS::backpropagate(Node* node, double reward) {
    while (node != nullptr) {
        node->addExperience(reward);
        node = node->parent;
    }
}
