#!/usr/bin/env python3
# Copyright (C) 2026 the DTVM authors. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
"""
Phase 5 figure-build script for the WISA 2026 paper.

Reads the §S6.0 paper SSOT (ablation_data.json) and emits up to 5 figures:
  Mandatory:
    - fig-noise-floor.{pdf,png}: Stage 0a/0b p99 + G1 effective threshold.
    - fig-r3-recovery.{pdf,png}: per-rule recovery_post for fired ranked_top_K.
  Conditional (skip when input is null/empty):
    - fig-tier-ablation.{pdf,png}: needs stage4.combined_tiers.
    - fig-interaction-matrix.{pdf,png}: needs stage4.fdr.n_valid_rules > 0.
    - fig-prod-vs-shadow.{pdf,png}: needs stage4.final_picks_n > 0.

The script enforces the §S6.0 contract: ONLY ablation_data.json is read; no
inlined data values anywhere in this file.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def log_skip(name: str, reason: str) -> None:
    """Emit a skip message for a conditional figure to stderr."""
    print(f"[fig] skipping {name}: {reason}", file=sys.stderr)


def log_emit(name: str, paths: list[Path]) -> None:
    """Emit an emission message for a built figure to stderr."""
    rendered = ", ".join(str(p) for p in paths)
    print(f"[fig] emitted {name}: {rendered}", file=sys.stderr)


def save_figure(fig: plt.Figure, out_dir: Path, stem: str, dpi: int) -> list[Path]:
    """Save a matplotlib figure as both PDF and PNG; return the paths."""
    out_dir.mkdir(parents=True, exist_ok=True)
    pdf_path = out_dir / f"{stem}.pdf"
    png_path = out_dir / f"{stem}.png"
    fig.savefig(pdf_path, format="pdf", bbox_inches="tight")
    fig.savefig(png_path, format="png", dpi=dpi, bbox_inches="tight")
    plt.close(fig)
    return [pdf_path, png_path]


# ---------------------------------------------------------------------------
# Mandatory figures
# ---------------------------------------------------------------------------

def build_noise_floor(data: dict[str, Any], out_dir: Path, dpi: int) -> None:
    """Single-panel bar chart of within-window p99, inter-run p99, and G1 threshold."""
    noise = data.get("noise") or {}
    p99_0a = noise.get("stage_0a_p99")
    p99_0b = noise.get("stage_0b_p99")
    threshold = noise.get("effective_threshold")
    if p99_0a is None or p99_0b is None or threshold is None:
        log_skip("fig-noise-floor", "noise.stage_0a_p99/stage_0b_p99/effective_threshold missing")
        return

    # Convert to percent for human-readable axes.
    values_pct = [p99_0a * 100.0, p99_0b * 100.0, threshold * 100.0]
    labels = [
        "Stage 0a p99\n(within-window)",
        "Stage 0b p99\n(inter-run)",
        "G1 threshold\n(effective)",
    ]
    colors = ["tab:blue", "tab:orange", "tab:gray"]

    fig, ax = plt.subplots(figsize=(5.0, 2.8))
    bars = ax.bar(labels, values_pct, color=colors, edgecolor="black", linewidth=0.6)
    for bar, val in zip(bars, values_pct):
        ax.text(
            bar.get_x() + bar.get_width() / 2.0,
            bar.get_height(),
            f"{val:.2f}%",
            ha="center",
            va="bottom",
            fontsize=9,
        )

    # Horizontal reference line for the G1 threshold across all bars.
    ax.axhline(threshold * 100.0, color="tab:gray", linestyle="--", linewidth=0.8, alpha=0.6)
    ax.annotate(
        f"G1 threshold = {threshold * 100.0:.2f}%",
        xy=(2, threshold * 100.0),
        xytext=(0.02, 0.92),
        textcoords="axes fraction",
        fontsize=8,
        color="tab:gray",
    )

    ax.set_ylabel("|relative delta| (%)")
    ax.set_title("Phase 5 noise floor and G1 threshold")
    ax.set_ylim(0, max(values_pct) * 1.25)
    ax.grid(axis="y", linestyle=":", linewidth=0.5, alpha=0.6)

    fig.tight_layout()
    paths = save_figure(fig, out_dir, "fig-noise-floor", dpi)
    log_emit("fig-noise-floor", paths)


def build_r3_recovery(data: dict[str, Any], out_dir: Path, dpi: int) -> None:
    """Per-rule horizontal bar chart of recovery_post for ranked_fired top_K."""
    stage3 = data.get("stage3") or {}
    top_k = (stage3.get("top_K") or {})
    ranked = top_k.get("ranked_fired") or []
    if not ranked:
        log_skip("fig-r3-recovery", "stage3.top_K.ranked_fired is empty")
        return

    # Sort by recovery_post descending so the most recovered rule is at the top.
    sorted_rules = sorted(
        ranked,
        key=lambda r: (r.get("recovery_post") or 0),
        reverse=True,
    )
    names = [r.get("name", "?") for r in sorted_rules]
    recoveries = [r.get("recovery_post") or 0 for r in sorted_rules]
    hits = [r.get("hit") or 0 for r in sorted_rules]
    classes = [r.get("class") or "?" for r in sorted_rules]

    n_rules = len(sorted_rules)
    n_recovered = sum(1 for v in recoveries if v > 0)

    fig, ax = plt.subplots(figsize=(5.0, 2.6))
    # Colour the recovered rules vs the zero-recovery rules.
    bar_colors = ["tab:blue" if v > 0 else "tab:gray" for v in recoveries]
    # In horizontal bars, matplotlib stacks y from bottom-up; reverse so first
    # entry (highest recovery) appears at the top.
    y_positions = list(range(n_rules))
    ax.barh(
        y_positions,
        recoveries,
        color=bar_colors,
        edgecolor="black",
        linewidth=0.6,
    )
    ax.set_yticks(y_positions)
    ax.set_yticklabels(names)
    ax.invert_yaxis()  # highest recovery at the top

    # Annotate each bar with hit count and class.
    max_recovery = max(recoveries) if recoveries else 0
    x_offset = max(max_recovery * 0.02, 0.5)
    for y, rec, hit, cls in zip(y_positions, recoveries, hits, classes):
        annot = f"hit={hit:,} | {cls}"
        ax.text(
            rec + x_offset,
            y,
            annot,
            va="center",
            ha="left",
            fontsize=8,
        )

    ax.set_xlabel("recovery_post (audit-aware delta hits)")
    ax.set_title("R3 recovery for fired ranked top-K rules")
    if max_recovery > 0:
        # Pad x-axis so annotations fit.
        ax.set_xlim(0, max_recovery * 1.6)
    else:
        ax.set_xlim(0, 1)
    ax.grid(axis="x", linestyle=":", linewidth=0.5, alpha=0.6)

    caption = f"{n_recovered} of {n_rules} fired rules has recovery > 0"
    fig.text(0.5, -0.02, caption, ha="center", fontsize=8, style="italic")

    fig.tight_layout()
    paths = save_figure(fig, out_dir, "fig-r3-recovery", dpi)
    log_emit("fig-r3-recovery", paths)


# ---------------------------------------------------------------------------
# Conditional figures
# ---------------------------------------------------------------------------

def build_tier_ablation(data: dict[str, Any], out_dir: Path, dpi: int) -> None:
    """Bar chart of e2e geomean by tier; needs stage4.combined_tiers."""
    stage4 = data.get("stage4") or {}
    tiers = stage4.get("combined_tiers")
    if not tiers:
        log_skip("fig-tier-ablation", "stage4.combined_tiers is null/empty")
        return

    # Expect a dict mapping tier name -> {"geomean_pct": float, "ci_low": float, "ci_high": float}
    # or a list of dicts with "name" / "geomean_pct" fields. Be defensive.
    tier_items: list[tuple[str, float, float | None, float | None]]
    if isinstance(tiers, dict):
        tier_items = []
        for name, payload in tiers.items():
            if not isinstance(payload, dict):
                continue
            gm = payload.get("geomean_pct")
            if gm is None:
                continue
            tier_items.append((name, gm, payload.get("ci_low"), payload.get("ci_high")))
    elif isinstance(tiers, list):
        tier_items = []
        for entry in tiers:
            if not isinstance(entry, dict):
                continue
            name = entry.get("name")
            gm = entry.get("geomean_pct")
            if name is None or gm is None:
                continue
            tier_items.append((name, gm, entry.get("ci_low"), entry.get("ci_high")))
    else:
        log_skip("fig-tier-ablation", "stage4.combined_tiers has unexpected shape")
        return

    if not tier_items:
        log_skip("fig-tier-ablation", "stage4.combined_tiers contained no usable entries")
        return

    names = [t[0] for t in tier_items]
    means = [t[1] for t in tier_items]
    err_low = [
        (t[1] - t[2]) if t[2] is not None else 0.0
        for t in tier_items
    ]
    err_high = [
        (t[3] - t[1]) if t[3] is not None else 0.0
        for t in tier_items
    ]

    fig, ax = plt.subplots(figsize=(5.0, 3.0))
    ax.bar(
        names,
        means,
        yerr=[err_low, err_high],
        color="tab:blue",
        edgecolor="black",
        linewidth=0.6,
        capsize=4,
    )
    ax.set_ylabel("e2e geomean delta (%)")
    ax.set_title("Tier-stacked ablation")
    ax.grid(axis="y", linestyle=":", linewidth=0.5, alpha=0.6)
    fig.tight_layout()
    paths = save_figure(fig, out_dir, "fig-tier-ablation", dpi)
    log_emit("fig-tier-ablation", paths)


def build_interaction_matrix(data: dict[str, Any], out_dir: Path, dpi: int) -> None:
    """Heatmap of pairwise rule interactions; needs fdr.n_valid_rules > 0."""
    stage4 = data.get("stage4") or {}
    fdr = stage4.get("fdr") or {}
    n_valid = fdr.get("n_valid_rules") or 0
    if n_valid <= 0:
        log_skip("fig-interaction-matrix", f"fdr.n_valid_rules={n_valid}, no valid rules to plot")
        return

    matrix = stage4.get("interaction_matrix")
    if not matrix:
        log_skip("fig-interaction-matrix", "stage4.interaction_matrix is null/empty")
        return

    # Expect a dict with "labels": [...], "values": [[...], [...]].
    labels = matrix.get("labels") if isinstance(matrix, dict) else None
    values = matrix.get("values") if isinstance(matrix, dict) else None
    if not labels or not values:
        log_skip("fig-interaction-matrix", "interaction_matrix missing labels/values")
        return

    fig, ax = plt.subplots(figsize=(5.0, 4.0))
    im = ax.imshow(values, cmap="RdBu_r", aspect="equal")
    ax.set_xticks(range(len(labels)))
    ax.set_yticks(range(len(labels)))
    ax.set_xticklabels(labels, rotation=45, ha="right", fontsize=8)
    ax.set_yticklabels(labels, fontsize=8)
    fig.colorbar(im, ax=ax, label="interaction effect (%)")
    ax.set_title("Pairwise rule interaction matrix")
    fig.tight_layout()
    paths = save_figure(fig, out_dir, "fig-interaction-matrix", dpi)
    log_emit("fig-interaction-matrix", paths)


def build_prod_vs_shadow(data: dict[str, Any], out_dir: Path, dpi: int) -> None:
    """Scatter of prod_delta vs shadow_predicted across final picks."""
    stage4 = data.get("stage4") or {}
    final_picks_n = stage4.get("final_picks_n") or 0
    if final_picks_n <= 0:
        log_skip("fig-prod-vs-shadow", f"final_picks_n={final_picks_n}, no picks to plot")
        return

    stage5 = data.get("stage5") or {}
    prod = stage5.get("aggregate_prod_delta_pct")
    shadow = stage5.get("aggregate_shadow_predicted_pct")
    per_pick = stage5.get("per_pick_prod_vs_shadow")  # optional richer payload

    if per_pick and isinstance(per_pick, list):
        prod_vals = []
        shadow_vals = []
        names: list[str] = []
        for entry in per_pick:
            if not isinstance(entry, dict):
                continue
            p = entry.get("prod_delta_pct")
            s = entry.get("shadow_predicted_pct")
            if p is None or s is None:
                continue
            prod_vals.append(p)
            shadow_vals.append(s)
            names.append(entry.get("name", ""))
        if not prod_vals:
            log_skip("fig-prod-vs-shadow", "per_pick_prod_vs_shadow had no usable entries")
            return
    elif prod is not None and shadow is not None:
        prod_vals = [prod]
        shadow_vals = [shadow]
        names = ["aggregate"]
    else:
        log_skip("fig-prod-vs-shadow", "no prod/shadow aggregate or per-pick data available")
        return

    fig, ax = plt.subplots(figsize=(5.0, 3.5))
    ax.scatter(shadow_vals, prod_vals, color="tab:blue", edgecolor="black", linewidth=0.5)
    # Diagonal y = x reference line.
    lo = min(min(prod_vals), min(shadow_vals))
    hi = max(max(prod_vals), max(shadow_vals))
    pad = (hi - lo) * 0.1 if hi > lo else 1.0
    ax.plot([lo - pad, hi + pad], [lo - pad, hi + pad], color="tab:gray", linestyle="--", linewidth=0.8)
    for x, y, name in zip(shadow_vals, prod_vals, names):
        if name:
            ax.annotate(name, (x, y), fontsize=7, xytext=(3, 3), textcoords="offset points")
    ax.set_xlabel("shadow predicted delta (%)")
    ax.set_ylabel("prod measured delta (%)")
    ax.set_title("Prod vs shadow per final pick")
    ax.grid(linestyle=":", linewidth=0.5, alpha=0.6)
    fig.tight_layout()
    paths = save_figure(fig, out_dir, "fig-prod-vs-shadow", dpi)
    log_emit("fig-prod-vs-shadow", paths)


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--ablation",
        type=Path,
        required=True,
        help="Path to ablation_data.json (the §S6.0 paper SSOT).",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        required=True,
        help="Directory to write figures into.",
    )
    parser.add_argument(
        "--dpi",
        type=int,
        default=300,
        help="DPI for PNG outputs (default: 300).",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not args.ablation.is_file():
        print(f"[fig] ERROR: ablation file not found: {args.ablation}", file=sys.stderr)
        return 2

    with args.ablation.open("r", encoding="utf-8") as fh:
        data = json.load(fh)

    # Mandatory.
    build_noise_floor(data, args.out_dir, args.dpi)
    build_r3_recovery(data, args.out_dir, args.dpi)

    # Conditional.
    build_tier_ablation(data, args.out_dir, args.dpi)
    build_interaction_matrix(data, args.out_dir, args.dpi)
    build_prod_vs_shadow(data, args.out_dir, args.dpi)

    return 0


if __name__ == "__main__":
    sys.exit(main())
