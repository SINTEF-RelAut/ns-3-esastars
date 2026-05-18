"""Generate topology diagrams for the three dual-IXP scenarios.

Outputs (one PNG each + a combined 3-panel figure):
  <out_dir>/topology_direct.png
  <out_dir>/topology_dual_visible.png
  <out_dir>/topology_dual_hidden.png
  <out_dir>/topology_all_scenarios.png
"""

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, Circle
from matplotlib.lines import Line2D
import numpy as np


# ---------------------------------------------------------------------------
# Colour / style palette
# ---------------------------------------------------------------------------
COL_MEMBER     = "#3A7FC1"
COL_IXP_SAT   = "#E07B39"
COL_BACKBONE   = "#90A4AE"
COL_EDGE_BB    = "#B0BEC5"
COL_EDGE_IXP   = "#E07B39"
COL_EDGE_PEER  = "#3A7FC1"
COL_EDGE_PEER2 = "#7BB5E8"

NODE_R_MAIN = 0.18
NODE_R_BB   = 0.13
FONT_MAIN   = 8.5
FONT_BB     = 7.5
FONT_LABEL  = 7.0
LW_MAIN     = 1.8
LW_BB       = 1.2
LW_PEER     = 2.0

# ---------------------------------------------------------------------------
# Node positions
# ---------------------------------------------------------------------------
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


# ---------------------------------------------------------------------------
# Drawing primitives
# ---------------------------------------------------------------------------

def _set_limits(ax, pos, margin=0.75):
    xs = [v[0] for v in pos.values()]
    ys = [v[1] for v in pos.values()]
    ax.set_xlim(min(xs) - margin, max(xs) + margin)
    ax.set_ylim(min(ys) - margin - 0.30, max(ys) + margin + 0.50)


def _edge(ax, p1, p2, col=COL_EDGE_BB, lw=LW_BB, ls="-", rad=0.0,
          alpha=1.0, zorder=2):
    x1, y1 = p1
    x2, y2 = p2
    if rad == 0.0:
        ax.plot([x1, x2], [y1, y2], color=col, lw=lw, ls=ls,
                alpha=alpha, zorder=zorder)
    else:
        ax.annotate("", xy=(x2, y2), xytext=(x1, y1),
                    arrowprops=dict(arrowstyle="-",
                                   color=col, lw=lw, linestyle=ls,
                                   connectionstyle=f"arc3,rad={rad}",
                                   alpha=alpha),
                    zorder=zorder)


def _edge_label(ax, p1, p2, label, col="gray", fontsize=FONT_LABEL,
                t=0.5, perp=0.14):
    x1, y1 = p1
    x2, y2 = p2
    mx = x1 + t * (x2 - x1)
    my = y1 + t * (y2 - y1)
    dx, dy = x2 - x1, y2 - y1
    length = max(np.hypot(dx, dy), 1e-9)
    lx = mx - dy / length * perp
    ly = my + dx / length * perp
    ax.text(lx, ly, label, ha="center", va="center",
            fontsize=fontsize, color=col, zorder=8,
            bbox=dict(boxstyle="round,pad=0.15", fc="white", ec="none",
                      alpha=0.80))


def _draw_nodes(ax, positions, members, ixp_sats, backbone):
    for node, (x, y) in positions.items():
        if node in members:
            col, r = COL_MEMBER, NODE_R_MAIN
        elif node in ixp_sats:
            col, r = COL_IXP_SAT, NODE_R_MAIN
        else:
            col, r = COL_BACKBONE, NODE_R_BB
        ax.add_patch(Circle((x, y), r, color=col, zorder=5,
                            linewidth=1.5, edgecolor="white"))

    for node, (x, y) in positions.items():
        is_main = node in (members | ixp_sats)
        r  = NODE_R_MAIN if is_main else NODE_R_BB
        fs = FONT_MAIN   if is_main else FONT_BB
        fw = "bold"      if is_main else "normal"
        inner = "IXP-A" if node == 120 else ("IXP-B" if node == 121 else str(node))
        ax.text(x, y, inner, ha="center", va="center",
                fontsize=fs - 0.5, fontweight=fw, color="white", zorder=7)
        # Sub-label
        if node in (120, 121):
            ax.text(x, y + r + 0.10, f"AS {node}", ha="center", va="bottom",
                    fontsize=FONT_BB - 0.5, color="#444444", zorder=7)
        elif node in members:
            ax.text(x, y - r - 0.10, f"AS {node}", ha="center", va="top",
                    fontsize=FONT_BB - 0.5, color="#444444", zorder=7)
        else:
            ax.text(x, y - r - 0.09, f"AS {node}", ha="center", va="top",
                    fontsize=FONT_BB - 1.0, color="#666666", zorder=7)


# ---------------------------------------------------------------------------
# Scenario drawing
# ---------------------------------------------------------------------------

def draw_direct(ax, pos):
    members  = {102, 103, 104, 105}
    backbone = {101, 106, 107, 108}

    for a, b in [(101, 102), (107, 103), (104, 108), (105, 106)]:
        _edge(ax, pos[a], pos[b], col=COL_EDGE_BB, lw=LW_BB)

    for a, b, lbl in [(102, 103, "GW1"), (103, 105, "GW2"),
                      (104, 105, "GW3"), (102, 104, "GW4")]:
        _edge(ax, pos[a], pos[b], col=COL_EDGE_PEER,  lw=LW_PEER, rad= 0.22)
        _edge(ax, pos[a], pos[b], col=COL_EDGE_PEER2, lw=LW_PEER * 0.75,
              ls="--", rad=-0.22)
        _edge_label(ax, pos[a], pos[b], lbl, col=COL_EDGE_PEER)

    _draw_nodes(ax, pos, members, set(), backbone)

    leg = [
        Line2D([0], [0], color=COL_EDGE_PEER,  lw=2,
               label="Direct peer link (primary)"),
        Line2D([0], [0], color=COL_EDGE_PEER2, lw=1.5, ls="--",
               label="Direct peer link (backup)"),
        Line2D([0], [0], color=COL_EDGE_BB,    lw=1.5,
               label="Backbone / transit link"),
        mpatches.Patch(color=COL_MEMBER,   label="IXP member AS (102–105)"),
        mpatches.Patch(color=COL_BACKBONE, label="Backbone AS"),
    ]
    ax.legend(handles=leg, loc="lower center", fontsize=FONT_LABEL,
              framealpha=0.9, ncol=2, bbox_to_anchor=(0.5, -0.01))
    ax.set_title("(a)  Direct Peering", fontsize=11, fontweight="bold", pad=7)


def draw_dual(ax, pos, hidden: bool):
    members  = {102, 103, 104, 105}
    ixp_sats = {120, 121}
    backbone = {101, 106, 107, 108}

    for a, b in [(101, 102), (107, 103), (104, 108), (105, 106)]:
        _edge(ax, pos[a], pos[b], col=COL_EDGE_BB, lw=LW_BB)

    for m in [102, 103, 104, 105]:
        _edge(ax, pos[m], pos[120], col=COL_EDGE_IXP, lw=LW_MAIN,
              rad= 0.15, alpha=0.92)
        _edge(ax, pos[m], pos[120], col=COL_EDGE_IXP, lw=LW_MAIN * 0.6,
              ls="--", rad=-0.15, alpha=0.48)
        _edge(ax, pos[m], pos[121], col=COL_EDGE_IXP, lw=LW_MAIN,
              rad=-0.15, alpha=0.92)
        _edge(ax, pos[m], pos[121], col=COL_EDGE_IXP, lw=LW_MAIN * 0.6,
              ls="--", rad= 0.15, alpha=0.48)

    if hidden:
        x_lo = min(pos[120][0], pos[121][0]) - 0.63
        x_hi = max(pos[120][0], pos[121][0]) + 0.63
        y_lo = min(pos[120][1], pos[121][1]) - 0.35
        y_hi = max(pos[120][1], pos[121][1]) + 0.55
        ax.add_patch(FancyBboxPatch(
            (x_lo, y_lo), x_hi - x_lo, y_hi - y_lo,
            boxstyle="round,pad=0.05",
            linewidth=1.8, edgecolor=COL_IXP_SAT,
            facecolor="#FFF3E6", alpha=0.65, zorder=1,
        ))
        ax.text((x_lo + x_hi) / 2, y_hi + 0.07,
                "Virtual IXP  (AS 110)",
                ha="center", va="bottom",
                fontsize=FONT_LABEL + 0.5, fontweight="bold",
                color=COL_IXP_SAT, zorder=9)
        ax.text((x_lo + x_hi) / 2, y_lo - 0.09,
                "1 active link/member\nfailure hidden from control plane",
                ha="center", va="top",
                fontsize=FONT_LABEL - 1.2, color=COL_IXP_SAT,
                style="italic", zorder=9,
                bbox=dict(boxstyle="round,pad=0.15", fc="white",
                          ec="none", alpha=0.70))

    _draw_nodes(ax, pos, members, ixp_sats, backbone)

    leg = [
        Line2D([0], [0], color=COL_EDGE_IXP, lw=2,
               label="IXP satellite link (primary)"),
        Line2D([0], [0], color=COL_EDGE_IXP, lw=1.3, ls="--",
               label="IXP satellite link (backup)"),
        Line2D([0], [0], color=COL_EDGE_BB,  lw=1.5,
               label="Backbone / transit link"),
        mpatches.Patch(color=COL_MEMBER,   label="IXP member AS (102–105)"),
        mpatches.Patch(color=COL_IXP_SAT,  label="IXP satellite AS"),
        mpatches.Patch(color=COL_BACKBONE, label="Backbone AS"),
    ]
    if hidden:
        leg.append(mpatches.Patch(
            facecolor="#FFF3E6", edgecolor=COL_IXP_SAT, linewidth=1.5,
            label="Virtual IXP abstraction"))
    ax.legend(handles=leg, loc="lower center", fontsize=FONT_LABEL,
              framealpha=0.9, ncol=2, bbox_to_anchor=(0.5, -0.01))

    title = ("(c)  Dual IXP Satellites — Hidden (VIXP)" if hidden
             else "(b)  Dual IXP Satellites — Visible")
    ax.set_title(title, fontsize=11, fontweight="bold", pad=7)


# ---------------------------------------------------------------------------
# Figure helpers
# ---------------------------------------------------------------------------

def _setup_ax(ax, pos, margin=0.75):
    ax.set_aspect("equal")
    ax.axis("off")
    _set_limits(ax, pos, margin=margin)


def make_fig(draw_fn, pos, w=6.0, h=4.8, dpi=150, margin=0.75, **kw):
    fig, ax = plt.subplots(figsize=(w, h), dpi=dpi)
    _setup_ax(ax, pos, margin=margin)
    draw_fn(ax, pos, **kw)
    fig.tight_layout(pad=0.4)
    return fig


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Generate topology diagrams for dual-IXP scenarios."
    )
    parser.add_argument("--out-dir", default="build/topology_figures")
    parser.add_argument("--dpi",    type=int, default=150)
    parser.add_argument("--format", default="png",
                        choices=["png", "pdf", "svg"])
    args = parser.parse_args()

    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    fmt = args.format

    specs = [
        (draw_direct, POS_DIRECT, "topology_direct",       {}),
        (draw_dual,   POS_DUAL,   "topology_dual_visible", {"hidden": False}),
        (draw_dual,   POS_DUAL,   "topology_dual_hidden",  {"hidden": True}),
    ]
    for draw_fn, pos, stem, kw in specs:
        fig = make_fig(draw_fn, pos, dpi=args.dpi, **kw)
        path = out / f"{stem}.{fmt}"
        fig.savefig(path, bbox_inches="tight")
        plt.close(fig)
        print(f"Wrote {path}")

    # Combined 3-panel
    fig_all, axes = plt.subplots(1, 3, figsize=(20, 6.2), dpi=args.dpi)
    for ax, pos in zip(axes, [POS_DIRECT, POS_DUAL, POS_DUAL]):
        _setup_ax(ax, pos, margin=0.75)
    draw_direct(axes[0], POS_DIRECT)
    draw_dual(axes[1], POS_DUAL, hidden=False)
    draw_dual(axes[2], POS_DUAL, hidden=True)
    fig_all.suptitle("Dual-IXP Scenario Topologies",
                     fontsize=13, fontweight="bold", y=1.01)
    fig_all.tight_layout(pad=0.9, w_pad=1.2)
    combined = out / f"topology_all_scenarios.{fmt}"
    fig_all.savefig(combined, bbox_inches="tight")
    plt.close(fig_all)
    print(f"Wrote {combined}")


if __name__ == "__main__":
    main()
