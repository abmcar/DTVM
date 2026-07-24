# Change: Reduce repeated compiler cold-path scans

- **Status**: Accepted
- **Date**: 2026-07-24
- **Tier**: Light

## Overview

This change removes two repeated linear scans from cold multipass compilation:

- MIR block membership now uses the block's index and pointer identity instead
  of scanning every block in the function.
- CGIR dead-instruction elimination now walks backward across the use suffix of
  a register chain instead of traversing its definition prefix for each query.

Both changes preserve the existing compiler semantics and data-structure
contracts.

## Motivation

The following diagnostic measurements were collected on pre-rebase
implementation head `1edac697773e3f20fe186a6a071a42793ee22b81`, based on
`57324bda91c945dc4c11c29d9809c2085b74d9d2`. They motivate this change but
do not measure the rebased integration.

On LabPerf's frozen 4-block, 499-transaction `prefix004` diagnostic corpus
(`SHA256 7242de6d9331621b27a4422891a05f0f167c49f0183c69c96b321a81fd38cc52`),
the two original scans dominated cold compilation:

| Flat sampled cycles | Before | After |
|---|---:|---:|
| `CgDeadCgInstructionElim::isDead` | 39.53% | 0.28% |
| `EVMMirBuilder::setInsertBlock` | 12.92% | 0.01% |

For the combined change, first cold block-execution time decreased from
2,238.890–2,263.209 seconds to 1,083.372 seconds, a diagnostic reduction of
51.6–52.1% or 2.07–2.09x. The two baseline values are non-identical reference
runs, not statistical replicates or a confidence interval.

These measurements apply only to `prefix004`. They do not establish a
32-block, mainnet-wide, production, steady-state runtime, or formal benchmark
result.

## Implementation

### MIR block membership

`MFunction::appendBlock` assigns each appended block its position in the
function's block vector. The new membership query accepts a block only when
its index is in range and the pointer at that index is identical. The pointer
check prevents a newly created block with the default index from being
mistaken for the entry block. `EVMMirBuilder::setInsertBlock` uses this
constant-time query before appending a block.

This relies on the existing append-only, stable-index invariant for live MIR
blocks.

### CGIR external-use lookup

The CGIR register chain stores definitions as a prefix and uses as a suffix.
The new lookup starts at the chain tail and walks backward only while operands
are uses. A use owned by another instruction keeps the definition live; a use
owned by the definition itself does not. The walk stops at the first
definition, with an explicit head check for a circular chain containing only
uses.

This preserves dead-instruction elimination behavior for self-uses, external
uses, multiple definitions, and fixed-point deletion.

## Impact

The blast radius is limited to the multipass compiler's MIR membership lookup,
CGIR dead-instruction elimination, and their regression tests. There is no
public ABI, SONAME, dependency, object-layout, global-state, threading, or
determinism contract change.

The compiler module specification was reviewed and remains accurate; no
module-spec update is required. The implementation depends on the existing
MIR stable-index and CGIR definition-prefix/use-suffix invariants. Future
changes to either data structure must update the corresponding query and
tests.

The measured hotspot shift identifies register allocation and liveness as
subsequent diagnostic targets. It does not show identical generated machine
code or a runtime-execution improvement.

## Validation

Local validation on implementation head
`7390935f9fe19d325fb096b787b6d643762262f0`, rebased onto
`a7e73059ffc46b14978e87d04a3fa6b36d4f6c3a`:

- Format and diff checks: passed
- Release build: passed
- Release EVM frontend: 53/53
- EVM range analyzer: 50/50
- EVM differential: 65/65
- ASAN EVM frontend: 53/53, with 0 sanitizer errors
- `tools/dtvm_local_test.sh --auto`: unit 223/223, EEST state
  2723/2723, EVM assembly 209/209, and CTest 12/12

GitHub Actions status for the rebased branch is tracked by PR #577.

## Checklist

- [x] Implementation complete
- [x] Tests added/updated
- [x] Module specs reviewed; no contract update required
- [x] Build and tests pass
