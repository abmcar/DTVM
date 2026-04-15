# G5 Correctness — Authoritative Pass Rates (spec §4.3)

Extracted from frozen logs (2026-04-15). No regression between HEAD
(`perf/x86-cg-peephole-rules`, peephole ENABLED) and baseline
(`upstream/main`, peephole DISABLED).

## Three Anchors

| Test category                      | HEAD        | Baseline    | Delta |
|------------------------------------|-------------|-------------|-------|
| evmone-unittests multipass (JIT)   | 223/223     | 223/223     | 0     |
| evmone-unittests interpreter       | 215/215     | 215/215     | 0     |
| evmone-statetest `-k fork_Cancun`  | 2723/2723   | 2723/2723   | 0     |

## Source Log Evidence

`correctness-head-20260415.log`:
- L459–460: `223 tests from 1 test suite ran` / `[  PASSED  ] 223 tests.`
- L903–904: `215 tests from 1 test suite ran` / `[  PASSED  ] 215 tests.`
- L6663–6664: `2723 tests from 101 test suites ran` / `[  PASSED  ] 2723 tests.`

`correctness-baseline-20260415.log`:
- L459–460: `223 tests from 1 test suite ran` / `[  PASSED  ] 223 tests.`
- L903–904: `215 tests from 1 test suite ran` / `[  PASSED  ] 215 tests.`
- L6663–6664: `2723 tests from 101 test suites ran` / `[  PASSED  ] 2723 tests.`

## Notes (from frozen summary)

- interpreter run list has 226 lines but gtest de-duplicates to 215 unique cases.
- Cancun filter `-k fork_Cancun` is MANDATORY (excludes ~28 pre-existing Prague failures).
- difftest category: SKIPPED (tools/difftest/ not implemented) — not a primary claim.

## Source Paths (absolute)

- `docs/research/directions/peephole-optimization/submissions/experiments/e3-ablation/correctness-head-20260415.log`
- `docs/research/directions/peephole-optimization/submissions/experiments/e3-ablation/correctness-baseline-20260415.log`
- `docs/research/directions/peephole-optimization/submissions/experiments/e3-ablation/correctness-summary-20260415.txt`

Verdict: HEAD and Baseline identical across all three anchors — no regression.
