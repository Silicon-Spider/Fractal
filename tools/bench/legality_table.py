#!/usr/bin/env python3
"""Tabulate fractal-legality's JSON output, one column per file.

Each file is one run of `fractal-legality --json`. For every bucket the runs share, it
prints a Markdown table: a row per measure, nanoseconds per position with the heap
allocations in brackets, then the two derived rows the tool prints itself (the first
turn after construction, and each further turn).

Usage: python tools/bench/legality_table.py LABEL=FILE [LABEL=FILE ...]
"""
from __future__ import annotations

import json
import sys
from pathlib import Path


def cell(ns: float, allocations: float) -> str:
    return f"{ns:,.0f} ({allocations:.1f})" if allocations >= 0.05 else f"{ns:,.0f}"


def main() -> int:
    if len(sys.argv) < 2 or any("=" not in a for a in sys.argv[1:]):
        print(__doc__)
        return 2
    runs = []
    for arg in sys.argv[1:]:
        label, _, path = arg.partition("=")
        runs.append((label, json.loads(Path(path).read_text(encoding="utf-8"))))
    first = runs[0][1]
    print(f"scorer {first['scorer']}; {first['passes']} passes; K = {first['turns']} turns; stride {first['stride']}")
    for bucket in first["buckets"]:
        name = bucket["name"]
        columns = []
        for label, run in runs:
            match = [b for b in run["buckets"] if b["name"] == name]
            if match:
                columns.append((label, match[0]))
        turns = bucket["turns"] / bucket["positions"]
        print(f"\n### {name} boards: {bucket['positions']:,} turn starts, {bucket['with_turn']:,} with a legal turn, "
              f"{bucket['in_check']:,} in check; {bucket['mean_moves']:.1f} moves and "
              f"{bucket['mean_playable']:.2f} playable boards on average; {turns:.2f} turns taken of K\n")
        print("| Measure | " + " | ".join(label for label, _ in columns) + " |")
        print("| --- | " + " | ".join("---:" for _ in columns) + " |")
        for row in bucket["rows"]:
            cells = []
            for _, b in columns:
                r = next(x for x in b["rows"] if x["name"] == row["name"])
                cells.append(cell(r["ns"], r["allocations"]))
            print(f"| {row['name']} | " + " | ".join(cells) + " |")
        derived = {"first turn after the build": [], "each further turn": []}
        for _, b in columns:
            rows = {x["name"]: x for x in b["rows"]}
            build, one, k = rows["build"], rows["first"], rows["K turns"]
            t = b["turns"] / b["positions"]
            derived["first turn after the build"].append(
                cell(one["ns"] - build["ns"], one["allocations"] - build["allocations"]))
            derived["each further turn"].append(
                cell((k["ns"] - one["ns"]) / (t - 1), (k["allocations"] - one["allocations"]) / (t - 1)) if t > 1 else "-")
        for label, cells in derived.items():
            print(f"| {label} | " + " | ".join(cells) + " |")
        nodes = {b["nodes"] for _, b in columns}
        points = {b["points"] for _, b in columns}
        print(f"\ngenerator nodes {'equal in every run' if len(nodes) == 1 else 'DIFFER: ' + str(sorted(nodes))}; "
              f"points judged {'equal' if len(points) == 1 else 'DIFFER: ' + str(sorted(points))}; "
              f"successors failing the scan: {sum(b['successors_failing_scan'] for _, b in columns)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
