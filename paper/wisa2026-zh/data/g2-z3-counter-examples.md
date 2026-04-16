# G2 — Z3 Counter-Examples from Carry-chain Admission Pipeline

## Source

Extracted from E2a experiment data:
- `docs/research/directions/peephole-optimization/submissions/experiments/e2a-synth-stats/data/carry_full_audit_log.json` (78-record per-candidate audit log)
- `docs/research/directions/peephole-optimization/submissions/experiments/e2a-synth-stats/case_studies/case-2.md` (existing case study for Example 1)
- README reject taxonomy (§"Sub-taxonomy of the 61 carry-chain semantic rejects")

Counter-models verified by manual evaluation against the carry-chain semantics:
`adc(x, y, cf) = x + y + cf`, `sbb(x, y, cf) = x - y - cf` (unsigned 64-bit wraparound).

## Selection

61 semantic rejects in carry-chain path (E2a). Two representative classes selected
to illustrate distinct failure modes that hand review would miss:

## Example 1: drop-the-carry (self-cancellation trap)

- **Candidate rule**: `sbb(x, x, cf)` -> `0`
- **Human intuition**: "Subtract a value from itself; result is always zero."
  A reviewer simplifying by hand writes `x - x = 0` and drops the borrow input.
- **Z3 counter-example**: `x = 0x0000000000000000`, `cf = 1`
- **LHS evaluation**: `sbb(0, 0, 1) = 0 - 0 - 1 = 0xFFFFFFFFFFFFFFFF` (unsigned wraparound of -1)
- **RHS evaluation**: `0`
- **Semantic error**: RHS silently discards the borrow input `cf`. When `cf = 1`,
  the true result is the all-ones mask (`2^64 - 1`), not zero. Any downstream
  operation reading this result gets a maximally wrong value. The pipeline correctly
  rejects this as a universal rewrite and only admits it under the guard `cf == 0`.
- **Correct universal form**: `sbb(x, x, cf)` -> `sub(0, cf)` (= `-cf`, i.e. 0 when cf=0,
  all-ones when cf=1). This form is admitted by Z3 as universally valid.
- **Audit log entry**: line 816-825 (`z3_any: "invalid"`, `z3_zero: "valid"`)

## Example 2: wrong polarity (borrow applied to wrong operand)

- **Candidate rule**: `sbb(x, 0, cf)` -> `sub(0, cf)`
- **Human intuition**: "Subtract zero from x with borrow cf; simplify by
  distributing the borrow." A reviewer might factor out `x` (thinking it
  cancels with the zero) and keep only the `-cf` term, producing `sub(0, cf)`.
- **Z3 counter-example**: `x = 0x0000000000000001`, `cf = 0`
- **LHS evaluation**: `sbb(1, 0, 0) = 1 - 0 - 0 = 1`
- **RHS evaluation**: `sub(0, 0) = 0`
- **Semantic error**: RHS replaces `x - cf` with `0 - cf`, effectively swapping
  the minuend from `x` to `0`. The borrow is applied to the wrong base value.
  The rewrite loses the data operand `x` entirely, producing `-cf` instead of
  `x - cf`. This is a polarity/operand confusion: the borrow should subtract
  from `x`, not from a zero constant.
- **Correct universal form**: `sbb(x, 0, cf)` -> `sub(x, cf)` (borrow subtracted
  from the actual data operand). This form is admitted by Z3 as universally valid.
- **Audit log entry**: line 685-694 (`z3_any: "invalid"`, `z3_zero: "invalid"` --
  invalid even under `cf == 0` because the error is in the data operand, not the carry)

## Why these two

| Property | Example 1 | Example 2 |
|----------|-----------|-----------|
| Error class | Drop-carry (cf ignored) | Wrong polarity (operand swap) |
| Looks correct to hand review? | Yes -- "x - x = 0" | Yes -- "subtract 0, keep borrow" |
| Z3 finds it in | carry_any mode | Both modes (carry_any and carry_zero) |
| Counter-model complexity | Minimal (cf=1 suffices) | Minimal (x=1 suffices) |
| Severity | All-ones mask injected silently | Data operand lost entirely |
| Guarded form exists? | Yes (cf==0 guard) | No (broken in both modes) |

The two examples are complementary: Example 1 shows a rule that is salvageable
with a guard (the pipeline recovers the safe guarded form automatically), while
Example 2 is unsalvageable in any mode (the fundamental operand mapping is wrong).

## Paper usage

Section 3.2 will include these as `\paragraph{示例反例。}` with the specific bit patterns.
Recommended presentation: show LHS/RHS as inline dMIR, counter-model as a small table,
one-line explanation of why the rewrite is unsafe.
