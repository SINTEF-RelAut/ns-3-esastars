#!/usr/bin/env python3
"""Render a topology XML file into a PNG/SVG figure."""

from __future__ import annotations

import argparse
import xml.etree.ElementTree as ET
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Plot SCION topology XML")
    parser.add_argument("--topology", required=True, help="Path to topology XML")
    parser.add_argument(
        "--out",
        default="build/topology_figure.png",
        help="Output image path (.png, .svg, ...)",
    )
    parser.add_argument(
        "--title",
        default="SCION Topology",
        help="Figure title",
    )
    return parser.parse_args()


def node_sort_key(node_id: str) -> tuple[int, str]:
    if node_id.isdigit():
        return (0, f"{int(node_id):08d}")
    return (1, node_id)


def spread_layer(nodes: list[str], y: float, span: float) -> dict[str, tuple[float, float]]:
    if not nodes:
        return {}

    if len(nodes) == 1:
        return {nodes[0]: (0.0, y)}

    positions: dict[str, tuple[float, float]] = {}
    for i, node_id in enumerate(nodes):
        x = -span + (2.0 * span * i) / (len(nodes) - 1)
        positions[node_id] = (x, y)
    return positions


def layered_positions(
    node_types: dict[str, str], edges: list[tuple[str, str, str, str]]
) -> dict[str, tuple[float, float]]:
    core_nodes = [node for node, node_type in node_types.items() if node_type == "core"]
    customer_nodes = [node for node, node_type in node_types.items() if node_type != "core"]

    terrestrial_nodes: set[str] = set()
    for src, dst, relation, _ in edges:
        if relation != "customer":
            continue
        if node_types.get(src) == "core":
            terrestrial_nodes.add(src)
        if node_types.get(dst) == "core":
            terrestrial_nodes.add(dst)

    satellite_nodes = [node for node in core_nodes if node not in terrestrial_nodes]

    satellite_nodes = sorted(satellite_nodes, key=node_sort_key)
    terrestrial_nodes_sorted = sorted(terrestrial_nodes, key=node_sort_key)
    customer_nodes = sorted(customer_nodes, key=node_sort_key)

    positions: dict[str, tuple[float, float]] = {}
    positions.update(spread_layer(satellite_nodes, y=2.35, span=1.5))
    positions.update(spread_layer(terrestrial_nodes_sorted, y=0.15, span=4.1))
    positions.update(spread_layer(customer_nodes, y=-2.15, span=4.1))

    if set(satellite_nodes) == {"102", "103", "104", "105"}:
        positions["102"] = (-1.85, 2.55)
        positions["103"] = (-0.65, 1.75)
        positions["104"] = (0.65, 1.75)
        positions["105"] = (1.85, 2.55)

    # Fallback in case a node is uncategorized.
    for node_id in sorted(node_types.keys(), key=node_sort_key):
        positions.setdefault(node_id, (0.0, 0.0))

    return positions


def parse_topology(path: Path) -> tuple[dict[str, str], list[tuple[str, str, str, str]]]:
    tree = ET.parse(path)
    root = tree.getroot()

    node_types: dict[str, str] = {}
    for as_node in root.findall("./ases/as"):
        as_id = as_node.get("id", "")
        as_type = "non-core"
        for prop in as_node.findall("./property"):
            if prop.get("name") == "type":
                as_type = prop.get("value", "non-core")
                break
        node_types[as_id] = as_type

    edges: list[tuple[str, str, str, str]] = []
    for link in root.findall("./link"):
        src = (link.findtext("./from") or "").strip()
        dst = (link.findtext("./to") or "").strip()

        relation = "unknown"
        latency = ""
        for prop in link.findall("./property"):
            name = prop.get("name")
            if name == "rel":
                relation = (prop.text or "unknown").strip()
            elif name == "latency":
                latency = (prop.text or "").strip()

        if src and dst:
            edges.append((src, dst, relation, latency))

    return node_types, edges


def draw_graph(node_types: dict[str, str], edges: list[tuple[str, str, str, str]], out_path: Path, title: str) -> None:
    node_ids = sorted(node_types.keys(), key=node_sort_key)
    positions = layered_positions(node_types, edges)

    relation_colors = {
        "core": "#1f77b4",
        "peer": "#2ca02c",
        "customer": "#ff7f0e",
        "provider": "#d62728",
        "unknown": "#7f7f7f",
    }

    fig, ax = plt.subplots(figsize=(12.5, 8))

    # Draw edges first so nodes stay on top.
    for src, dst, relation, latency in edges:
        if src not in positions or dst not in positions:
            continue

        x1, y1 = positions[src]
        x2, y2 = positions[dst]
        color = relation_colors.get(relation, relation_colors["unknown"])
        ax.plot([x1, x2], [y1, y2], color=color, linewidth=2.0, alpha=0.85)

        # Label each edge with relation and latency, offset from the edge to avoid overlap.
        mx = (x1 + x2) / 2.0
        my = (y1 + y2) / 2.0
        dx = x2 - x1
        dy = y2 - y1
        length = (dx * dx + dy * dy) ** 0.5
        if length > 0:
            perp_x = -dy / length
            perp_y = dx / length
            direction = 1.0 if node_sort_key(src) <= node_sort_key(dst) else -1.0
            mx += direction * 0.09 * perp_x
            my += direction * 0.09 * perp_y

        edge_label = relation
        ax.text(
            mx,
            my,
            edge_label,
            fontsize=8,
            color=color,
            ha="center",
            va="center",
            bbox={"boxstyle": "round,pad=0.2", "facecolor": "white", "alpha": 0.75, "edgecolor": "none"},
        )

    # Draw nodes.
    for node_id in node_ids:
        x, y = positions[node_id]
        node_type = node_types.get(node_id, "non-core")
        face = "#083d77" if node_type == "core" else "#f4a261"
        ax.scatter([x], [y], s=1000, c=face, edgecolors="black", linewidths=1.4, zorder=3)
        ax.text(x, y, node_id, fontsize=12, color="white", ha="center", va="center", zorder=4)

    # Legend.
    from matplotlib.lines import Line2D

    legend_items = [
        Line2D([0], [0], marker="o", color="w", markerfacecolor="#083d77", markeredgecolor="black", markersize=10, label="Core AS"),
        Line2D([0], [0], marker="o", color="w", markerfacecolor="#f4a261", markeredgecolor="black", markersize=10, label="Non-core AS"),
        Line2D([0], [0], color=relation_colors["core"], lw=2, label="core link"),
        Line2D([0], [0], color=relation_colors["peer"], lw=2, label="peer link"),
        Line2D([0], [0], color=relation_colors["customer"], lw=2, label="customer link"),
    ]
    ax.legend(
        handles=legend_items,
        loc="center left",
        bbox_to_anchor=(1.02, 0.5),
        ncol=1,
        frameon=True,
    )

    ax.set_title(title, fontsize=15, pad=14)
    ax.set_axis_off()
    ax.set_aspect("equal", adjustable="box")
    ax.margins(x=0.08, y=0.14)
    fig.subplots_adjust(right=0.78, top=0.88)
    plt.tight_layout(rect=(0, 0, 0.78, 1))

    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=180, bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    args = parse_args()
    topology_path = Path(args.topology).resolve()
    out_path = Path(args.out).resolve()

    node_types, edges = parse_topology(topology_path)
    draw_graph(node_types, edges, out_path, args.title)

    print(out_path)


if __name__ == "__main__":
    main()
