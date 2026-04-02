import argparse
import json
import os
import re
from collections import defaultdict
from typing import Any, Dict, List, Set, Tuple, cast


def generate_greedy_bad_vertex_cover_instance(n: int) -> Tuple[Dict[str, Set[str]], List[str], Dict[int, List[str]]]:
    """
    Generate the bipartite graph G_n used to show that max-degree greedy
    for minimum vertex cover can have Omega(log n) approximation ratio.

    Construction:
      - Left side L = {L1, ..., Ln}
      - For each i = 2..n:
          create floor(n / i) vertices in R_i
          each such vertex has degree exactly i
      - Each left vertex is adjacent to at most one vertex from each R_i

        Returns:
            graph: dict[str, set[str]]   adjacency list
            L: list[str]
            R_groups: dict[int, list[str]]   vertices in each R_i
    """

    if n < 2:
        raise ValueError("n must be at least 2")

    L = [f"L{j}" for j in range(n)]
    graph: Dict[str, Set[str]] = defaultdict(set)
    R_groups: Dict[int, List[str]] = {}

    # Ensure all left vertices appear in the graph even if isolated
    for u in L:
        graph[u]

    for i in range(2, n + 1):
        cnt = n // i
        group: List[str] = []

        # Use cyclic windows of length i:
        # for the t-th vertex in R_i, connect to
        # L[t], L[t+1], ..., L[t+i-1] mod n
        #
        # This ensures:
        #   1) each R_i vertex has degree exactly i
        #   2) each L vertex is used by exactly cnt vertices from R_i
        #      if cnt = n//i and we only create cnt such windows,
        #      so in particular each L vertex is not adjacent to
        #      "too many" R_i vertices.
        #
        # If you want the stronger textbook condition
        # "each L vertex is adjacent to at most one vertex from each R_i",
        # that can only be satisfied when the chosen i-subsets are disjoint,
        # which requires cnt * i <= n. Since cnt = floor(n/i), that is true,
        # and the disjoint-block construction below is even cleaner.
        #
        # So we use disjoint blocks instead of cyclic windows.
        for t in range(cnt):
            r = f"R{i}_{t}"
            group.append(r)
            graph[r]  # initialize

            # disjoint block of size i
            neighbors = L[t * i : (t + 1) * i]

            # sanity: this should always have size i
            if len(neighbors) != i:
                raise RuntimeError(
                    f"Internal construction error for i={i}, t={t}, "
                    f"expected {i} neighbors, got {len(neighbors)}"
                )

            for u in neighbors:
                graph[r].add(u)
                graph[u].add(r)

        R_groups[i] = group

    return dict(graph), L, R_groups


def _to_edge_list_undirected(graph: Dict[str, Set[str]]) -> List[Tuple[str, str]]:
    """Return undirected edges (u,v) with u < v, no duplicates."""
    edges: Set[Tuple[str, str]] = set()
    for u, nbrs in graph.items():
        for v in nbrs:
            a, b = (u, v) if u < v else (v, u)
            if a != b:
                edges.add((a, b))
    return sorted(edges)


def _stable_node_order(graph: Dict[str, Set[str]]) -> List[str]:
    """Order nodes as L0.. then R* to keep IDs stable/readable."""
    nodes = sorted(graph.keys())
    l_nodes = [u for u in nodes if u.startswith("L")]
    r_nodes = [u for u in nodes if u.startswith("R")]
    other = [u for u in nodes if not (u.startswith("L") or u.startswith("R"))]
    return l_nodes + r_nodes + other


def export_one_instance_to_hack(
    *,
    n: int,
    hack_dir: str = "data/hack",
    solution_type: str = "greedy-bad-bipartite",
) -> Tuple[str, str]:
    """Generate 1 instance and append it to the hack dataset.

    Writes:
      - {hack_dir}/inputs/graph_XXXX.json
      - {hack_dir}/outputs/graph_XXXX.json
    And appends to:
      - {hack_dir}/manifest.json

    Input schema matches existing hack inputs:
      {"num_vertices": N, "edges": [[u,v], ...]}
    """
    graph, L, R_groups = generate_greedy_bad_vertex_cover_instance(n)

    # assign integer IDs
    ordered = _stable_node_order(graph)
    id_map = {name: idx for idx, name in enumerate(ordered)}
    edges = [[id_map[u], id_map[v]] for u, v in _to_edge_list_undirected(graph)]

    greedy_cover = greedy_vertex_cover_max_degree(graph)
    greedy_size = len(greedy_cover)

    inputs_dir = os.path.join(hack_dir, "inputs")
    outputs_dir = os.path.join(hack_dir, "outputs")
    os.makedirs(inputs_dir, exist_ok=True)
    os.makedirs(outputs_dir, exist_ok=True)

    manifest_path = os.path.join(hack_dir, "manifest.json")
    manifest: Dict[str, Any]
    if os.path.exists(manifest_path):
        with open(manifest_path, "r") as f:
            manifest = json.load(f)
    else:
        manifest = {"instances": []}

    instances_any: Any = manifest.get("instances", [])
    if isinstance(instances_any, list):
        instances: List[Dict[str, Any]] = []
        for item in cast(List[Any], instances_any):
            if isinstance(item, dict):
                instances.append(cast(Dict[str, Any], item))
    else:
        instances = []

    used: Set[int] = set()
    for inst in instances:
        m = re.search(r"graph_(\d+)\.json$", str(inst.get("input", "")))
        if m:
            used.add(int(m.group(1)))
    next_idx: int = (max(used) + 1) if used else 0

    input_rel = f"data/hack/inputs/graph_{next_idx:04d}.json"
    output_rel = f"data/hack/outputs/graph_{next_idx:04d}.json"
    input_path = os.path.join(inputs_dir, f"graph_{next_idx:04d}.json")
    output_path = os.path.join(outputs_dir, f"graph_{next_idx:04d}.json")

    with open(input_path, "w") as f:
        json.dump({"num_vertices": len(ordered), "edges": edges}, f)

    with open(output_path, "w") as f:
        json.dump(
            {
                "solution_type": solution_type,
                "size": greedy_size,
                "notes": {
                    "generator": "data/asdf.py::generate_greedy_bad_vertex_cover_instance",
                    "n_param": n,
                    "size_is_greedy_upper_bound": True,
                    "greedy_selection_size": greedy_size,
                    "bipartite": True,
                    "left_size": len(L),
                    "right_size": len(ordered) - len(L),
                    "r_group_sizes": {str(k): len(v) for k, v in R_groups.items()},
                },
            },
            f,
            indent=2,
        )

    instances.append({"input": input_rel, "output": output_rel})
    manifest["instances"] = instances
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)

    return input_rel, output_rel


def greedy_vertex_cover_max_degree(graph: Dict[str, Set[str]]) -> List[str]:
    """
    Run the max-degree greedy algorithm:
      repeatedly pick a current maximum-degree vertex,
      add it to the cover, and delete it and all incident edges.

    Input graph is a dict[str, set[str]] adjacency list.

    Returns:
      cover: list[str] in selection order
    """
    g: Dict[str, Set[str]] = {v: set(neis) for v, neis in graph.items()}
    cover: List[str] = []

    def has_edges():
        return any(g[v] for v in g)

    while has_edges():
        # Pick a vertex of maximum current degree
        v = max(g, key=lambda x: len(g[x]))
        if len(g[v]) == 0:
            break

        cover.append(v)

        # Remove all incident edges
        for u in list(g[v]):
            g[u].remove(v)
        g[v].clear()

    return cover


def is_vertex_cover(graph: Dict[str, Set[str]], cover: List[str]) -> bool:
    """
    Check whether 'cover' is a valid vertex cover.
    """
    C: Set[str] = set(cover)
    seen_edges: Set[Tuple[str, str]] = set()

    for u, nbrs in graph.items():
        for v in nbrs:
            a, b = (u, v) if u < v else (v, u)
            e: Tuple[str, str] = (a, b)
            if e in seen_edges:
                continue
            seen_edges.add(e)
            if u not in C and v not in C:
                return False
    return True


def graph_stats(graph: Dict[str, Set[str]], L: List[str], R_groups: Dict[int, List[str]]) -> None:
    """
    Print basic stats of the generated graph.
    """
    num_vertices = len(graph)
    num_edges = sum(len(nbrs) for nbrs in graph.values()) // 2
    size_R = sum(len(group) for group in R_groups.values())

    print(f"|L| = {len(L)}")
    print(f"|R| = {size_R}")
    print(f"|V| = {num_vertices}")
    print(f"|E| = {num_edges}")
    print("R-group sizes:")
    for i in sorted(R_groups):
        print(f"  |R_{i}| = {len(R_groups[i])}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--n", type=int, default=12, help="n parameter for the construction")
    parser.add_argument(
        "--export-hack",
        action="store_true",
        help="Export exactly one generated instance into data/hack and update manifest.json",
    )
    parser.add_argument(
        "--hack-dir",
        type=str,
        default="data/hack",
        help="Hack dataset directory (default: data/hack)",
    )
    args = parser.parse_args()

    if args.export_hack:
        in_rel, out_rel = export_one_instance_to_hack(n=args.n, hack_dir=args.hack_dir)
        print(f"Exported 1 instance to hack dataset:\n  input:  {in_rel}\n  output: {out_rel}")
    else:
        # demo / sanity check
        graph, L, R_groups = generate_greedy_bad_vertex_cover_instance(args.n)
        graph_stats(graph, L, R_groups)

        greedy_cover = greedy_vertex_cover_max_degree(graph)

        print("\nGreedy selection order:")
        print(greedy_cover)

        print(f"\nGreedy cover size = {len(greedy_cover)}")
        print(f"Trivial cover L size = {len(L)}")
        print(f"Greedy/|L| ratio = {len(greedy_cover) / len(L):.3f}")

        print("\nIs greedy output a valid cover?", is_vertex_cover(graph, greedy_cover))

        # Show how many greedy picks came from each side
        greedy_L = [v for v in greedy_cover if v.startswith("L")]
        greedy_R = [v for v in greedy_cover if v.startswith("R")]
        print(f"Greedy picked {len(greedy_L)} vertices from L")
        print(f"Greedy picked {len(greedy_R)} vertices from R")