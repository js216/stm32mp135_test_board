#!/usr/bin/env python3
"""
Generate a Graphviz inclusion graph for C/C++ projects.

Features:
- Merges .c/.cpp with corresponding .h/.hpp into a single node.
- Resolves includes across subdirectories.
- Detects inclusion cycles and exits 1 if any exist.
- Writes DOT to stdout.
"""

import sys
import re
from pathlib import Path
from collections import defaultdict

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')

def parse_includes(file_path):
    """Return list of quoted #include filenames in the file."""
    includes = []
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                m = INCLUDE_RE.match(line)
                if m:
                    includes.append(m.group(1))
    except Exception as e:
        print(f"Warning: failed to read {file_path}: {e}", file=sys.stderr)
    return includes

def node_name(path):
    """Return node name without extension (merge header and source)."""
    return Path(path).stem

def build_graph(files):
    """Build inclusion graph: node -> set of nodes it includes."""
    files = [Path(f).resolve() for f in files]
    graph = defaultdict(set)

    # Maps for resolving includes
    path_map = {str(f.relative_to(Path.cwd())): f for f in files}
    filename_map = {f.name: f for f in files}

    for f in files:
        includes = parse_includes(f)
        src_node = node_name(f)
        for inc in includes:
            # exact relative path match
            if inc in path_map:
                tgt_node = node_name(path_map[inc])
            # fallback: match by filename only
            elif Path(inc).name in filename_map:
                tgt_node = node_name(filename_map[Path(inc).name])
            else:
                continue  # skip includes we don’t have
            if tgt_node != src_node:  # avoid self-loop
                graph[src_node].add(tgt_node)
    return graph

def detect_cycles(graph):
    """Return list of cycles (each cycle is a list of nodes)."""
    visited = set()
    stack = set()
    cycles = []

    def visit(node, path):
        if node in stack:
            cycle_start = path.index(node)
            cycles.append(path[cycle_start:])
            return
        if node in visited:
            return
        visited.add(node)
        stack.add(node)
        for neighbor in graph.get(node, []):
            visit(neighbor, path + [neighbor])
        stack.remove(node)

    for node in graph:
        visit(node, [node])
    return cycles

def write_graphviz(graph, cycles):
    """Output Graphviz DOT to stdout, highlighting cycle edges in red."""
    print("digraph inclusions {")
    print("  node [shape=box];")

    # collect all edges that are part of cycles
    cycle_edges = set()
    for c in cycles:
        for i in range(len(c)):
            src = c[i]
            tgt = c[(i + 1) % len(c)]  # wrap around to form cycle
            cycle_edges.add((src, tgt))

    for src, targets in graph.items():
        for tgt in targets:
            if (src, tgt) in cycle_edges:
                print(f'  "{src}" -> "{tgt}" [color=red];')
            else:
                print(f'  "{src}" -> "{tgt}";')

    print("}")

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file1> <file2> ...", file=sys.stderr)
        sys.exit(1)

    files = sys.argv[1:]
    graph = build_graph(files)
    cycles = detect_cycles(graph)

    write_graphviz(graph, cycles)

    if cycles:
        print("Inclusion cycles detected:", file=sys.stderr)
        for c in cycles:
            print(" -> ".join(c), file=sys.stderr)
        sys.exit(1)
    sys.exit(0)

if __name__ == "__main__":
    main()
