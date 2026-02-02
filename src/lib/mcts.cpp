#include "mcts.hpp"

MCTS::MCTS(Graph& graph, double explorationParam)
    : root(new Node())
    , graph(graph)
    , explorationParam(explorationParam) {
    root->state = State(graph.numVertices);
    answer = graph.numVertices; // Initial worst-case answer
    while (this->kernelization(root));
    if (!root->state.selectActionEdge(this->graph)) { 
        answer = std::count(root->state.isSelected.begin(), root->state.isSelected.end(), true);
        root->expandable = 0;
        expandableUpdate(root);
    }
}

MCTS::~MCTS() {
    delete root;
}

void MCTS::setExplorationParam(double param) {
    this->explorationParam = param;
}

void MCTS::expandableUpdate(Node* node) {
    while (node->expandable == 0) {
        node = node->parent;
        if (!node) return;
        node->expandable--;
    }
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
    int k = answer;
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
            if (!bestChild || c->maxValue > bestChild->maxValue || 
                (c->maxValue == bestChild->maxValue && c->visits > bestChild->visits)) {
                bestChild = c;
            }
        }
        node = bestChild;
    }
    return simulate(node);
}

Node* MCTS::select(Node* node) {
    if (!node->full()) return node;
    assert(node->expandable > 0 && "Node is fully expanded but marked expandable");
    if (node->expandable == 1) {
        assert(node->children.size() == 2);
        if (node->children[0]->expandable > 0) return select(node->children[0]);
        else return select(node->children[1]);
    }
    // return select(treePolicy::uctSampling(node, this->explorationParam));
    return select(treePolicy::epsilonGreedy(node, this->explorationParam));
}

Node* MCTS::expand(Node* node) {
    assert(node->expandable > 0 && "Cannot expand a fully expanded node");
    assert(node->state.actionEdge.first != -1 && "No valid action edge to expand on");

    Node *child = new Node();
    child->state = node->state;
    child->parent = node;
    child->state.include(node->state.actionEdge.first);
    // if (node->children.size() == 1) { child->state.exclude(node->state.actionEdge.second); }
    while (this->kernelization(child));
    if (!child->state.selectActionEdge(this->graph)) { 
        child->expandable = 0;
        expandableUpdate(child);
    }
    node->addChild(child);

    std::swap(node->state.actionEdge.first, node->state.actionEdge.second);

    return child;
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

    answer = std::min(answer, static_cast<int>(std::count(sel.begin(), sel.end(), true)));

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
