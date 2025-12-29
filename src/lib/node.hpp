#ifndef NODE_HPP
#define NODE_HPP

#include <vector>
#include "utils.hpp"

/**
 * @brief Represents a node in the Monte Carlo Tree Search.
 */
class Node {
public:

    Node();
    ~Node();
    
    /**
     * @brief Constructs a new Node.
     * @param child Pointer to the child node to be added.
     */
    void addChild(Node* child);

    /**
     * @brief Updates the node's statistics with the given reward.
     * @param reward The reward to be added to the node's experience.
     */
    void addExperience(double reward);

    /**
     * @brief Checks if the node is fully expanded.
     * @return true if the node is fully expanded, false otherwise.
     */
    bool full();

    /**
     * @brief Selected vertices at this node.
     */
    State state;

    /**
     * @brief Pointer to the parent node.
     */
    Node* parent;

    /**
     * @brief Vector of pointers to child nodes.
     */
    std::vector<Node*> children;

    /**
     * @brief Number of times the node has been visited.
     */
    int visits;

    /**
     * @brief Average reward of the node.
     */
    double value;
};

#endif // NODE_HPP