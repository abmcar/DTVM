# Change: Add EVM factorial build switches and replay diagnostics

- **Status**: Accepted
- **Date**: 2026-08-13
- **Tier**: Full

## Overview

Expose independent build switches for the EVM u64 arithmetic fast paths and
the complete memory-plan pipeline, and add an optional, versioned EVMC metrics
ABI for replay diagnostics. Defaults preserve the current production behavior.

## Motivation

The current EVM build cannot isolate the u64 and memory-plan effects on one
source revision. Replay experiments also need direct compile, cache, JIT,
interpreter, and fallback counters so compile-excluded execution can be proved
instead of inferred by subtraction.

## Impact

### Affected Modules

- `compiler`: compile-time u64 and memory-plan gates.
- `action`: scoped foreground JIT compile accounting.
- `vm-interface`: versioned metrics snapshot/reset ABI.
- `tests`: switch routing and ABI behavior coverage.

### Affected Contracts

- `ZEN_ENABLE_EVM_U64_ARITH_FASTPATH` defaults to `ON`.
- `ZEN_ENABLE_EVM_MEMORY_PLAN_PIPELINE` defaults to `ON` and is effective only
  when `ZEN_ENABLE_EVM_MEMORY_PLAN_FRAMEWORK` is enabled.
- `ZEN_ENABLE_EVMC_PHASE_METRICS` defaults to `OFF`.
- `dtvm_get_evmc_phase_metrics()` and `dtvm_reset_evmc_phase_metrics()` remain
  exported in every build. Production builds return `DISABLED`; diagnostic
  builds implement schema version 2 with a fixed 192-byte struct.

### Compatibility

Existing builds are unchanged because both optimization switches default to
`ON` and diagnostic collection defaults to `OFF`. Clients must initialize and
validate the metrics version and struct size before reading a snapshot.

## Implementation Plan

### Phase 1: Factorial switches

- [x] Gate all scoped u64 arithmetic fast paths behind one build option.
- [x] Gate memory-fact production and planner consumers behind one build option.

### Phase 2: Diagnostic ABI

- [x] Account top-level execute, synchronous compile, cache, active execution,
  and fallback metrics without changing production timing builds.
- [x] Reject snapshot/reset while execute or background compile is active.

### Phase 3: Validation

- [x] Build representative enabled and disabled cells.
- [x] Run structural, execution, and ABI tests before sealing experiment builds.

## Compatibility Notes

The metrics schema is additive and versioned. A future layout change must use a
new version and size; it must not reinterpret version 2 fields.

## Risks

- Diagnostic clocks perturb execution: formal timing uses metrics-off builds;
  metrics-on sibling runs provide attribution only.
- Partial memory gating would create an invalid factorial cell: both fact
  production and optimization consumers share the same pipeline gate.
- Concurrent snapshots could observe partial counters: the ABI returns `BUSY`
  until top-level execution and background compilation are quiescent.
