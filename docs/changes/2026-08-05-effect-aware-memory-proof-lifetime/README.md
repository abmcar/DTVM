# Change: Model effect-aware memory proof lifetime

- **Status**: Proposed
- **Date**: 2026-08-05
- **Tier**: Full

## Overview

This change defines typed memory-proof requirements for EVM runtime helpers and
tracks when a recovered proof remains valid across control-flow edges. Unknown
effects, incomplete control flow, dynamic dispatch, and unsupported paths fail
closed.

The proof only establishes the memory facts represented by its typed token. It
does not authorize moving gas charges, expansion checks, traps, termination, or
other protocol-observable behavior.

## Motivation

Existing prepared-memory helpers can reuse facts recovered by the multipass
frontend, but cross-block reuse needs one shared definition of which operations
preserve or invalidate those facts. Encoding the contract centrally prevents
individual lowering sites from making ad hoc assumptions about proof lifetime.

## Design

Opcode and runtime-helper effects are summarized with explicit flags for memory
reads and writes, logical-size observation, dynamic gas, external calls,
externalization, ordering, and termination. Runtime helper contracts state the
exact proof obligations that a caller must satisfy.

The proof-lifetime query follows only complete, statically known control flow.
It rejects joins, cycles, dynamic dispatch, unknown effects, and paths whose
observable behavior cannot be proven safe. `LOG` is classified as an observable
memory reader but is not introduced as a typed cross-block consumer here.

## Impact

The change is limited to EVM multipass analysis and its internal helper
contracts. It does not change a public ABI, runtime helper implementation,
protocol-visible ordering, or the interpreter. Conservative rejection may
reduce optimization opportunities but preserves the existing generic path.

This proposal is the foundation for budgeted proof recovery and prepared-helper
contract enforcement. It has no dependency on experiment policies, telemetry,
certificate publication, or witness serialization.

## Validation

Focused frontend tests cover opcode effects, helper requirements, logical-size
preservation, unique-predecessor reuse, and fail-closed behavior for calls,
termination, incomplete CFGs, dynamic dispatch, and unknown effects.
Differential tests continue to compare interpreter and multipass behavior.

## Checklist

- [x] Production implementation and focused tests prepared
- [x] Compiler module specification updated
- [x] Unknown or incomplete cases fail closed
- [x] Experiment and paper-only surfaces excluded
