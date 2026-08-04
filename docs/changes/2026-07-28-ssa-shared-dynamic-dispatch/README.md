# Change: Share full dynamic dispatch in stack-SSA builds

- **Status**: Implemented
- **Date**: 2026-07-28
- **Tier**: Light

## Overview

Reuse the existing module-level full-table dynamic jump dispatch CFG when EVM
stack-SSA lifting is enabled. Sources with a proven smaller target set continue
to use a per-source filtered dispatch.

## Motivation

The previous stack-SSA path rebuilt the full dynamic dispatch for every source.
For contracts with many dynamic jump sites and many `JUMPDEST`s, this duplicated
MIR control flow at approximately
`O(dynamic jump sources × dynamic jump targets)`. The duplication increased
frontend time, MIR size, register-allocation work, and emitted code without
changing execution semantics.

Runtime dynamic targets that use the full dispatch table are already forced to
the materialized runtime-stack fallback. They do not consume source-specific
stack-merge phis, so the full-table dispatch can be shared in stack-SSA and
non-SSA builds alike.

## Impact

- `compiler`: remove the stack-SSA exception from full-table dispatch sharing.
- `tests`: assert that many unfiltered dynamic sources produce fewer dispatch
  switches than sources.
- `docs`: record the runtime-stack fallback invariant that makes sharing sound.

Filtered target sets remain source-specific so their predecessor registration
and target filtering are unchanged. EVM semantics and determinism are
unaffected.

## Validation

- [x] Implementation complete
- [x] Tests added
- [x] Module specifications updated
- [x] Formatting check passes
- [x] Multipass stack-SSA build passes
- [x] `evmJitFrontendTests` passes (`109/109`)
