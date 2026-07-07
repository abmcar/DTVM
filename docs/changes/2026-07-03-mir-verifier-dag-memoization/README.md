# Change: Memoize the MIR verifier operand walk

- **Status**: Proposed
- **Date**: 2026-07-03
- **Tier**: Light

## Overview

`MVisitor::visitInstruction` recurses into every operand of every statement
with no visited-set. The MIR expression graph is a DAG — a value produced once
can be an operand of many later instructions — so a node reachable by K
distinct paths is dispatched K times per `verify()`. This change adds a
per-`visit()` visited-set so each instruction is dispatched at most once,
turning the walk from O(paths) into O(distinct nodes).

## Motivation

Unsigned 256-bit DIV/MOD lower to a cluster of instructions with shared,
reconvergent sub-expressions: the divide-by-zero guard, the guarded-divisor
select, and the upper-limb test all feed several consumers, and each op both
consumes and feeds the loop-carried accumulator that connects to the next op.
Path count therefore grows combinatorially with the length of a data-dependent
DIV/MOD chain, and `MVerifier::verify()` — the first sub-phase of
`compileMIRToCgIR` — dominates compile time long before any other pass.

Measured on a dependent-chain loop corpus (cold compile plus first execution,
median of 3 fresh-VM runs, x86-64 Linux, 2026-07-03):

| chain depth | DIV before | DIV after | MOD before | MOD after | SDIV (control) |
|---|---|---|---|---|---|
| 8  | 3.3 ms | 3.2 ms | — | — | — |
| 12 | 17.7 ms | 4.3 ms | — | — | — |
| 16 | 791.6 ms | 5.4 ms | 795.0 ms | 5.6 ms | 8.9 ms |
| 20 | not practical (multi-second) | 6.5 ms | — | — | — |
| 32 | not practical | 10.0 ms | — | — | — |

![MIR-verifier compile time vs dependent-chain depth: before super-linear, after linear](figures/compile-scaling.svg)

*Figure: MIR-verifier compile time vs dependent-chain depth (lower is better,
log-y). Before the fix, DIV compile time grows super-linearly (3.3 → 17.7 →
791.6 ms, then multi-second at depth 20 and impractical at depth 32 — no finite
value recorded, shown as an off-scale break). Memoizing the walk makes it linear
(3.2 → 10.0 ms across depths 8–32), tracking the already-linear SDIV control.
Because a straight line on a log-y axis would mean exponential growth, the
after-curve's linearity is shown in the linear-axes inset. Median of 3 fresh-VM
runs; gas byte-identical, steady-state throughput unchanged.*

Signed SDIV/SMOD lower to a straight-line runtime call with no reconvergent
DAG, which is why they stay linear on the unfixed baseline; after the fix,
DIV/MOD track the SDIV curve.

## Impact

- Module: `src/compiler/mir/pass` (`visitor.h` only, +22 lines).
- `MVerifier` is the only `MVisitor` subclass. Every check it performs is a
  function of the node itself (operand type kinds, predicate ranges, callee
  signatures, block-target indices) plus per-function state (`CurFunc`); no
  check depends on `CurBB` or on the path by which a node is reached, so
  visiting each node once preserves every check. Block-level checks (phi
  grouping, terminator placement) run in `visitBasicBlock` before the operand
  walk and are unaffected.
- The verifier is the only consumer of the walk; codegen is untouched. Gas is
  byte-identical and steady-state throughput is unchanged on the same corpus
  (within run-to-run noise).
- Scope: `compileMIRToCgIR` verifies MIR for both the EVM and the WASM
  multipass pipelines, so this change affects WASM compile time as well. The
  soundness argument above is frontend-agnostic (every check is node-local
  over the same `MInstruction` kinds); the empirical validation below is
  EVM-side, and CI's WASM jobs cover the shared path.

## Tests

- `evmone-unittests` (multipass run list): pass.
- `evmone-statetest` over the EEST `state_tests` corpus (`-k fork_Cancun`): pass.
- `tests/evm_asm` full suite: 211 total, 198 passed, 13 skipped, 0 failed —
  identical counts to the unpatched baseline build.
- `tools/format.sh check` clean; 0 new compiler warnings.

## Checklist

- [x] Implementation complete
- [ ] Tests added/updated — behavior is compile-time only; covered by the
  existing suites above, no new unit test retained
- [ ] Module specs in `docs/modules/` updated (if affected) — not affected
- [x] Build and tests pass
