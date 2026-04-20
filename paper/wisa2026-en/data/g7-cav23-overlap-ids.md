# G7 — CAV 2023 vs DTVM Rule Overlap (7 rules)

Source: `docs/research/directions/peephole-optimization/submissions/experiments/e1-cav23-overlap/overlap_results.json`

## Summary

7 / 83 DTVM rules (8.4%) overlap with Albert et al. CAV 2023.
All 7 are universal bit-vector / algebraic identities that appear in
any verified peephole framework.

## The 7 overlap rules

| # | DTVM rule ID | CAV'23 rule | Rewrite | Category | Why universal |
|---|---|---|---|---|---|
| 1 | `dmir_add_zero` | `optimize_add_zero` | `add(x, 0)` -> `x` | identity | additive identity |
| 2 | `dmir_mul_one_rhs` | `optimize_mul_one` | `mul(x, 1)` -> `x` | identity | multiplicative identity |
| 3 | `dmir_mul_zero_rhs` | `optimize_mul_zero` | `mul(x, 0)` -> `0` | arithmetic | multiplicative annihilation |
| 4 | `dmir_or_zero` | `optimize_or_zero` | `or(x, 0)` -> `x` | identity | bitwise OR identity |
| 5 | `dmir_double_not` | `optimize_not_not` | `not(not(x))` -> `x` | identity | double negation (involution) |
| 6 | `dmir_sub_self` | `optimize_sub_x_x` | `sub(x, x)` -> `0` | arithmetic | self-cancellation |
| 7 | `dmir_and_factor_lhs` | `optimize_and_and_l` | `and(and(x,y), x)` -> `and(x,y)` | boolean | AND idempotent absorption |

## Footnote text (for S4.3)

dmir_add_zero, dmir_mul_one_rhs, dmir_mul_zero_rhs, dmir_or_zero,
dmir_double_not, dmir_sub_self, dmir_and_factor_lhs
(all universal BV identities; see E1 overlap analysis).

## Paper usage

- S4.3 footnote: comma-separated single line of rule IDs.
- S5.2: "7 rules overlap with CAV 2023; all are universal BV identities" narrative uses this list.
- Verdict: `strong_non_overlap` (8.4% < 30% threshold).
