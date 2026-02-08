#include <cassert>
#include <iostream>
#include "../lib/mcts.hpp"
#include "../lib/utils.hpp"

static Graph load_dataset_graph() {
    // Use the first generated instance from the moved exact dataset
    const std::string path = "/Users/geon4096/Documents/MCTS-MVC/data/exact/inputs/graph_0000.json";
    return loadGraphFromJson(path);
}

int main() {
    // Load a graph from dataset
    Graph g = load_dataset_graph();
    MCTS mcts(g);

    // Root should have a state sized to graph
    assert(mcts.root->state.isSelected.size() == static_cast<size_t>(g.numVertices));

    // Run MCTS a few iterations (assuming run() performs select/expand/simulate/backpropagate)
    for (int i = 0; i < 5; ++i) {
        mcts.run();
    }

    // After runs, expect some tree growth or visit updates
    assert(mcts.root->children.size() >= 1);

    // Check that some child has non-zero visits or value (tree being explored)
    bool anyVisited = false;
    for (Node* c : mcts.root->children) {
        if (c->visits > 0 || c->value != 0.0) { anyVisited = true; break; }
    }
    assert(anyVisited);

    // Try to add more children to root (only if still expandable)
    while (mcts.root->children.size() < 4 && mcts.root->expandable > 0) {
        mcts.expand(mcts.root);
    }
    // With kernelization, the root may become terminal early; just ensure we didn't violate invariants.
    assert(mcts.root->children.size() >= 1);

    // Encourage depth: expand one of the children to create grandchildren (if possible)
    Node* child0 = mcts.root->children[0];
    for (int i = 0; i < 2; ++i) {
        if (child0->expandable == 0) break;
        mcts.expand(child0);
    }
    // With edge-based branching, a successful expand will typically add 2 children
    // (unless no valid actionEdge remains).
    assert(child0->children.empty() || child0->children.size() >= 2);

    // Tree policy sanity: should return one of the children
    Node* picked = treePolicy::epsilonGreedy(mcts.root);
    assert(picked != nullptr);
    bool belongs = false;
    for (Node* c : mcts.root->children) {
        if (c == picked) { belongs = true; break; }
    }
    assert(belongs);

    std::cout << "All tests passed." << std::endl;
    return 0;
}
