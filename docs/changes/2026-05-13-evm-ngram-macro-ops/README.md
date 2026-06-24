# Change: implement initial EVM n-gram macro-ops and keccak helpers

- **Status**: Implemented
- **Date**: 2026-05-13
- **Tier**: Full

## Overview

Implement a first production-scoped slice of the `docs/research/n-gram`
design for the EVM multipass JIT. The first version adds frontend-recognized
macro-op style lowering for hot constant-control-flow and address-generation
patterns, plus specialized runtime helpers for stable two-word `KECCAK256`
staging patterns used by common mapping access paths.

The change is intentionally narrower than the research draft. It does not
introduce a general region-JIT deoptimization framework, speculative branch
deopt, or a standalone stack materialization ABI. Instead, it reuses the
existing lifted-stack and runtime-helper infrastructure already present in the
EVM frontend.

## Motivation

The current EVM multipass JIT already has conservative stack lifting,
constant-jump handling, memory precheck planning, and runtime helper calls for
expensive operations such as `KECCAK256`. However, several hot opcode n-grams
still lower as independent stack-machine operations even when their bytecode
shape is stable and their semantics are already visible at compile time.

The research draft in `docs/research/n-gram` identifies three profitable
clusters:

1. Constant control-flow templates such as `PUSHk imm ; JUMP`,
   `PUSHk imm ; JUMPI`, and `ISZERO ; PUSHk imm ; JUMPI`
2. Address-generation and staging templates such as `DUPn ; ADD`,
   `PUSHk imm ; DUPn ; ADD`, and `... ; MSTORE`
3. Two-word `KECCAK256` preparation patterns for mapping slot derivation

These patterns are frequent in ERC20-style contracts, routers, and ABI-heavy
paths. Lowering them as grouped operations in the EVM frontend should reduce
redundant virtual stack traffic, avoid unnecessary generic control-flow
handling, and trim helper overhead around common `KECCAK256` preparation code.

## Impact

### Affected Modules

- `src/action/` - EVM bytecode visitor pattern recognition and block-level
  state transfer
- `src/compiler/evm_frontend/` - macro-op aware lowering, helper call sites,
  operand/state normalization
- `src/compiler/evm_frontend/evm_imported.*` - new specialized keccak helpers
  and runtime function table entries
- `src/tests/` and `tests/` - frontend unit tests and EVM behavioral coverage

### Affected Contracts

No external API or user-visible interface changes. The optimization is
internal to the multipass EVM JIT pipeline.

### Compatibility

Backwards compatible. Unsupported or partially matched bytecode shapes must
continue to lower through the existing generic EVM frontend path.
Non-multipass execution modes are unaffected.

## Implementation Plan

### Phase 1: scope V1 around existing frontend/runtime infrastructure

- [x] Freeze V1 scope to the subset that fits the existing lifted-stack and
      helper-call model
- [x] Explicitly defer `helper_branch_deopt`, `helper_materialize_stack`,
      `helper_memgrow_slowpath`, `MO_U256_SHIFT_MASK`, and
      `MO_U256_FIELD_PACK`
- [x] Document that V1 is frontend pattern grouping, not a new speculative
      region-JIT ABI

### Phase 2: frontend control-flow macro-ops

- [x] Add bytecode visitor pattern recognition for `PUSHk imm ; JUMP`
- [x] Add bytecode visitor pattern recognition for `PUSHk imm ; JUMPI`
- [x] Add bytecode visitor pattern recognition for `ISZERO ; PUSHk imm ; JUMPI`
- [x] Route matched forms through dedicated builder entry points that reuse
      existing constant-jump validation, canonical jumpdest resolution, and
      lifted-block entry-state transfer
- [x] Keep non-matching or non-provable cases on the current generic
      `handleJump` / `handleJumpI` path

### Phase 3: address-generation and staging fusion in the EVM frontend

- [x] Introduce builder helpers for grouped lowering of `DUPn ; ADD` and
      `PUSHk imm ; DUPn ; ADD`
- [x] Add visitor-level grouped lowering for `PUSHk imm ; ADD`
- [x] Add conservative visitor-level `MSTORE` motifs for `PUSHk imm ; MSTORE`,
      `ADD ; MSTORE`, and `DUP1 ; DUP1 ; MSTORE ; DUP2 ; ADD`
- [x] Fuse stable `... ; MSTORE` staging shapes only when stack effects and
      memory checks remain identical to the generic path
- [x] Reuse existing memory compile block precheck planning instead of
      introducing a separate slowpath helper ABI

### Phase 4: specialized keccak helpers

- [x] Add a generic `helper_keccak_2word(a, b)` style runtime entry for 64-byte
      two-word hashing
- [x] Add specialized mapping helpers for `calldata key + const slot` and
      `caller + const slot` when the bytecode template is stable
- [x] Match helper-eligible staging patterns before lowering to generic
      `MSTORE/MSTORE/KECCAK256`
- [x] Fall back to the existing `GetKeccak256` runtime helper for all unmatched
      layouts
- [x] Preserve existing memory growth, gas charging, and cache behavior
      semantics

### Phase 5: validation and guardrails

- [x] Add EVM frontend unit tests covering matched and fallback control-flow
      patterns
- [x] Add EVM tests validating mapping slot derivation equivalence for
      calldata/caller keyed helpers
- [x] Add regression coverage for invalid jumpdest, high-limb non-zero jump
      targets, and memory expansion edge cases
- [x] Measure end-to-end impact on representative ERC20 / external-call /
      create-heavy workloads

## Compatibility Notes

This change must preserve all interpreter-visible EVM semantics:

- JUMP and JUMPI target validity rules remain unchanged
- Stack overflow/underflow behavior remains unchanged
- Memory expansion and gas charging remain unchanged
- `KECCAK256` results and side effects remain unchanged

The optimization is additive. Any failure to prove a pattern, guard, or layout
must fail closed to the existing generic lowering path.

## Performance Notes

Local end-to-end measurements were taken on 2026-05-13 with the repo's
`solidityContractTests` harness in `Release` mode using multipass JIT,
`--enable-evm-gas`, `--disable-multipass-multithread`, and `taskset -c 0`.
Each datapoint below is the median of 3 measured runs.

| Workload | Harness focus | Baseline median | Current median | Delta |
|----------|---------------|-----------------|----------------|-------|
| `erc20` | mapping / ABI-heavy | 1.80s | 1.83s | +1.7% |
| `caller` | external-call / ABI-heavy | 0.46s | 0.53s | +15.2% |
| `Factory` | create-heavy | 0.16s | 0.18s | +12.5% |

These measurements complete the validation checklist, but they do not yet show
an end-to-end speedup over the pre-change baseline. Additional profiling is
required before claiming a macro-level performance win for this slice.

## Risks

- **Pattern unsoundness**: bytecode pattern recognition may accidentally widen
  beyond the intended semantic shape. Mitigation: keep matching conservative
  and anchored on existing analyzer/jumpdest facts; fail closed to generic
  lowering.
- **State-transfer mismatches across lifted blocks**: grouped control-flow
  lowering can break lifted entry-state propagation if it bypasses existing
  block handoff logic. Mitigation: reuse current constant-jump and fallthrough
  state-assignment paths instead of inventing parallel control-flow plumbing.
- **Gas or memory semantic drift in keccak helpers**: specialized helpers can
  diverge from generic `MSTORE/MSTORE/KECCAK256` behavior. Mitigation:
  centralize gas/memory checks in shared helper code paths and add equivalence
  tests against the existing implementation.
