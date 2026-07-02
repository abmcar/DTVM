# Change: Remove the EVM multipass whole-module interpreter fallback by making SSA-lift dynamic boundaries sound

- **Status**: Proposed (design approved in review session 2026-07-02)
- **Date**: 2026-07-02
- **Tier**: Full
- **Baseline**: main HEAD `5d64911`

## Overview

Real Solidity contracts almost never execute through the multipass JIT today:
26/28 real deployed contracts (92.9%) and 7/8 evmone benchmark kernels are
routed to the interpreter by the whole-module flag `ShouldFallbackToInterp`
(`src/runtime/evm_module.cpp:109-112`). The two structural disjuncts that
cause this — `hasUnresolvedNonLiftedDeepEntryRisk` and
`hasUnresolvedCompatibleDynamicReturnTrampoline` — exist to protect an SSA
stack-lifting optimization (`ZEN_ENABLE_EVM_STACK_SSA_LIFT`) that skips
runtime-stack materialization at some lifted-block exits. This change fixes
the underlying unsoundness at the lifted/non-lifted dynamic boundary and then
deletes both guards, in all build configurations. Expected effect: the ~93%
structural fallback disappears; every such contract JIT-compiles.

## Motivation — measured evidence (all from this session, at HEAD 5d64911)

1. **Fallback rate.** 26/28 real contracts (Sourcify corpus) and 7/8 kernels
   set `ShouldFallbackToInterp=1` at load. Signal: fallback modules skip eager
   compile (`evm_module.cpp:123-128`), so the `--enable-statistics`
   "JIT Compilation" line is absent; cross-checked with gdb breakpoints on
   `BaseInterpreter::interpret` vs `Runtime::callEVMInJITMode`.
2. **Attribution.** gdb printing of the three disjuncts per module: size
   thresholds fired on **0/33** fallback modules; deep-entry on 32/33;
   trampoline on 6/33 (5 jointly with deep-entry, 1 alone).
3. **Root cause shape.** Internal-function returns (`JUMP` to a stack-passed
   return PC) are unresolvable by every per-block abstract-stack pass in the
   tree (`resolveJumpTargetsByAbstractStack`, `src/evm/evm_cache.cpp:1362-1474`, and
   the analyzer's local pass, `evm_analyzer.h:707-895`, are the same
   algorithm; the return PC crosses block boundaries on the stack). Return
   continuations therefore keep `ResolvedEntryStackDepth < 0`, invalidation
   cascades along the static CFG, and the deep-entry predicate fires. A
   39-byte minimal reproducer exists (internal call + continuation whose
   static successor reads 2 stack slots).
4. **Default build (lift OFF): guards protect nothing.** With
   `ShouldFallbackToInterp` force-cleared in gdb (Debug build, asserts on),
   all 33 fallback modules — 52 (contract, calldata) runs + 7 kernels with
   real benchmark inputs — JIT-compiled and produced byte-identical output
   and exit status to the interpreter. Non-lifted codegen is structurally
   depth-free: `ResolvedEntryStackDepth` has zero uses in
   `evm_mir_compiler.*` and only lifted-path uses in the visitor.
5. **Lift-ON build: guards protect a real but rare hazard, incompletely.**
   Same probe under `ZEN_ENABLE_EVM_STACK_SSA_LIFT=ON`: 58/59 runs
   byte-identical; **1 divergence** (contract `0x9649BF5d…`, selector
   `0x42966c68`: JIT takes a wrong branch into an empty `REVERT` via
   `evmSetRevert(0,0)` while the interpreter succeeds) — the stale-stack
   miscompile class the guards exist for. Three statically-verified coverage
   gaps show the guards do not fully close that class even today (see
   Findings 3).

## Findings — the hazard mechanism (adversarially re-verified)

1. **Skip sites.** `finalizeBlockExit(Values, Materialize)` is the single
   exit primitive (`evm_bytecode_visitor.h:872-888`); `Materialize=false`
   writes nothing to the runtime stack. Safe skips (statically wired
   lifted→lifted edges): constant JUMP (`:951-963`), fallthrough
   (`:752, :965-973`), dual-static JUMPI (`:1720-1724`), already-begun entry
   (`:740-751`). Unsafe skips: **(S5)** dynamic JUMP skips whenever the
   compatible-dynamic target set is non-empty (`:1676-1686`) while SSA entry
   state is assigned only to *lifted* set members (`:913-923`); **(S7)**
   dynamic-dest JUMPI decides the skip *only* from fallthrough liftability
   (`:1737-1742`), ignoring the taken edge entirely.
2. **No runtime enforcement.** Dynamic dispatch is a switch wired to every
   JUMPDEST (`evm_mir_compiler.cpp:303-430`, table from a raw bytecode scan,
   `:1356-1440`), with no depth or membership check; lifted blocks also skip
   the runtime stack check (`evm_bytecode_visitor.h:1068-1071`). Soundness
   rests entirely on the analyzer's unproven assumption that a class-X
   dynamic jump lands only on class-X targets.
3. **Guard coverage gaps (static findings).** (a) The trampoline guard skips
   class-0 sources (`evm_module.cpp:33-36`) although the fallback branch of
   `getCompatibleDynamicJumpTargetBlocksForSourceBlock`
   (`evm_analyzer.h:359-376`) can still hand such a source a non-empty set
   that triggers the visitor skip. (b) The dynamic-JUMPI skip (S7) is
   invisible to the trampoline guard and silent for the deep-entry guard
   when region targets are depth-resolved but shape-incompatible. (c) An
   out-of-class runtime target that is depth-resolved escapes both guards.
4. **Lifted-target residual.** A lifted block entered through the jump table
   only restores SSA entry state (`evm_bytecode_visitor.h:1073-1079`); edges
   whose values were never assigned yield null phi incomings
   (`evm_mir_compiler.cpp:1149-1152`, never repaired, `:228-232`) or
   never-assigned entry variables. The completeness predicate
   `hasCompleteEntryState` (`evm_lifted_stack_lifter.h:158-180`) has zero
   callers. A static edge from a non-lifted predecessor with unresolved exit
   depth into a lifted successor is silently dropped
   (`evm_bytecode_visitor.h:940-948`); no liftability rule checks static
   predecessors' resolvedness.
5. **Prerequisite bug (live today).** A materializing exit of a lifted block
   with `HiddenLiveInPrefixDepth > 0` spills the full logical stack at byte
   offset `Hidden*32` and sets `StackSize = Hidden + size` while the lifted
   logical stack already spans the full absolute depth
   (`FullEntryStateDepth = ResolvedEntryStackDepth`, `evm_analyzer.h:1445`;
   spill at `evm_mir_compiler.cpp:1221-1255`). Top-relative contents stay
   consistent but the recorded depth is inflated by `Hidden`: spurious
   overflow traps near the 1024 limit, missed underflow traps. Reachable
   today via a constant jump from a `Hidden>0` lifted block to a non-lifted
   successor; Stage 1 multiplies the trigger sites, so this must land first.
6. **Guard/codegen analyzer input mismatch.** The load-time guard analyzer
   never receives `ResolvedJumpTargets` (`evm_module.cpp:106-108`) while the
   codegen analyzer does (`evm_compiler.cpp:100`), so the guards certify a
   different region model than the one driving the skips. Deleting the
   guards (Stage 3) removes the mismatch.
7. **Under-resolved entry depth at return-continuation successors (root cause
   of the recovered lift-ON wallet miscompile).** `propagateEntryDepths`
   (`evm_analyzer.h:962-991`) flows entry depths only along static edges from
   the function entry. An internal-function return continuation — a JUMPDEST
   reached solely by a dynamic return JUMP — receives its entry depth from the
   dynamic-jump region uniform-entry heuristic, which counts only the slots
   visible at the return and under-counts the caller's frame. The shortfall
   carries into every static successor. A successor can then hold
   `ResolvedEntryStackDepth + MinStackHeight < 0`: it pops below its own
   resolved bottom, which is impossible for a correctly resolved depth. When
   such a block is lifted its logical entry state is sized to the too-shallow
   depth and `validateLiftedBlockStackBounds`
   (`evm_bytecode_visitor.h:1016-1041`) emits a spurious `EVMStackUnderflow`,
   reverting the call. Observed on stWalletTest setDailyLimit: continuation
   JUMPDEST PC1791 (`ResolvedEntryStackDepth=2, HiddenLiveInPrefixDepth=1`)
   feeds its lifted fallthrough PC1797 (`ResolvedEntryStackDepth=1,
   MinStackHeight=-3`); four fork_Cancun wallet tests diverged (empty
   `logs_hash`, wrong `state_root`) once the guards were removed. The existing
   liftability clauses miss it: PC1797 is not a dynamic-jump target candidate,
   its predecessor's `ResolvedExitStackDepth` is `>= 0` (consistently wrong,
   not unresolved), and its `HiddenLiveInPrefixDepth` is 0, so the
   hidden-prefix fixpoint never inspects it. The non-lifted path is unaffected:
   it reads the real (deeper) runtime stack via the `-MinStackHeight` stack
   check (`evm_bytecode_visitor.h:1072-1074`).
8. **Residual channel (a) surfaces as a verifier fallback, not a miscompile.**
   After guard removal, modules that hit the null-phi-incoming class of
   Finding 4 (a lifted merge whose `ExpectedIncomingCount` exceeds its actual
   MIR predecessor edges, leaving `<pending>` phi slots;
   `evm_lifted_stack_lifter.h:158-180` `hasCompleteEntryState` still has zero
   callers) do not miscompile: the always-on `MVerifier`
   (`compiler.cpp:66-69`) runs before code generation, rejects the invalid MIR
   ("The number of phi incoming values must match predecessor count",
   `verifier.cpp:182-184`), and the EVM compile path catches the throw and
   falls back to the interpreter, so results stay byte-identical (full EEST
   passes). This class is orthogonal to Finding 7, pre-exists the depth fix
   (identical verify-fallback counts with and without it), and is fallback-safe
   rather than unsound. Making these modules actually JIT-compile is the
   documented follow-up (runtime depth-check + reload machinery).

## Design

Principle: make the runtime stack valid at every edge that can reach a block
which reads it, and lift only blocks whose every entry edge provably carries
assigned SSA state. Materialization is additive to SSA assignment (the JUMPI
partial path already does both, `evm_bytecode_visitor.h:1728-1742`), so
lifted successors keep zero-reload SSA entry on static edges.

### Stage 0 — fix the hidden-prefix spill depth inflation

Under the full-absolute-depth entry-state model the correct spill prefix for
a lifted block is 0. Fix `spillTrackedStackPreservingPrefix` usage at
`evm_bytecode_visitor.h:875-878` accordingly. Add a regression test that
executes a `Hidden>0` lifted block with a materializing exit near the stack
limit and asserts no spurious overflow/underflow behavior versus the
interpreter.

### Stage 1 — always materialize at dynamic exits

Force `NeedsRuntimeMaterialization = true` for any dynamic (non-constant)
dest at the two exit decision points: dynamic JUMP
(`evm_bytecode_visitor.h:1682-1686`) and the `NeedsRuntimeMaterialization`
computation in the lifted JUMPI else-branch (`:1726-1742`, which today
consults only the fallthrough when the dest is dynamic). Keep the SSA
entry-state assignment calls. This closes the stale-runtime-stack reads of
*non-lifted* dynamic targets only; lifted targets are closed by Stage 2.
Statically wired lifted→lifted skip sites are not modified, but note that on
the recovered population (any module with an unknown dynamic jump) Stage 2
unlifts every JUMPDEST, so those skips keep firing only on fallthrough-chain
interiors there — cross-block SSA residency on such contracts is largely
forfeited (see Risks).

### Stage 2 — lift only provably-safe blocks

- Exclude jump-table-enterable JUMPDESTs from lifting (today the
  dispatch-coverage revocation in `finalizeLiftability`
  (`evm_analyzer.h:1435-1438`) already unlifts almost all of them on real
  contracts; make the exclusion unconditional for dynamically enterable
  blocks).
- **The exclusion condition must equal codegen's actual switch-emission
  set.** Codegen emits the indirect dispatch iff the JUMP/JUMPI dest MIR
  operand is non-constant (`evm_mir_compiler.cpp:1534/1563` and
  `:1624/1666`); the analyzer's candidate marking keys on
  `HasUnknownDynamicJump` (`evm_analyzer.h:1206-1208`). The two coincide
  only if visitor-non-constant implies analyzer-dynamic. That direction is
  expected to hold — the analyzer's resolution (local pass and shared cache
  are the same block-local algorithm, `src/evm/evm_cache.cpp:1362-1474` /
  `evm_analyzer.h:707-895`) tracks a subset of the visitor's own constant
  propagation — but it is enforced, not assumed: add a check in the
  switch-emission path asserting the analyzer marked the jump dynamic (and
  hence every JUMPDEST was excluded from lifting). A violated assertion
  fails compilation instead of miscompiling.
- Add a liftability clause: a block is not lifted if any static predecessor
  has unresolved exit depth (closes the silently-dropped-assignment hole,
  `evm_bytecode_visitor.h:940-948`).
- Add a liftability clause: a block is not lifted when
  `ResolvedEntryStackDepth + MinStackHeight < 0` (`finalizeLiftability`,
  `evm_analyzer.h`). This closes Finding 7: the sum is the local proof that
  the resolved entry depth is under-resolved (or the block genuinely
  underflows). In both cases the lifted logical entry state cannot cover the
  block's pops, and the non-lifted runtime stack check handles the real depth,
  so unlifting is behaviorally neutral (a genuine underflow still traps via
  the runtime check).
- Resurrect `hasCompleteEntryState` as a debug assertion at lifted block
  begin.

### Stage 3 — delete the structural fallback guards

Remove `hasUnresolvedCompatibleDynamicReturnTrampoline` and
`hasUnresolvedNonLiftedDeepEntryRisk` from the `ShouldFallbackToInterp`
computation (`evm_module.cpp:109-112`) in all configurations. Keep the size
and complexity thresholds (`evm_analyzer.h:126-130,667-672`) — they bound
compile cost and are orthogonal to lifting.

### Stage 4 — correctness gates (each stage, both build configs)

Note on staging: until Stage 3 removes the guards, the recovered population
still sets `ShouldFallbackToInterp=1`, so an unforced run never exercises
the new codegen on it. Stages 0-2 are therefore gated with the forced-JIT
probe protocol (Evidence appendix: gdb clears the flag), and Stage 3 is
gated without forcing.

- `evmDifferentialTests` (ctest) green, lift-ON and lift-OFF builds.
- EEST state tests + evm_asm via `tools/dtvm_local_test.sh --auto`.
- Corpus differential: 26 contracts × 2 calldata + 7 kernels with benchmark
  inputs, interp vs multipass output and status equality, lift-ON and
  lift-OFF (forced at Stages 0-2, unforced after Stage 3).
- Red test: contract `0x9649BF5d…` with selector `0x42966c68` +192 zero
  bytes must produce interpreter-identical output on the lift-ON build
  (diverges today when the guard is bypassed; must pass with guards removed).
- The 39-byte deep-entry reproducer becomes a unit differential test
  asserting `JITCompiled == true` plus output equality.
- Targeted differential test for the analyzer/codegen dispatch consistency:
  a module whose jump is resolved by the shared cache while a lifted
  JUMPDEST exists, exercising the Stage 2 assertion path.
- Structural invariant assertions (debug builds), checked on every compiled
  module rather than only on tested inputs: (a) `CanLiftStack` implies
  `!IsDynamicJumpTargetCandidate` after Stage 2; (b) every static
  predecessor of a lifted block has `ResolvedExitStackDepth >= 0` equal to
  the successor's `FullEntryStateDepth`; (c) `hasCompleteEntryState` holds
  at every begun lifted block (the Stage 2 assertion).
- CREATE initcode / transient modules load under a scoped `InterpMode`
  config (`dt_evmc_vm.cpp:464-479`), never run the analyzer or the guards,
  and are unaffected; EEST covers them regardless.
- Recommendation (separate follow-up, not this change): add a lift-ON entry
  to the CI matrix — lift-ON is the only configuration that can miscompile
  here and CI currently never builds it.

### Stage 5 — measurement

Before/after `evmone-bench` (main + micro suites, `libdtvmapi.so`,
back-to-back, ≥10 repetitions, identical flags, both sides same protocol) in
both configurations, plus the real-contract corpus. Then the u64 fast-path
evaluation on real contracts proceeds on the recovered population: range
analysis and all its lowering consumers are active in the default build
(zero `#ifdef ZEN_ENABLE_EVM_STACK_SSA_LIFT` outside the lifter); recovered
blocks get in-block narrowing and resolved-static-edge narrowing, while
unresolved-entry blocks keep U256 entry ranges (seeding skipped at
`evm_analyzer.h:1963-1965`; the U256 default is realized at the consumer,
`evm_bytecode_visitor.h:1090-1098`) — extending that is follow-up work, not
part of this change.

## Stage 5 results (measured 2026-07-03, branch 2c7a135 vs base 5d64911)

Protocol: byte-identical CMake flags (cache-line diff empty), lift-OFF
production configuration, back-to-back runs in one shell session with
recorded timestamps, 12 repetitions, median and IQR reported, JIT compile
time excluded (evmone-bench times only the post-warmup steady state). Raw
JSON under `bench-results/` in this worktree (untracked).

- evmone main suite, 14 cases: geometric-mean execution time **−65.1%**.
  Six of the seven recovered kernels improve: sha1_divs −93%, sha1_shifts
  −91%, structarray_alloc −75%, blake2b_shifts −69%, swap_math −49% to
  −54%, snailtracer −38%.
- **weierstrudel regresses +19% to +22%** (both inputs, both configs; delta
  is 40-50x either side's IQR — real, not noise). JIT-compiled execution of
  this internal-return-heavy EC contract is slower than the baseline
  interpreter, matching the Risk-section prediction for return-trampoline
  code. Follow-up: profile before claiming a universal win; a per-module
  adaptive policy is possible future work.
- Micro suite (no fallback population): geomean +0.31%, null within noise.
- Control blake2b_huff (JIT on both sides): within ±0.4% everywhere — no
  systematic bias, no unintended codegen change.
- Fallback rate after the change: 0/36 modules (was 33/36) in both
  configurations.

## Impact

### Affected Modules

- `src/action/evm_bytecode_visitor.h` (exit materialization, lift gating)
- `src/compiler/evm_frontend/evm_analyzer.h` (liftability clauses)
- `src/compiler/evm_frontend/evm_mir_compiler.cpp` (spill prefix fix)
- `src/runtime/evm_module.cpp` (guard removal)
- `src/compiler/evm_frontend/evm_lifted_stack_lifter.h` (completeness assert)

### Affected Contracts

No API or interface changes. `ZEN_ENABLE_JIT_PRECOMPILE_FALLBACK` remains a
vestigial no-op (no consumers; not touched here).

### Compatibility

No behavioral change for contracts that JIT today, other than lift-ON
codegen differences validated by the differential gates. Contracts that fell
back now JIT; execution semantics must be byte-identical (enforced by
Stage 4).

## Risks

- **Residual lift-ON miscompile surface**: the corpus differential is finite
  (33 modules, limited calldata). Mitigation: full EEST statetest + spec
  suites on both configs; Stage 2 removes the known unsound entry classes by
  construction, not by testing alone.
- **Perf regression on currently-JIT-able lift-ON modules**: forced
  materialization costs O(absolute stack depth) stores per dynamic exit —
  the internal-return hot path the skip optimized — and cross-dynamic-edge
  SSA residency is forfeited. Mitigation: Stage 5 measures both configs;
  lift-ON is not the production configuration (default OFF, CI never sets
  it), and for the ~93% class the comparison is JIT-with-spills versus pure
  interpreter. A sound re-enable of cross-dynamic-edge SSA (runtime depth
  check + reload at lifted JUMPDEST entries) is possible follow-up work.
- **Hidden-prefix fix interacts with existing lift-ON users**: the depth
  inflation is a live bug today; fixing it changes behavior near the stack
  limit. Mitigation: targeted regression test in Stage 0.

## Follow-up backlog (out of scope here)

- Cross-block return-PC resolution (would resolve continuation entry depths,
  unlocking entry-range seeding and reducing reliance on dynamic dispatch).
- Top-relative K-window entry-range seeding for unresolved-depth blocks
  (extends u64 fast-path coverage across continuation boundaries).
- Runtime depth-check + reload machinery to soundly re-enable lifted
  dynamic-target blocks.
- Cache build runs `resolveJumpTargetsByAbstractStack` twice redundantly
  (`src/evm/evm_cache.cpp:1782-1794`); trivial cleanup.
- Dead code: unreachable branch at `evm_bytecode_visitor.h:755-758`; dead
  PGJ `JITRejected` marking (`dt_evmc_vm.cpp:800-803`, unreachable behind
  the fallback early-return); PGJ background compiles run without SPP gas
  chunk data (`evm_module.cpp:115-119` vs `evm_module.h:121-130` directive).

## Evidence appendix — reproduction commands

Fallback measurement (release build, no source change):

    build/dtvm --format evm -m multipass --enable-statistics \
      --enable-evm-gas --gas-limit 1000000 --calldata 0x <contract.hex>
    # "JIT Compilation" statistics line present <=> module compiled

Attribution + forced probe (Debug builds `build-debug/` lift-OFF,
`build-debug-ssa/` lift-ON; breakpoint after the decision):

    gdb -q -batch \
      -ex 'break src/runtime/evm_module.cpp:123' -ex run \
      -ex 'print Analyzer.getJITSuitability().ShouldFallback' \
      -ex 'print Analyzer.hasUnresolvedNonLiftedDeepEntryRisk()' \
      -ex 'print hasUnresolvedCompatibleDynamicReturnTrampoline(Analyzer)' \
      -ex 'set var Mod->ShouldFallbackToInterp = false' \
      -ex 'break zen::evm::BaseInterpreter::interpret' -ex continue \
      --args <build>/dtvm --format evm -m multipass --enable-evm-gas \
        --gas-limit 1000000 --calldata <cd> <contract.hex>

Corpus: 28 Sourcify contracts under
`.claude/worktrees/measure-u64-fastpath/tests/corpus/evm-cache/raw/`;
kernels from `~/evmone/test/evm-benchmarks/benchmarks/main/*.json`
(`pre[tx.to].code`, `transaction.data[0]`). The lift-ON divergence case:
contract `0x9649BF5dfd35687C679E8B3A5eF832bad61da94A`, calldata
`0x42966c68` + 192 zero hex chars.
