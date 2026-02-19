#ifndef UTILS_HPP
#define UTILS_HPP

#include <vector>
#include <unordered_set>
#include <cassert>
#include <string>

/**
 * @brief Represents an undirected graph.
 */
class Graph {
public:

    Graph(int numVertices);
    ~Graph();
    
    /**
     * @brief Number of vertices in the graph.
     */
    int numVertices;

    /**
     * @brief Adjacency list representing the graph.
     */
    std::vector<std::vector<int>> adjacencyList;

    /**
     * @brief Adds an undirected edge between two vertices.
     * @param u The first vertex.
     * @param v The second vertex.
     */
    void addEdge(int u, int v);
};

/**
 * @brief Load a Graph from a simple JSON file containing {"num_vertices": N, "edges": [[u,v], ...]}.
 * @param path Filesystem path to the JSON file.
 * @return Graph parsed from the file.
 */
Graph loadGraphFromJson(const std::string& path);

/**
 * @brief Represents the state of selected vertices in the graph.
 */
class State {
public:

    State();
    State(int numVertices);
    State(std::vector<bool> isSelectedInit);
    ~State();

    /**
     * @brief Boolean vector indicating selected vertices.
     */
    std::vector<bool> isSelected;

    /**
     * @brief Set of selected vertex indices.
     */
    std::unordered_set<int> selectedVertices;

    /**
     * @brief Set of possible vertices to select.
     */
    std::unordered_set<int> possibleVertices;

    /**
     * @brief Index of the action vertex.
     */
    int actionVertex;

    /**
     * @brief Selects a random action vertex from the possible vertices.
     * @param graph The graph to select the vertex from.
     * @return true if an action vertex was selected, false otherwise.
     */
    bool selectActionVertex(const Graph& graph);

    /**
     * @brief Selects a vertex in the solution.
     * @param vertex The vertex to be included. It must not be already selected.
     */
    void include(int vertex);

    /**
     * @brief Excludes a vertex in the solution.
     * @param vertex The vertex to be excluded.
     */
    void exclude(int vertex);
    
};

// Forward declaration to avoid circular include in headers
class Node;

namespace treePolicy {
    /**
     * @brief Samples a child node using the UCT formula.
     * @param node Pointer to the parent node.
     * @param explorationParam Exploration parameter for UCT.
     * @return Pointer to the selected child node.
     */
    Node* uctSampling(Node* node, double explorationParam = 0.0);

    /**
     * @brief Samples a child node using epsilon-greedy strategy based on state rewards.
     * @param node Pointer to the parent node.
     * @param explorationParam epsilon parameter for exploration.
     * @return Pointer to the selected child node.
     */
    Node* epsilonGreedy(Node* node, double explorationParam = 0.0);
};

#endif // UTILS_HPP