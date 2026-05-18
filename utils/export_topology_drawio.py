"""Export editable Draw.io diagrams for the dual-IXP topology figures.

This writes one .drawio.xml file per scenario so the diagrams can be opened
and edited in draw.io / diagrams.net.
"""

from __future__ import annotations

import argparse
import html
import uuid
from pathlib import Path
import xml.etree.ElementTree as ET


# Layout shared with utils/plot_topology_figures.py
POS_DIRECT = {
    102: (-1.30,  0.45),
    103: ( 1.30,  0.45),
    104: (-1.30, -0.80),
    105: ( 1.30, -0.80),
    101: (-3.00,  0.45),
    107: ( 3.00,  0.45),
    108: (-3.00, -0.80),
    106: ( 3.00, -0.80),
}

POS_DUAL = {
    120: (-0.60,  1.95),
    121: ( 0.60,  1.95),
    102: (-1.45,  0.55),
    103: ( 1.45,  0.55),
    104: (-1.45, -0.85),
    105: ( 1.45, -0.85),
    101: (-3.00,  0.55),
    107: ( 3.00,  0.55),
    108: (-3.00, -0.85),
    106: ( 3.00, -0.85),
}

# Approximate figure bounds from the matplotlib version; draw.io uses pixels.
# We use a simple linear scale to keep the layouts compact and editable.
SCALE = 120
SHIFT_X = 460
SHIFT_Y = 360
NODE_W = 70
NODE_H = 34
BACKBONE_W = 60
BACKBONE_H = 30

COLORS = {
    "member": "#3A7FC1",
    "ixp_sat": "#E07B39",
    "backbone": "#90A4AE",
    "edge_bb": "#B0BEC5",
    "edge_ixp": "#E07B39",
    "edge_peer": "#3A7FC1",
    "edge_peer2": "#7BB5E8",
}


def to_px(x: float, y: float) -> tuple[float, float]:
    return SHIFT_X + x * SCALE, SHIFT_Y - y * SCALE


class IdGen:
    def __init__(self) -> None:
        self._count = 0

    def next(self) -> str:
        self._count += 1
        return f"n{self._count}"


def _style(parts: list[str]) -> str:
    return ";".join(parts) + ";"


def _vertex(cell_id: str, x: float, y: float, w: float, h: float, label: str,
            fill: str, font_color: str = "#ffffff", stroke: str = "#ffffff",
            rounded: bool = False) -> ET.Element:
    cell = ET.Element("mxCell", {
        "id": cell_id,
        "value": html.escape(label),
        "style": _style([
            "shape=ellipse",
            f"fillColor={fill}",
            f"strokeColor={stroke}",
            "strokeWidth=2",
            f"fontColor={font_color}",
            "align=center",
            "verticalAlign=middle",
            "html=1",
            "whiteSpace=wrap",
            "shadow=0",
        ]),
        "vertex": "1",
        "parent": "1",
    })
    geom = ET.SubElement(cell, "mxGeometry", {
        "x": str(x - w / 2),
        "y": str(y - h / 2),
        "width": str(w),
        "height": str(h),
        "as": "geometry",
    })
    return cell


def _edge(cell_id: str, source: str, target: str, label: str = "", color: str = "#000000",
          dashed: bool = False, curved: bool = False, orthogonal: bool = False,
          dashed2: bool = False) -> ET.Element:
    style = [
        "edgeStyle=orthogonalEdgeStyle" if orthogonal else "edgeStyle=elbowEdgeStyle",
        "endArrow=none",
        f"strokeColor={color}",
        "strokeWidth=2",
        "html=1",
        "rounded=0",
    ]
    if curved:
        style[0] = "edgeStyle=elbowEdgeStyle"
        style.append("curved=1")
    if dashed or dashed2:
        style.append("dashed=1")
        style.append("dashPattern=6 4")
        if dashed2:
            style.append("strokeWidth=1.5")
    cell = ET.Element("mxCell", {
        "id": cell_id,
        "value": html.escape(label),
        "style": _style(style),
        "edge": "1",
        "parent": "1",
        "source": source,
        "target": target,
    })
    ET.SubElement(cell, "mxGeometry", {"relative": "1", "as": "geometry"})
    return cell


def _label(cell_id: str, x: float, y: float, text: str, color: str = "#444444") -> ET.Element:
    cell = ET.Element("mxCell", {
        "id": cell_id,
        "value": html.escape(text),
        "style": _style([
            "text;html=1;align=center;verticalAlign=middle;whiteSpace=wrap;",
            f"fontColor={color}",
            "fontStyle=1",
            "strokeColor=none",
            "fillColor=none",
            "spacing=2",
        ]),
        "vertex": "1",
        "parent": "1",
    })
    ET.SubElement(cell, "mxGeometry", {
        "x": str(x),
        "y": str(y),
        "width": "120",
        "height": "30",
        "as": "geometry",
    })
    return cell


def _header(title: str) -> ET.Element:
    mxfile = ET.Element("mxfile", {
        "host": "app.diagrams.net",
        "modified": "2026-05-18T00:00:00.000Z",
        "agent": "GitHub Copilot",
        "version": "24.7.17",
        "type": "device",
    })
    diagram = ET.SubElement(mxfile, "diagram", {"id": str(uuid.uuid4()), "name": title})
    model = ET.SubElement(diagram, "mxGraphModel", {
        "dx": "1200",
        "dy": "800",
        "grid": "1",
        "gridSize": "10",
        "guides": "1",
        "tooltips": "1",
        "connect": "1",
        "arrows": "1",
        "fold": "1",
        "page": "1",
        "pageScale": "1",
        "pageWidth": "1200",
        "pageHeight": "800",
        "math": "0",
        "shadow": "0",
    })
    root = ET.SubElement(model, "root")
    ET.SubElement(root, "mxCell", {"id": "0"})
    ET.SubElement(root, "mxCell", {"id": "1", "parent": "0"})
    return mxfile


def _place_nodes(root: ET.Element, positions: dict[int, tuple[float, float]],
                 idgen: IdGen, scenario: str) -> dict[int, str]:
    ids: dict[int, str] = {}
    for node, (x, y) in positions.items():
        px, py = to_px(x, y)
        if node in {102, 103, 104, 105}:
            fill = COLORS["member"]
            w, h = NODE_W, NODE_H
            label = f"AS {node}"
        elif node in {120, 121}:
            fill = COLORS["ixp_sat"]
            w, h = NODE_W, NODE_H
            label = f"IXP-{chr(65 + (node - 120))}\nAS {node}"
        else:
            fill = COLORS["backbone"]
            w, h = BACKBONE_W, BACKBONE_H
            label = f"AS {node}"
        cid = idgen.next()
        ids[node] = cid
        root.append(_vertex(cid, px, py, w, h, label, fill))

    if scenario == "hidden":
        x1, y1 = to_px(POS_DUAL[120][0], POS_DUAL[120][1])
        x2, y2 = to_px(POS_DUAL[121][0], POS_DUAL[121][1])
        box_id = idgen.next()
        box = ET.Element("mxCell", {
            "id": box_id,
            "value": html.escape("Virtual IXP (AS 110)"),
            "style": _style([
                "shape=rectangle",
                "rounded=1",
                "fillColor=#FFF3E6",
                "strokeColor=#E07B39",
                "strokeWidth=2",
                "fontColor=#E07B39",
                "align=center",
                "verticalAlign=top",
                "html=1",
                "whiteSpace=wrap",
            ]),
            "vertex": "1",
            "parent": "1",
        })
        ET.SubElement(box, "mxGeometry", {
            "x": str(min(x1, x2) - 70),
            "y": str(min(y1, y2) - 65),
            "width": str(abs(x2 - x1) + 140),
            "height": "160",
            "as": "geometry",
        })
        root.insert(2, box)
        label_id = idgen.next()
        root.append(_label(label_id, min(x1, x2) - 40, max(y1, y2) + 40,
                           "1 active link/member\nfailure hidden from control plane",
                           color="#E07B39"))
    return ids


def build_direct() -> ET.Element:
    mxfile = _header("Direct Peering")
    root = mxfile.find(".//root")
    assert root is not None
    idgen = IdGen()
    ids = _place_nodes(root, POS_DIRECT, idgen, "direct")

    # edges
    for a, b in [(101, 102), (107, 103), (104, 108), (105, 106)]:
        root.append(_edge(idgen.next(), ids[a], ids[b], color=COLORS["edge_bb"], orthogonal=True))
    for a, b, lbl in [(102, 103, "GW1"), (103, 105, "GW2"), (104, 105, "GW3"), (102, 104, "GW4")]:
        root.append(_edge(idgen.next(), ids[a], ids[b], label=lbl, color=COLORS["edge_peer"], curved=True))
        root.append(_edge(idgen.next(), ids[a], ids[b], color=COLORS["edge_peer2"], curved=True, dashed=True))
    return mxfile


def build_dual(hidden: bool) -> ET.Element:
    title = "Dual IXP Hidden" if hidden else "Dual IXP Visible"
    mxfile = _header(title)
    root = mxfile.find(".//root")
    assert root is not None
    idgen = IdGen()
    ids = _place_nodes(root, POS_DUAL, idgen, "hidden" if hidden else "visible")

    for a, b in [(101, 102), (107, 103), (104, 108), (105, 106)]:
        root.append(_edge(idgen.next(), ids[a], ids[b], color=COLORS["edge_bb"], orthogonal=True))

    for m in [102, 103, 104, 105]:
        root.append(_edge(idgen.next(), ids[m], ids[120], color=COLORS["edge_ixp"], curved=True))
        root.append(_edge(idgen.next(), ids[m], ids[120], color=COLORS["edge_ixp"], curved=True, dashed=True))
        root.append(_edge(idgen.next(), ids[m], ids[121], color=COLORS["edge_ixp"], curved=True))
        root.append(_edge(idgen.next(), ids[m], ids[121], color=COLORS["edge_ixp"], curved=True, dashed=True))
    return mxfile


def write_xml(path: Path, mxfile: ET.Element) -> None:
    tree = ET.ElementTree(mxfile)
    ET.indent(tree, space="  ")
    path.write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n' +
        ET.tostring(mxfile, encoding="unicode"),
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Export draw.io XML for dual-IXP topologies.")
    parser.add_argument("--out-dir", default="build/topology_figures")
    args = parser.parse_args()

    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)

    files = {
        "topology_direct.drawio.xml": build_direct(),
        "topology_dual_visible.drawio.xml": build_dual(hidden=False),
        "topology_dual_hidden.drawio.xml": build_dual(hidden=True),
    }
    for name, mxfile in files.items():
        path = out / name
        write_xml(path, mxfile)
        print(f"Wrote {path}")


if __name__ == "__main__":
    main()
