# Change: Harden EVM stack-shape analysis before JIT admission

- **Status**: Implemented and correctness-validated
- **Date**: 2026-07-02
- **Tier**: Full
- **Baseline**: main `5d64911`

## Overview

The multipass JIT previously used two whole-module interpreter guards for
contracts with unresolved stack shapes. This change fixes the known lifted-stack
boundary hazards and removes the compatible-dynamic-return guard. It retains the
guard for reachable non-lifted blocks whose required entry stack cannot be
resolved, because the lifted-path fixes do not cover that execution path.

This distinction is required by mainnet replay evidence. With EVM stack SSA lift
disabled, removing this guard made block `21800002`, transaction `46`, construct
the wrong storage key. The replay requested slot
`0xc51e32f45511c94086ad679bd468edb3b2197074a81a0ca3446204c99d92eb57`
instead of
`0xb6a8b75ae122bf609f843157cc59a3fda6034d110435651bb7f2869d45d7817b`.
The exact corrected PR branch, built in Release mode with stack SSA lift
disabled, reached transaction index `47` without an audit violation, the
erroneous key, or `SIGABRT`. It also passed a continuous 100-block replay. The
current implementation therefore sends affected modules to the interpreter
until the JIT can materialize deeper caller frames soundly.

## What changed

### Lifted-stack boundaries

- Materializing exits spill a lifted logical stack from the stack bottom. This
  avoids double-counting a hidden live-in prefix in the recorded stack depth.
- Dynamic exits materialize the runtime stack before indirect dispatch.
- Blocks reachable through the runtime jump table do not use lifted entry
  state. Blocks whose entry depth came from the dynamic-region heuristic also
  remain non-lifted.
- An analyzer/code-generation mismatch raises a compilation error. The caller
  can then use the interpreter instead of terminating a Release process.

The current admission policy relies on these changes while omitting the
compatible-dynamic-return guard. They do not establish that non-lifted
execution with unresolved caller-frame slots is safe.

### Under-resolved stack shapes

After resolving entry depths, the analyzer invalidates any block whose resolved
depth cannot satisfy its own stack pops. Invalidation propagates to statically
reachable successors before entry-shape metadata, liftability, and value-range
analysis are finalized.

This invalidation does not itself reject the whole module. The later
reachable-risk predicate decides whether the unresolved non-lifted shape
requires interpreter fallback.

If range propagation still encounters producer and successor vectors with
different depths, it no longer depends on a Release assertion. The analyzer
disables lifting for the module, discards inferred narrow ranges, and resets
resolved entry slots to `U256`. This prevents unrelated stack slots from being
aligned and used by a narrow-value lowering.

Compiler errors raised while visiting EVM bytecode are propagated to the
existing compile boundary, where the module can fall back to the interpreter.

### Conservative admission for unresolved non-lifted stacks

`ShouldFallbackToInterp` still includes
`hasUnresolvedNonLiftedDeepEntryRisk()`. The predicate is now restricted to
blocks that may be reached from the function entry:

- static successors are followed normally;
- once a reachable unresolved dynamic jump is found, every canonical
  `JUMPDEST` is treated as a possible target, matching indirect-dispatch
  lowering;
- dynamic jumps in dead code do not make unrelated dead blocks reachable.

This removes false fallback caused only by unreachable control flow without
weakening the guard on reachable blocks that require unresolved caller-frame
slots.

### Invalid constant jumps

A known constant `JUMP` or `JUMPI` destination is invalid only when it is not
the original byte position of any valid `JUMPDEST` and therefore cannot be
mapped to a canonical target. Consecutive `JUMPDEST` opcodes remain valid raw
destinations: each byte position in the run maps to the run's canonical block
entry. For `JUMPI`, an invalid constant makes only the taken edge a
bad-destination path; the fallthrough remains reachable.

Such a constant is not classified as an unknown dynamic jump, including when it
has non-zero high limbs. Only an unknown target can consult the shared target
map or expand possible dynamic reachability.

## Scope and consequences

The implementation changes the analyzer, bytecode visitor, module admission,
and focused regression tests. It does not change the EVMC API; the runtime
changes preserve the intended EVM semantics by selecting the interpreter when
the JIT lacks a sound stack shape.

The policy is intentionally conservative. A small reachable internal-call
fixture currently matches the interpreter when JIT execution is forced, but
normal admission still selects the interpreter because the fixture contains a
reachable unresolved non-lifted stack shape. One passing fixture does not
establish safety for every deeper caller frame, while the mainnet transaction
above is a counterexample to removing the guard globally.

Reducing this remaining interpreter population requires runtime stack
materialization and reload logic for the non-lifted path. The admission
predicate must not be relaxed from bytecode shape alone.

## Verification

The current branch was rebuilt and tested in Release mode with EVM stack SSA
lift disabled and enabled. Each configuration ran the same focused set:

| Test binary | Lift disabled | Lift enabled |
|---|---:|---:|
| `evmJitFrontendTests` | 33/33 | 33/33 |
| `evmRangeAnalyzerTests` | 50/50 | 50/50 |
| `evmDifferentialTests` | 49/49 | 49/49 |
| **Total** | **132/132** | **132/132** |

The combined result is **264/264**. The tests cover under-resolved-depth
invalidation, conservative range reset, dead dynamic control flow, invalid
constant jumps, the retained unresolved-stack admission decision, and
interpreter/JIT result equality for the focused bytecode cases.

The exact PR branch also passed the project auto gate:

| Suite | Result |
|---|---:|
| evmone multipass unit tests | 223/223 |
| EEST state tests | 2723/2723 |
| EVM assembly tests | 200/200 |
| CTest targets | 12/12 |

### Exact Lift-OFF mainnet evidence

The mainnet regression occurred with `ZEN_ENABLE_EVM_STACK_SSA_LIFT=OFF`, so
the replay used the exact PR branch at `5d28830` and its Lift-OFF Release VM.
It produced the following results:

- mainnet-replay-v2: 300/300 registered fixture tests;
- continuous replay: 100/100 blocks from `21800000` through `21800099`;
- 14,150 transactions and 1,825,295,016 gas;
- final trusted-tip hash
  `0xaa5e1274f0125381d5a07cd4521b71dd808dd581f46587f8eb82083b1052a812`;
- fresh DB import, header/body prevalidation, storage-access audit, gas,
  receipt-root, and logs-bloom checks;
- per-block and union post-state comparison.

The targeted block `21800002` replay reached transaction index `47`. It did not
report a storage-audit violation, the previous erroneous key, or an unexpected
`SIGABRT`.

Receipts and change sets were persisted; call traces were intentionally
disabled. The state reference was an **offline go-ethereum stateless replay
rooted in a proof-backed parent state**. The result records
`silkworm_checked=false`; it is not an independent Silkworm full-state-root
recomputation.

These correctness runs are not performance data. Their elapsed-time fields are
excluded from performance conclusions.

## Performance scope

This PR makes no performance claim. Earlier
fallback-count and steady-state figures were measured after deleting both
guards, so they describe an unsafe revision and are not valid evidence for this
implementation. In particular, this document no longer carries the earlier
fallback-count, geometric-mean, or per-kernel claims.

The corrected v6 A/B/C protocol evaluates two later compiler-scan
optimizations. Every lane already includes PR 560 and PR 561, so that protocol
does not compare this PR with upstream main and cannot measure this PR's
performance effect. Its results must not be attributed to PR 560. A separate
upstream-main versus exact-corrected-PR protocol would be required for such a
claim.

## Follow-up

- Materialize and reload the non-lifted runtime stack across unresolved
  internal-return continuations, then re-evaluate the unresolved-stack guard.
- Add the lift-enabled configuration to continuous integration so lifted-stack
  boundaries remain covered.
