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
     * @brief Weights of the vertices.
     */
    std::vector<int> weights;

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

    /**
     * @brief Prints the graph's adjacency list and weights.
     */
    void print();
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
     * @brief Number of expandable actions.
     */
    int expandable;

    /**
     * @brief Selects a random vertex from the graph.
     * @return An integer representing the selected vertex.
     */
    int randomVertex();

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

    /**
     * @brief Score of the solution.
     * @return A double representing the evaluation score.
     */
    double evaluate();

    /**
     * @brief Gets the count of possible actions.
     * @return An integer representing the count of possible actions.
     */
    int getActionCounts();

    /**
     * @brief Checks if the state is a valid vertex cover for the given graph.
     * @param graph The graph to validate against.
     * @return true if the state is a valid vertex cover, false otherwise.
     */
    bool isValid(const Graph& graph) const;
};

// Forward declaration to avoid circular include in headers
class Node;

namespace UCT {
    /**
     * @brief Samples a child node using the UCT formula.
     * @param children Vector of pointers to child nodes.
     * @param explorationParam Exploration parameter for UCT.
     * @return Pointer to the selected child node.
     */
    Node* sampling(std::vector<Node*>& children, double explorationParam = 0.0);
};

namespace GraphOracle {
    /**
     * @brief Coarsens the graph by merging vertices.
     * @param g The original graph.
     * @return The coarsened graph, which approximately has half the vertices, with a mapping from original to coarsened vertices.
     */
    std::pair<Graph, std::vector<std::vector<int>>> coarsenGraph(const Graph& g);

    /**
     * @brief Solver for the coarsened graph.
     * @param graph The coarsened graph to solve.
     * @return The State representing the vertex cover on the coarsened graph.
     */
    State coarseSolve(const Graph& graph);

    /**
     * @brief Exact solver for small graphs using brute-force.
     * @param g The graph to solve.
     * @return The optimal State representing the vertex cover.
     */
    State exactSolve(const Graph& graph);

    /**
     * @brief Greedy solver for vertex cover.
     * @param g The graph to solve.
     * @return The approximate State representing the vertex cover.
     */
    State greedySolve(const Graph& graph);

    /**
     * @brief Lifts a solution from a coarsened graph back to the original graph.
     * @param coarseGraph The coarsened graph.
     * @param coarseState The solution state on the coarsened graph.
     * @param originalGraph The original graph.
     * @return The lifted State on the original graph.
     */
    State lifting(const Graph& coarseGraph, const State& coarseState, 
                  const Graph& originalGraph, const State& originalState);
}

#endif // UTILS_HPP