# Change: Recover memory proofs on demand within fixed budgets

- **Status**: Proposed
- **Date**: 2026-08-05
- **Tier**: Full

## Overview

This change avoids eager construction of every EVM memory analysis. A cheap
opportunity scan admits planning only when a compatible consumer exists;
individual proof services are activated on demand, indexed, cached, and bounded
by deterministic work budgets.

When a query exceeds its budget or encounters incomplete facts, cycles, dynamic
predecessors, or unsupported control flow, it returns a conservative result and
the existing generic lowering remains in use.

## Motivation

Prepared-memory optimizations should not impose their full analysis cost on
contracts that cannot consume a proof. Demand-driven recovery keeps the no-hit
path small while retaining proof availability for eligible consumers.

## Design

The bytecode visitor first records a compact opportunity summary. Full memory
facts and planning state are constructed only after admission. Stable operation
indexes replace repeated scans, and region, extent, dead-store, and forwarding
components activate only when queried.

Sparse proof queries use deterministic per-query and per-compilation budgets and
cache both successful and conservative outcomes. Repeated dense demand may
promote to a bounded global analysis; failed or over-budget promotion remains a
permanent conservative fallback for that compilation.

## Impact

The change affects only EVM multipass compilation. It preserves deterministic
results and does not add runtime policy selection, telemetry publication,
certificate emission, benchmark output, or paper artifacts. Budget exhaustion
can reduce optimization coverage but cannot authorize an unsafe transformation.

This proposal depends on the effect-aware proof-query contract. Prepared-helper
consumer enforcement is layered on top in a subsequent change.

## Validation

Focused frontend tests cover no-hit admission, dense-hit preservation, lazy
activation, cache identity, budget exhaustion, deterministic promotion, cycles,
entry backedges, incomplete predecessors, and conservative fallback.
Differential tests continue to compare interpreter and multipass behavior.

## Checklist

- [x] Production implementation and focused tests prepared
- [x] Compiler module specification updated
- [x] Budgets and promotion decisions are deterministic
- [x] Telemetry, certificates, experiment policy, and paper assets excluded
