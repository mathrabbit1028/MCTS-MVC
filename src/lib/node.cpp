#include "node.hpp"

Node::Node() : parent(nullptr), visits(0), value(0.0), expandable(2) {}

Node::~Node() {
    for (Node* child : children) {
        delete child;
    }
}

void Node::addChild(Node* child) {
    children.push_back(child);
    child->parent = this;
}

void Node::addExperience(double reward) {
    visits++;
    // value <- value + (reward - value) / visits
    value += (reward - value) / static_cast<double>(visits);
    maxValue = std::max(maxValue, reward);
}

bool Node::full() {
    return this->children.size() == 2;
}

double Node::evaluate(const Graph& graph) {
    return 0.0;
}