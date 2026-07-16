# Change: Harden EVM stack-shape analysis before JIT admission

- **Status**: Implemented and correctness-validated
- **Date**: 2026-07-02
- **Tier**: Full
- **Original branch base**: main `5d64911`
- **Validation base commit**: `ffc56028f68558e5a812813b0873ed46554909e7`
- **Validated source hashes**: v4 evidence `metadata/source-files.sha256`
- **Merged main parent**: `ce5f36f27f00436d2197e8a284c4ac71c4ee4283`

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
- Once the conservative CFG contains a reachable dynamic jump, every canonical
  `JUMPDEST` and its static-successor closure is marked as possibly receiving
  its absolute entry depth from runtime indirect dispatch. Those blocks remain
  non-lifted even when a separate static predecessor assigned the same block a
  concrete depth. This covers backward and out-of-region dynamic entries whose
  hidden caller frame is not represented by that static depth.
- An analyzer/code-generation mismatch raises a compilation error. The caller
  can then use the interpreter instead of terminating a Release process.

### JUMPDEST fallthrough liveness

The MIR builder creates `PostRevert` and `PostReturn` blocks as insertion
points after terminating instructions. These blocks have no live fallthrough.
The previous `JUMPDEST` handling nevertheless connected such a dead block to the
next canonical destination. This added a CFG predecessor that the analyzer had
not recorded, so shared-entry phi nodes had fewer incoming values than MIR
predecessors. MIR verification then rejected the module and execution fell back
to the interpreter.

The bytecode visitor now passes an explicit `HasLiveFallthrough` value to
`handleJumpDest`. The MIR builder adds the fallthrough edge only when that value
is true. Lifted `JUMPI` fallthroughs that enter a shared `JUMPDEST` remain live
through the existing deferred-entry path.

The current admission policy relies on these changes while omitting the
compatible-dynamic-return guard. They do not establish that non-lifted
execution with unresolved caller-frame slots is safe.

The dynamic-source reachability check is intentionally a conservative CFG
over-approximation. It does not prove a `JUMPI` condition constant before
following both edges. This can disable more Lift-ON stack lifting than runtime
feasibility alone requires, but it cannot make a runtime dynamic entry use an
unproven absolute depth. A fully condition-aware pruning pass is a separate
performance follow-up and must preserve this safety property.

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

The implementation changes the analyzer, bytecode visitor, MIR builder, module
admission, and focused regression tests. It does not change the EVMC API. The
runtime changes preserve the intended EVM semantics by selecting the
interpreter when the JIT lacks a sound stack shape.

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

The validated source snapshot consists of base commit
`ffc56028f68558e5a812813b0873ed46554909e7` plus the seven modified C/C++ files
whose SHA-256 values are recorded in the v4 evidence as
`metadata/source-files.sha256`. Those hashes were unchanged after testing. The
untracked `bench-results/` directory and external EVM fixtures were not
modified. This documentation was updated after the gate and does not affect the
validated binaries.

Two fresh GCC 12 Release builds exercised stack SSA lift disabled and enabled.
Both enabled multipass, spec tests, and precompile fallback. Each configuration
ran the same focused tests:

| Test binary | Lift disabled | Lift enabled |
|---|---:|---:|
| `evmJitFrontendTests` | 45/45 | 45/45 |
| `evmRangeAnalyzerTests` | 50/50 | 50/50 |
| `evmDifferentialTests` | 54/54 | 54/54 |
| **Total** | **149/149** | **149/149** |

The combined result is **298/298**. The tests cover under-resolved-depth
invalidation, conservative range reset, dead dynamic control flow, invalid
constant jumps, backward dynamic-entry depth taint, dead `RETURN` and `REVERT`
fallthrough, shared-entry phi construction, the retained unresolved-stack
admission decision, and interpreter/JIT result equality including gas.

The project local gate produced:

| Suite | Result |
|---|---:|
| evmone multipass unit tests | 223/223 |
| EEST state tests | 2723/2723 |
| EVM assembly tests | 209/209 |
| CTest targets | 12/12 |

`ctest` ran against the worktree's lift-disabled `build/` and passed 12/12.
The lift-enabled configuration separately ran the three JIT-dependent focused
test binaries above. This avoids attributing the lift-disabled `ctest` result
to both configurations.

Both builds completed without compiler errors. Each build emitted 14 compiler
warning lines, with none attributed to a modified source file. The seven
modified C/C++ files pass the repository's clang-format check, and
`git diff --check HEAD` passes. The full-repository `tools/format.sh check`
still returns 123 for pre-existing formatting issues outside this change.

### Mainnet replay evidence

The frozen lift-disabled and lift-enabled VMs have SHA-256 values
`7da5bb68601e88eff2dbbed78c332901fab5f4e4e705e44a8acf98b029420991`
and
`c6896c1b081e8e1ed7cdea4b8a2ca70c625c5a25f2bada01b8eb0c696dddb98b`,
respectively. Each VM passed 300/300 mainnet-replay-v2 Cancun fixtures.

Both VMs also passed the frozen continuous replay of blocks
`21800000..21800099`:

| Result | Lift disabled | Lift enabled |
|---|---:|---:|
| Replay process | exit 0 | exit 0 |
| Frozen validator | PASS | PASS |
| State/proof audit | PASS | PASS |
| Blocks | 100/100 | 100/100 |
| Transactions | 14,150 | 14,150 |
| Gas | 1,825,295,016 | 1,825,295,016 |
| Final block hash | `0xaa5e1274f0125381d5a07cd4521b71dd808dd581f46587f8eb82083b1052a812` | same |

The two results match on every block number, hash, transaction count, and gas
value. Validation covers gas, receipt root, logs bloom, per-block post-state,
proof-union final state, and execution-access state. The authoritative logs
contain zero `Verifying Error`, zero `JIT compilation failed`, and zero
`lift-diag` markers. This confirms that the dead-fallthrough fix removes the
observed phi-verifier failure and its interpreter fallback from this replay
window.

The replay state is an offline stateless state rooted in a proof-backed parent
state. It is not an independent full-node state-root recomputation, and its
MDBX database is not a complete mainnet state database.

## Performance scope

This correctness gate provides no performance conclusion. Its elapsed-time
fields are excluded from performance analysis.

PR 561 and subsequent compiler-scan optimizations require independent,
controlled A/B measurements built from explicitly frozen source snapshots.
Those measurements must use identical VM options, corpora, host controls, and
correctness checks. Synthetic benchmarks, one fixture, or one mainnet
transaction can establish a regression mechanism, but cannot be extrapolated
to aggregate mainnet performance.

## Follow-up

- Materialize and reload the non-lifted runtime stack across unresolved
  internal-return continuations, then re-evaluate the unresolved-stack guard.
- Add condition-aware pruning for provably untaken dynamic-source CFG edges,
  while retaining taint for every runtime-feasible indirect entry.
- Add the lift-enabled configuration to continuous integration so lifted-stack
  boundaries remain covered.
