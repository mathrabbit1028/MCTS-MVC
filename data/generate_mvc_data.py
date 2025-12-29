#!/usr/bin/env python3
import argparse
import json
import os
import random
from typing import List, Tuple

random.seed(42)

def gen_random_graph(n: int, p: float) -> List[Tuple[int, int]]:
    edges = []
    for u in range(n):
        for v in range(u + 1, n):
            if random.random() < p:
                edges.append((u, v))
    return edges

def edges_from_adj(adj: List[List[int]]) -> List[Tuple[int, int]]:
    edges = []
    for u in range(len(adj)):
        for v in adj[u]:
            if u < v:
                edges.append((u, v))
    return edges

def is_cover(n: int, edges: List[Tuple[int, int]], cover_bits: int) -> bool:
    for u, v in edges:
        if not (((cover_bits >> u) & 1) or ((cover_bits >> v) & 1)):
            return False
    return True

def exact_min_vertex_cover(n: int, edges: List[Tuple[int, int]]) -> List[int]:
    best_bits = None
    best_size = n + 1
    # simple branch: iterate by increasing size
    for size in range(n + 1):
        # iterate all subsets of given size using bit masks
        # combinatorial generation: iterate all masks and check size
        for mask in range(1 << n):
            # Python 3.8 compatibility: count bits via bin()
            if bin(mask).count("1") == size and is_cover(n, edges, mask):
                best_bits = mask
                best_size = size
                # Found minimal size; return indices
                res = [i for i in range(n) if (best_bits >> i) & 1]
                return res
    # fallback (shouldn't happen): return all vertices
    return list(range(n))

def greedy_vertex_cover(n: int, edges: List[Tuple[int, int]]) -> List[int]:
    remaining = set(edges)
    cover = []
    # adjacency degree
    while remaining:
        # pick vertex with max degree in remaining edges
        deg = [0] * n
        for u, v in remaining:
            deg[u] += 1
            deg[v] += 1
        w = max(range(n), key=lambda i: deg[i])
        cover.append(w)
        # remove all edges incident to w
        remaining = { (u, v) for (u, v) in remaining if u != w and v != w }
    return sorted(cover)

def greedy_vertex_cover_min_degree(n: int, edges: List[Tuple[int, int]]) -> List[int]:
    """A contrarian heuristic: repeatedly select the min-degree vertex.
    Included to diversify heuristics; sometimes helps on sparse graphs."""
    remaining = set(edges)
    cover = []
    while remaining:
        deg = [0] * n
        for u, v in remaining:
            deg[u] += 1
            deg[v] += 1
        # select min-degree among vertices incident to remaining edges
        incident = {u for e in remaining for u in e}
        w = min(incident, key=lambda i: deg[i])
        cover.append(w)
        remaining = { (u, v) for (u, v) in remaining if u != w and v != w }
    return sorted(cover)

def two_approx_vertex_cover(n: int, edges: List[Tuple[int, int]]) -> List[int]:
    """Standard 2-approx algorithm: repeatedly pick an arbitrary edge and
    add both endpoints to the cover."""
    remaining = set(edges)
    cover = set()
    while remaining:
        u, v = next(iter(remaining))
        cover.add(u); cover.add(v)
        # remove all edges incident to u or v
        remaining = { (a, b) for (a, b) in remaining if a not in (u, v) and b not in (u, v) }
    return sorted(cover)

def greedy_vertex_cover_random_edge(n: int, edges: List[Tuple[int, int]]) -> List[int]:
    """Randomized heuristic: pick a random remaining edge, add the higher-degree
    endpoint to the cover. Deterministic given fixed global seed."""
    remaining = set(edges)
    cover = []
    while remaining:
        # compute degrees once per iteration
        deg = [0] * n
        for u, v in remaining:
            deg[u] += 1
            deg[v] += 1
        # choose a random edge
        u, v = random.choice(tuple(remaining))
        w = u if deg[u] >= deg[v] else v
        cover.append(w)
        remaining = { (a, b) for (a, b) in remaining if a != w and b != w }
    return sorted(cover)

def min_of_greedy_variants(n: int, edges: List[Tuple[int, int]]):
    """Run multiple greedy heuristics and return the best (smallest) cover.
    Returns (name, cover_list)."""
    candidates = [
        ("max_degree", greedy_vertex_cover(n, edges)),
        ("two_approx_edge_pair", two_approx_vertex_cover(n, edges)),
        ("random_edge_high_degree_endpoint", greedy_vertex_cover_random_edge(n, edges)),
        ("min_degree", greedy_vertex_cover_min_degree(n, edges)),
    ]
    # pick the smallest cover
    best_name, best_cover = min(candidates, key=lambda kv: len(kv[1]))
    return best_name, best_cover, [(name, len(cov)) for name, cov in candidates]

def save_instance(out_dir: str, idx: int, n: int, edges: List[Tuple[int, int]], exact_limit: int, mode: str = "auto"):
    inst_dir = os.path.join(out_dir, "inputs")
    outp_dir = os.path.join(out_dir, "outputs")
    os.makedirs(inst_dir, exist_ok=True)
    os.makedirs(outp_dir, exist_ok=True)

    input_path = os.path.join(inst_dir, f"graph_{idx:04d}.json")
    output_path = os.path.join(outp_dir, f"graph_{idx:04d}.json")

    with open(input_path, "w") as f:
        json.dump({"num_vertices": n, "edges": edges}, f)

    # compute solution
    if mode == "exact" or (mode == "auto" and n <= exact_limit):
        cover = exact_min_vertex_cover(n, edges)
        kind = "exact"
        extra = {}
    elif mode == "large":
        best_name, cover, details = min_of_greedy_variants(n, edges)
        kind = "heuristic_min_of_greedy"
        extra = {
            "chosen_heuristic": best_name,
            "heuristics": [{"name": nm, "size": sz} for nm, sz in details]
        }
    else:
        cover = greedy_vertex_cover(n, edges)
        kind = "greedy"
        extra = {}

    with open(output_path, "w") as f:
        out_obj = {
            "solution_type": kind,
            "vertex_cover": cover,
            "size": len(cover)
        }
        # include heuristic breakdown for large mode
        out_obj.update(extra)
        json.dump(out_obj, f)

    return input_path, output_path

def main():
    parser = argparse.ArgumentParser(description="Generate Minimum Vertex Cover datasets")
    parser.add_argument("--count", type=int, default=20, help="Number of instances")
    parser.add_argument("--min-n", type=int, default=8, help="Minimum number of vertices")
    parser.add_argument("--max-n", type=int, default=20, help="Maximum number of vertices")
    parser.add_argument("--edge-prob", type=float, default=0.2, help="Edge probability (0..1)")
    parser.add_argument("--exact-limit", type=int, default=16, help="Max n for exact solver")
    parser.add_argument("--dataset-type", type=str, choices=["auto", "exact", "large"], default="auto",
                        help="exact: force exact solutions; large: use multi-greedy estimation; auto: exact if n<=exact-limit")
    parser.add_argument("--out-dir", type=str, default="data/mvc", help="Output directory root")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    manifest = []
    for i in range(args.count):
        n = random.randint(args.min_n, args.max_n)
        edges = gen_random_graph(n, args.edge_prob)
        in_path, out_path = save_instance(args.out_dir, i, n, edges, args.exact_limit, mode=args.dataset_type)
        manifest.append({"input": in_path, "output": out_path})

    with open(os.path.join(args.out_dir, "manifest.json"), "w") as f:
        json.dump({"instances": manifest}, f, indent=2)

if __name__ == "__main__":
    main()
