# Change: Enforce prepared-memory helper proof contracts

- **Status**: Proposed
- **Date**: 2026-08-05
- **Tier**: Full

## Overview

This change requires existing prepared-memory COPY, KECCAK, RETURN, REVERT, and
CALL-family lowering paths to present the typed proof tokens required by their
runtime helper contracts. A missing or mismatched obligation selects the
existing generic helper.

## Motivation

The multipass compiler already contains prepared-memory helpers. Central proof
contracts are useful only if every optimized call site checks the exact facts it
relies on. Explicit enforcement makes the safety boundary reviewable and keeps
new consumers from silently bypassing proof requirements.

## Design

COPY and KECCAK require logical-size, access-range, dynamic-gas, and ordering
proofs appropriate to their helpers. RETURN and REVERT additionally preserve
terminal behavior. CALL, CALLCODE, DELEGATECALL, and STATICCALL retain separate
argument-range and return-range obligations; coverage of one range never
substitutes for the other.

The checks do not move memory expansion, gas charging, account access, EIP-150
handling, external calls, return-data updates, or termination. `LOG` remains
outside this typed consumer series.

## Impact

The change is internal to EVM multipass lowering and reuses existing runtime
helpers. It does not change their ABI or implementation. Conservative fallback
may select a generic helper more often, but protocol-visible behavior remains on
the established path.

This proposal depends on the effect-aware proof contract and budgeted proof
service. It does not depend on telemetry, certificates, witnesses, experiment
policies, or paper artifacts.

## Validation

Frontend and differential tests cover COPY and KECCAK proof requirements,
RETURN and REVERT termination behavior, zero-length and overflow fallback, and
independent CALL argument and return obligations while preserving gas and
external-effect ordering.

## Checklist

- [x] Production enforcement and focused tests prepared
- [x] CALL argument and return obligations remain separate
- [x] `LOG` is not claimed as a typed consumer
- [x] Certificate and experiment-only surfaces excluded
