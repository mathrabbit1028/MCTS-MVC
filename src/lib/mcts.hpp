#ifndef MCTS_HPP
#define MCTS_HPP

#include "node.hpp"
#include "utils.hpp"

/**
 * @brief Class implementing the Monte Carlo Tree Search algorithm.
 */
class MCTS {
public:

    MCTS(Graph& graph, double explorationParam = 0.0);
    ~MCTS();

    /**
     * @brief Runs the MCTS loop once.
     */
    void run();

    /**
     * @brief Applies kernelization rules to simplify the problem at the given node.
     * @param node Pointer to the node to be kernelized.
     * @return true if any reduction was applied, false otherwise.
     */
    bool kernelization(Node* node);

    /**
     * @brief Retrieves the best solution found by MCTS.
     */
    State getSolution();

    /**
     * @brief Pointer to the root node of the search tree.
     */
    Node *root;

    /**
     * @brief The given graph for minimum vertex cover problem.
     */
    Graph graph;

    /**
     * @brief Exploration parameter for UCT sampling.
     */
    double explorationParam = 0.0;

    /**
     * @brief The best answer found so far (size of minimum vertex cover).
     */
    int answer;

    /**
     * @brief Sets the exploration parameter for UCT sampling.
     * @param param The exploration parameter to be set.
     */
    void setExplorationParam(double param);

    /**
     * @brief Selects a node to expand.
     * @param node Pointer to the current node.
     * @return Pointer to the selected node.
     */
    Node* select(Node* node);

    /**
     * @brief Expands the given node by adding child nodes.
     * @param node Pointer to the node to be expanded.
     * @return Pointer to the newly created child node.
     */
    Node* expand(Node* node);

    /**
     * @brief Simulates a random playout from the given node.
     * @param node Pointer to the node to be simulated.
     * @return The resulting solution from the simulation.
     */
    State simulate(Node* node);

    /**
     * @brief Backpropagates the results of the simulation up the tree.
     * @param node Pointer to the node to be updated.
     * @param reward The reward to be backpropagated.
     */
    void backpropagate(Node* node, double reward);

    /**
     * @brief Updates the expandable count of ancestor nodes.
     * @param node Pointer to the node whose ancestors are to be updated.
     */
    void expandableUpdate(Node* node);
};

#endif // MCTS_HPP