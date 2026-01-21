#include "mcts.hpp"
#define THRESHOLD_EXACT_SOLVE 16

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

    // Build a residual graph by removing all selected vertices and edges incident to them.
    // 1) Determine unselected vertices and create a compact index mapping.
    std::vector<int> oldToNew(this->graph.numVertices, -1);
    std::vector<int> keep;
    keep.reserve(this->graph.numVertices);
    for (int i = 0; i < this->graph.numVertices; ++i) {
        if (!node->state.selectedVertices.count(i)) {
            oldToNew[i] = static_cast<int>(keep.size());
            keep.push_back(i);
        }
    }
    // 2) Create remainGraph with only the unselected vertices.
    Graph remainGraph(static_cast<int>(keep.size()));
    // Preserve weights for kept vertices (default weight=1 if missing).
    for (int ni = 0; ni < (int)keep.size(); ++ni) {
        int oi = keep[ni];
        assert(oi < this->graph.numVertices);
        remainGraph.weights[ni] = this->graph.weights[oi];
    }
    // 3) Add edges among kept vertices, remapped to compact indices. Use u<v to avoid duplicates.
    for (int u = 0; u < this->graph.numVertices; ++u) {
        int su = oldToNew[u];
        if (su < 0) continue; // u was selected; removed
        for (int v : this->graph.adjacencyList[u]) {
            if (u < v) {
                int sv = oldToNew[v];
                if (sv < 0) continue; // v was selected; removed
                // add undirected edge once
                remainGraph.addEdge(su, sv);
            }
        }
    }

    State remainSol = GraphOracle::coarseSolve(remainGraph);
    State finalSol = node->state;
    for (int i = 0; i < this->graph.numVertices; ++i) {
        if (oldToNew[i] != -1) {
            if (remainSol.isSelected[oldToNew[i]]) {
                finalSol.include(i);
            }
        }
    }

    // assert(finalSol.isValid(this->graph));
    return finalSol;
}

void MCTS::backpropagate(Node* node, double reward) {
    while (node != nullptr) {
        node->addExperience(reward);
        node = node->parent;
    }
}
