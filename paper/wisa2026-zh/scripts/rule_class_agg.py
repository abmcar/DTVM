#!/usr/bin/env python3
"""Aggregate peephole rule classification counts for WISA 2026 Tab 3.1 (G3).

Classification heuristic
------------------------
The authoritative rule list is dtvm_rules.json, which has explicit ``level``
(dmir | x86_cgir) and ``class`` fields for every rule.

**dMIR rules** (level == "dmir"): the JSON ``class`` field is used directly.
Observed values: identity, boolean, constant_fold, dead_code, arithmetic.

**x86 CgIR rules** (level == "x86_cgir"): the JSON marks them all as
``dead_code``, but the paper taxonomy requires finer sub-categories.  We
derive the sub-category from the rule *name* using these patterns:

  * ``fallthrough``        -> branch-fallthrough  (unconditional/conditional jump removal)
  * ``fold_setcc``         -> test-setcc-fold     (setcc+test+jne -> jcc fusion)
  * ``redundant_test``     -> redundant-test      (test Rn,Rn after flag-setting insn)
  * ``redundant_cmp``      -> redundant-cmp       (cmp Rn,Rn after flag-setting insn)
  * ``self_move``          -> noop-move           (mov Rn,Rn elimination)
  * ``zero_shift``         -> noop-imm            (shift-by-0 elimination)

No rule is left unclassified; every rule lands in exactly one bucket.
"""

from __future__ import annotations

import csv
import json
import sys
from collections import Counter
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
RULES_JSON = (
    REPO_ROOT
    / "docs/research/directions/peephole-optimization"
    / "submissions/experiments/e1-cav23-overlap/dtvm_rules.json"
)
OUTPUT_CSV = REPO_ROOT / "paper/wisa2026-zh/data/g3-rule-classification.csv"


def classify_x86(name: str) -> str:
    """Derive a fine-grained x86 CgIR category from the rule name."""
    if "fallthrough" in name:
        return "branch-fallthrough"
    if "fold_setcc" in name:
        return "test-setcc-fold"
    if "redundant_test" in name:
        return "redundant-test"
    if "redundant_cmp" in name:
        return "redundant-cmp"
    if "self_move" in name:
        return "noop-move"
    if "zero_shift" in name:
        return "noop-imm"
    return "other"


def main() -> None:
    with open(RULES_JSON, encoding="utf-8") as f:
        data = json.load(f)

    rules = data["rules"]

    # ------------------------------------------------------------------
    # Classify
    # ------------------------------------------------------------------
    dmir_counts: Counter[str] = Counter()
    x86_counts: Counter[str] = Counter()

    for r in rules:
        level = r["level"]
        if level == "dmir":
            dmir_counts[r["class"]] += 1
        elif level == "x86_cgir":
            x86_counts[classify_x86(r["name"])] += 1
        else:
            print(f"WARNING: unknown level '{level}' for rule {r['name']}", file=sys.stderr)

    # ------------------------------------------------------------------
    # Sanity checks
    # ------------------------------------------------------------------
    dmir_total = sum(dmir_counts.values())
    x86_total = sum(x86_counts.values())
    grand_total = dmir_total + x86_total

    expected_dmir = data.get("dmir_rule_count", 70)
    expected_x86 = data.get("x86_cgir_rule_count", 13)

    ok = True
    if dmir_total != expected_dmir:
        print(f"WARN: dMIR count {dmir_total} != expected {expected_dmir}", file=sys.stderr)
        ok = False
    if x86_total != expected_x86:
        print(f"WARN: x86 count {x86_total} != expected {expected_x86}", file=sys.stderr)
        ok = False

    # ------------------------------------------------------------------
    # Write CSV
    # ------------------------------------------------------------------
    OUTPUT_CSV.parent.mkdir(parents=True, exist_ok=True)
    with open(OUTPUT_CSV, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["layer", "category", "count"])

        for cat in sorted(dmir_counts):
            writer.writerow(["dmir", cat, dmir_counts[cat]])
        writer.writerow(["dmir", "TOTAL", dmir_total])

        for cat in sorted(x86_counts):
            writer.writerow(["x86_cgir", cat, x86_counts[cat]])
        writer.writerow(["x86_cgir", "TOTAL", x86_total])

        writer.writerow(["ALL", "GRAND_TOTAL", grand_total])

    # ------------------------------------------------------------------
    # Print summary
    # ------------------------------------------------------------------
    print(f"dMIR:  {dmir_total}  (expected {expected_dmir})")
    for cat in sorted(dmir_counts):
        print(f"  {cat}: {dmir_counts[cat]}")

    print(f"x86:   {x86_total}  (expected {expected_x86})")
    for cat in sorted(x86_counts):
        print(f"  {cat}: {x86_counts[cat]}")

    print(f"Total: {grand_total}")
    print(f"CSV written to {OUTPUT_CSV}")

    if not ok:
        sys.exit(1)


if __name__ == "__main__":
    main()
