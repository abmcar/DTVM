# Change: Position-independent EVM host calls

- **Status**: Proposed
- **Date**: 2026-08-20
- **Tier**: Full

## Overview

The EVM multipass JIT materialized every host routine address as an `movabs`
immediate inside the emitted `.text`. This change routes all host calls through
a dispatch table carried by `EVMInstance`, so a compiled EVM object depends only
on slot indices and is valid in any process and against any build of
`libdtvmapi.so`.

This is the first, independently reviewable increment toward a persistent
compilation-artifact cache. It deliberately does **not** add cache storage,
loading, or key derivation.

## Motivation

DTVM cannot reuse JIT output across processes, so every process recompiles all
mainnet bytecode it meets. Three independent measurements bound the cost:

- Witness replay over 100 mainnet blocks: cold compilation median 118 s/block
  (mean 150 s) against a 179.5 ms median steady-state execution — a ratio of
  roughly 655x. Cumulative cold compile 15,053 s versus 20.4 s of measured
  execution.
- A five-engine comparison spent 67% of its 79-minute wall clock inside DTVM
  (3,195 s wall clock for 165 s of measured execution).
- Wired into a reth node against real mdbx state, stock reth produced its first
  block 37 s into the execution phase; DTVM was still compiling after 8 minutes
  (CPU 48%, disk 0.01 MB/s, process state R — CPU-bound compilation, not I/O).

Unique contract count grows far sub-linearly in block count (29,334 contract
references but only 4,505 unique contracts over 100 blocks), so a persistent
cache converts a per-process cost into a one-time cost.

A prior feasibility study established that the compiled artifact is already a
complete relocatable ELF object (`CompileContext::ObjBuffer`, parsed and loaded
by `JITCompilerBase::emitObjectBuffer`), and that the EVM path copies only
`.text` with no relocations. Baked host addresses were the one hard blocker:
they appear as `movabs $imm64` with **no** relocation entry, so a replayed
`.text` would call into garbage under ASLR or against a differently built
`libdtvmapi.so`.

## Impact

### Affected Modules

- `src/compiler/evm_frontend` — host call emission
- `src/runtime` — `EVMInstance` layout and construction

### Affected Contracts

- `EVMInstance` gains a `HostFuncTable` array field plus
  `getHostFuncTableOffset()`. Layout of the EVM-specific field region changes;
  the WASM-compatible prefix region is untouched.
- `COMPILER` gains `getHostFuncTable()`, `getHostFuncSlot()`,
  `getHostFuncSlotOffset()`, and the `ExtraHostFunc` slot enum.
- JIT-emitted MIR for a host call changes from
  `icall ... (target = const.i64 <addr>, ...)` to
  `icall ... (target = load (base = $0, offset = <slot offset>), ...)`.

### Compatibility

No external ABI or EVM-semantics change. Behaviour is identical; only the
instruction sequence used to reach a host routine changes.

## Implementation Plan

### Phase 1: Position-independent host calls (this change)

- [x] Add a flat host dispatch table over `RuntimeFunctions` plus explicit slots
      for the non-helper routines (`memmove`, and the three
      `EVMInstance::*InstanceExceptionOnJIT` entry points).
- [x] Carry the table inline in `EVMInstance`; populate it in `newEVMInstance`.
- [x] Emit every host call as a single load from the table
      (`EVMMirBuilder::loadHostFuncAddr`).
- [x] Hard-fail (never silently fall back to an immediate) when a routine is
      absent from the table.
- [x] Update the MIR-golden tests to match the load-based call form.

### Phase 2: Cache storage and loading (not in this change)

- [ ] Store `ObjBuffer` at the point it is wrapped in `emitObjectBuffer`.
- [ ] Load a cached object through the existing parse/copy/mprotect path.

### Phase 3: Cache key derivation (not in this change)

- [ ] Derive a key covering every codegen-affecting input.

## Compatibility Notes

None. `EVMInstance` is an internal runtime object; its layout is not part of any
published interface.

## Risks

- **Throughput**: each host call gains one load
  (`mov disp(%instance), %reg`) in place of a `movabs` immediate. Mitigation:
  the table is inline in `EVMInstance` so the load is single-level and hits the
  same cache lines the call already touches; measured against a same-config
  baseline before merge. If a regression appears on hot arithmetic helpers, the
  fallback is symbolisation (emit named external symbols and extend the existing
  `ExternRelocs` path), which leaves codegen quality unchanged.
- **Silent reintroduction of a baked address**: a new host call site that
  bypasses `loadHostFuncAddr` would be invisible to tests. Mitigation:
  `getHostFuncSlot` aborts on an unknown routine rather than falling back, and
  position independence is verified empirically by byte-comparing the emitted
  object across two separate processes with ASLR enabled.
- **Table capacity drift**: `EVMInstance::HostFuncTableCapacity` is declared in
  the runtime while the slot count is owned by the frontend. Mitigation: a
  `static_assert` in `newEVMInstance` fails the build if the frontend outgrows
  the capacity.

## Notes for Phase 3 (cache key)

Keying only on contract code hash would be a correctness bug. `evm_compiler.cpp`
enumerates the codegen-affecting configuration; a key must cover code hash and
length, EVM revision, gas metering, the `ZEN_ENABLE_EVM_GAS_REGISTER` build
flag, the SPP metering variant, memory stride specialisation, the
message-derived `EVMMemorySpecializationProfile`, **host CPU optional features**
(`adx/bmi/bmi2/lzcnt/popcnt`, so one binary emits different code on different
machines), and compiler build identity.

`loadEVMModuleWithRegAllocRetry` transparently recompiles with
`DisableMultipassGreedyRA = true` when register allocation fails, so the key
must record the configuration that actually took effect, not the one requested.
Mismatches in this class produce plausible-looking wrong answers and surface as
consensus bugs rather than crashes.

## Validation (2026-08-25)

Build: Release, GCC 12, LLVM 15, `ZEN_ENABLE_SINGLEPASS_JIT=OFF` required.
`evmJitFrontendTests`: 179/179 passed (includes the updated MIR-golden and
call/terminating memory suites).

**Position independence & determinism** (`evmTextHashTool`, added by this
change): three contracts (host-call-dense, arithmetic-dense, memory-dense)
compiled in three separate processes with ASLR active
(`randomize_va_space=2`, differing libc bases confirmed) produced
byte-identical JIT `.text` (equal FNV-1a64). This simultaneously verifies
that no process-specific absolute address survives in emitted code and that
codegen is deterministic for identical inputs.

**Same-config hot-execution A/B** (`evmExecBenchTool`, added by this change;
baseline = parent commit `338d123`, 5 interleaved rounds x 200 timed
executions, medians):

| contract | baseline | candidate | delta | JIT size |
|---|---:|---:|---:|---|
| host-call-dense (worst case) | 30.0 us | 31.8 us | **+6.0%** | 81920 -> 77824 |
| arithmetic-dense | 2176 ns | 2162 ns | -0.6% (noise) | unchanged |
| memory-dense | 4942 ns | 4993 ns | +1.0% | unchanged |

The regression is confined to host-call sites; the synthetic worst case is
denser than any real contract, so real-block impact is bounded above by 6%
and remains to be measured at cache-acceptance time on real blocks. Fallback
trigger recorded: if real-block regression exceeds 2-3%, switch host calls to
symbolisation + load-time relocation (codegen quality identical to baseline,
persistence preserved via the loader).
