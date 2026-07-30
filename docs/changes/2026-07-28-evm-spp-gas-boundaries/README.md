# Change: Preserve gas semantics at EVM SPP boundaries

- **Status**: Accepted
- **Date**: 2026-07-28
- **Tier**: Light
- **PR**: #579

This fix closes two unsafe SPP boundary classes with 0% `GasBlock` size growth.
Retain both the unresolved-source and implicit-target guards to preserve gas
semantics.

## Overview

The Structured Precharging Pass (SPP) must not rely only on total path cost at
these two boundaries:

- `SSTORE`, whose EIP-2200 sentry depends on the gas remaining at the
  instruction;
- unresolved dynamic `JUMP` and `JUMPI` sources, whose runtime successor set
  is incomplete in the explicit cache-build CFG.

The implementation treats `SSTORE` as a gas-sensitive boundary and rejects
source-side shifts from blocks with omitted dynamic successors. The existing
implicit-predecessor check continues to reject shifts into possible dynamic
targets.

## Motivation

SPP may move a successor block's gas charge into its predecessor. This
transformation is valid only when intermediate gas cannot affect execution and
every runtime successor receives the corresponding compensation.

Precharging later work before `SSTORE` can change a successful execution into
an out-of-gas failure at the EIP-2200 call-stipend threshold. At an unresolved
dynamic `JUMPI`, moving the explicit fallthrough cost onto the source also
charges the taken path even though its omitted target receives no compensating
reduction.

## Impact

- `src/evm/evm_cache.cpp` adds the two scheduling guards.
- `GasBlock` stores the omitted-successor flag in existing padding and remains
  32 bytes.
- `src/evm/evm_cache.md` and `docs/modules/evm/cache-build.md` record the
  source, successor, and CFG invariants used by SPP scheduling.
- Cache-level regressions cover both failure modes. An
  interpreter-versus-multipass regression covers the unresolved dynamic
  `JUMPI` case without introducing a new runtime component.
- No API, ABI, configuration, or persisted-data format changes are introduced.
- The guards can reduce SPP scheduling opportunities around `SSTORE` and
  unresolved dynamic jumps. Applying the `SSTORE` boundary before Istanbul is
  intentionally conservative. No performance claim is made.

## Verification

The production implementation at commit
`79540851a852d5eb2fbc1f847c2fc6f95acd7aff` was validated by:

- tracked-source formatting and a clean Release all-target build, with no
  warning diagnostics from changed files;
- focused cache regressions: 2/2;
- focused interpreter-versus-multipass regression: 1/1;
- CTest: 12/12 targets;
- interpreter unit, Cancun state, and EVM assembly suites: 215/215, 2723/2723,
  and 209/209;
- multipass unit, Cancun state, and EVM assembly suites: 223/223, 2723/2723,
  and 209/209.

A sealed multipass replay of blocks 21,800,000--21,800,031 covered 32/32
blocks, 4,993 transactions, and 594,578,894 gas. Its semantic summary matched
the frozen reference, and its exported post-state was byte-identical. The
replay used Git tree `97dd4837b1803f72e3b3c4fc742fec691b09d115` and
`libdtvmapi.so.0.1.0` SHA-256
`5135b4a4424a92c4e66cce6c86df01e688998468341e3dd0292bbb1f7ff2d73a`.
This evidence covers the production implementation in
`src/evm/evm_cache.cpp` blob
`6ca5d7a55812920d4d0e1de8b258c176c3d3c252`; it does not claim that a later
documentation or test-only head was itself replayed.

## Checklist

- [x] Implementation complete
- [x] Tests added/updated
- [x] Module specs in `docs/modules/` updated
- [x] Build and tests pass
