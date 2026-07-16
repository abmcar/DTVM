# Change: Memoize the MIR verifier operand walk

- **Status**: Implemented
- **Date**: 2026-07-03
- **Tier**: Light

## Overview

`MVisitor::visitInstruction` recurses into every operand of every statement.
The MIR expression graph is a DAG — a value produced once can be an operand of
many later instructions — so a node reachable by K distinct paths is
dispatched K times per traversal. `MVerifier` now opts into a visited-set that
is cleared at the start of each `verify()` call, so verification dispatches
each instruction at most once per run. The general `MVisitor` traversal keeps
its original per-use semantics for visitors whose behavior depends on the
path or use being traversed.

## Motivation

Unsigned 256-bit DIV/MOD lower to a cluster of instructions with shared,
reconvergent sub-expressions: the divide-by-zero guard, the guarded-divisor
select, and the upper-limb test all feed several consumers, and each op both
consumes and feeds the loop-carried accumulator that connects to the next op.
Path count therefore grows combinatorially with the length of a data-dependent
DIV/MOD chain, and `MVerifier::verify()` — the first sub-phase of
`compileMIRToCgIR` — dominates compile time long before any other pass.

Measured on a synthetic dependent-chain loop corpus (cold compile plus first
execution, median of 3 fresh-VM runs, x86-64 Linux, 2026-07-03):

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
DIV/MOD track the SDIV curve. This synthetic chain demonstrates the
pathological traversal complexity and the effect of memoization; it does not
establish a mainnet workload benefit. Mainnet replay A/B measurements are
still required.

## Impact

- Modules: `src/compiler/mir/pass/verifier.cpp`,
  `src/compiler/mir/pass/verifier.h`,
  `src/compiler/mir/pass/visitor.h`, and
  `src/tests/evm_jit_frontend_tests.cpp`.
- Memoization is local to `MVerifier`; other `MVisitor` subclasses retain
  per-use traversal.
- Every verifier instruction check is a function of the node itself (operand
  type kinds, predicate ranges, callee signatures, and block-target indices)
  plus per-function state (`CurFunc`). No instruction check depends on the path
  by which a node is reached, so visiting each instruction once preserves
  those checks. Block-level checks still run in `visitBasicBlock` and are
  unaffected.
- The `DenseSet` hashes and inserts every distinct instruction. That work can
  add overhead on small or tree-like MIR where repeated traversal was already
  cheap, so typical-workload impact must be measured rather than inferred from
  the synthetic chain.
- Code generation is untouched. The previously measured synthetic corpus kept
  gas byte-identical and showed no steady-state throughput change within
  run-to-run noise.
- Scope: `compileMIRToCgIR` verifies MIR for both the EVM and the WASM
  multipass pipelines, so this change affects WASM compile time as well. The
  soundness argument above is frontend-agnostic (every check is node-local
  over the same `MInstruction` kinds); the empirical validation below is
  EVM-side, and CI's WASM jobs cover the shared path.

## Tests

- `cmake --build build --target ircompiler compiler evmJitFrontendTests
  evmMirVerifierTests -j2`: pass.
- `build/evmMirVerifierTests
  --gtest_filter=MIRVerifierTest.SharedDagMemoizationIsVerifierLocalAndResetsPerVerify`:
  pass. The test verifies that:
  - a shared invalid node reached through a diamond is checked once per
    `verify()` call;
  - the same verifier clears its visited-set between consecutive `verify()`
    calls;
  - the invalid shared node is still diagnosed;
  - a shared invalid load index, reached only through the verifier's auxiliary
    operand path, is also diagnosed once per `verify()` call;
  - shared invalid store indices and WASM memory-check bases are each
    diagnosed once per `verify()` call, covering every auxiliary operand edge
    changed to enter the memoizing override;
  - a plain `MVisitor` still visits the shared node once per use.
- `rg 'MVisitor::visitInstruction' src/compiler/mir/pass/verifier.cpp`: no
  matches. Load/store indices and WASM memory-check bases enter the verifier's
  memoizing override instead of bypassing it.
- `build/evmJitFrontendTests`: 33 tests passed after the verifier regression
  moved to its own target; `build/evmMirVerifierTests`: 1 test passed.
- GCC 11.5, Release, virtual stack enabled, target-only ASan: the pre-split
  test layout reproduced CI's `use-after-poison` in
  `MBasicBlock::addStatement`; the split targets pass 2/2 with the verifier
  target still compiled and linked with ASan. GCC 12.5 normal Release and the
  equivalent ASan configuration also pass 2/2. The failure was a mixed
  instrumentation weak-inline/COMDAT selection in the test executable, not a
  production `finalizeEVMBase` failure; the const-fold harness finalization is
  unchanged.
- `ctest --test-dir build --output-on-failure -j2`: all 13 registered test
  targets passed after generating the EVM assembly and Solidity fixtures used
  by the complete local suite.
- `tests/mir`: all 775 `RUN` lines passed with the built `ircompiler` and
  `FileCheck`. The environment did not provide the `lit` executable, so the
  same `ircompiler | FileCheck` commands declared by the test files were run
  directly.
- `clang-format --dry-run -style=file -Werror` on the changed C++ files and
  `cmake-format --check` on `src/tests/CMakeLists.txt`: pass.
- `git diff --check`: pass.
- `tools/format.sh check`: blocked by existing formatting violations in
  unmodified files; none of the reported paths are changed by this work.
- Earlier branch validation:
  - `evmone-unittests` multipass run list: pass.
  - EEST `state_tests` through `evmone-statetest -k fork_Cancun`: pass.
  - `tests/evm_asm`: 211 total, 198 passed, 13 skipped, 0 failed, matching the
    unpatched baseline.

## Checklist

- [x] Implementation complete
- [x] Tests added/updated
- [ ] Module specs in `docs/modules/` updated (if affected) — not affected
- [x] Build and tests pass
