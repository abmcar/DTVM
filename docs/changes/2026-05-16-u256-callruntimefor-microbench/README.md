# Change: callRuntimeFor cycle/op microbench harness

- **Status**: Proposed
- **Date**: 2026-05-16
- **Tier**: Light

## Overview

Add a standalone microbench harness under `tests/microbench/callruntimefor/`
that drives `evmone-bench` (Google Benchmark cycle counter) against
hand-crafted EVM bytecode loops which deliberately route through
`callRuntimeFor`. Subtracts a matched empty-loop baseline (no runtime call,
same stack delta) to isolate the per-call runtime overhead.

Output: measured cycle/op for `callRuntimeFor` as used by `handleMulMod`,
`handleAddMod` (slow-path), and `handleDiv` (multi-limb slow-path).

This is **tooling only — no `src/` change**. The harness is consumed by
upstream optimization decisions for A2 (MULMOD Barrett restart) and N6
(MULMOD narrow-path inline).

## Motivation

Two pending u256 optimizations gate their go/no-go on the true cost of
`callRuntimeFor`:

- **A2 / MULMOD Barrett restart** — Barrett reduction is only worth its
  added MIR if the runtime call it replaces costs noticeably more than the
  inline Barrett path.
- **N6 / MULMOD narrow-path inline** — A u64-narrow MULMOD inline path is
  only worth implementing if the runtime call it would replace dominates
  per-op cost.

The current Stage 1 reviewer estimate is 35-70 cycles per `callRuntimeFor`
invocation (gray zone for both decisions). A real measurement collapses
this uncertainty.

Decision matrix (set by upstream A2/N6 reviewers):

- **< 20 cycles**: A2 irrelevant; N6 stays as helper-call.
- **20-100 cycles** (gray): empirical confirmation needed before either
  optimization ships.
- **> 100 cycles**: A2 Barrett strongly motivated; N6 must inline.

## Impact

- **No `src/` change.** Adds files under `tests/microbench/callruntimefor/`:
  - `README.md` — usage and methodology notes
  - `fixtures/callruntime/baseline_empty.json` — empty 3-push-3-pop loop
  - `fixtures/callruntime/baseline_unop.json` — empty 1-push-1-pop loop
    (matches DIV stack delta)
  - `fixtures/callruntime/mulmod_loop.json` — MULMOD with one non-const
    operand to force `callRuntimeFor` (clean path, no fast path)
  - `fixtures/callruntime/addmod_loop.json` — ADDMOD with u64 modulus to
    force slow path (`mod[3] == 0` defeats fast-path eligibility)
  - `fixtures/callruntime/div_nonconst_loop.json` — DIV with non-const
    divisor that has upper limbs set to defeat the u64÷u64 fast path
  - `run_microbench.sh` — driver that invokes `evmone-bench` with DTVM as
    external EVMC VM and post-processes JSON output into cycle/op.
- Local infrastructure only — does **not** build or run in CI by default.
- Reads from `build/lib/libdtvmapi.so` in the main DTVM build dir; no
  separate DTVM build needed.

## Methodology

The harness measures `(test_loop_ns - baseline_loop_ns) / N` where:

- `N` is the in-bytecode loop count (65000, matching evmone's existing
  `wide_compare_u256_control` micro-bench), large enough that JIT compile
  cost is amortized below the noise floor.
- Each "test loop" runs the target opcode (MULMOD / ADDMOD / DIV) once per
  iteration with operands that defeat constant-folding **and** any narrow
  fast path in the EVM-MIR compiler.
- The "baseline loop" pushes the same number of stack items per iteration
  as the test loop, then POPs them — matching stack-shape delta so the
  subtraction isolates the runtime-call cost alone.
- Per `reference_evmone_bench_iteration_semantics.md`, we pass
  `--benchmark_min_time=1x` to keep `--benchmark_repetitions=5` from
  re-JITting between iterations.

Cycle/op derivation:

```
cycles_per_op = (test_ns_per_run - baseline_ns_per_run) * cpu_freq_GHz / N
```

`run_microbench.sh` reads CPU frequency via `lscpu`/`/proc/cpuinfo` and
prints both ns/op and the derived cycles/op.

## Verification path

1. **Path coverage check** (review by Codex reviewer): each `*_loop.json`
   fixture must actually hit `callRuntimeFor` — not a fast path or constant
   folding. Verified by citing
   `src/compiler/evm_frontend/evm_mir_compiler.cpp` line numbers:
   - MULMOD: line 2479 (unconditional callRuntimeFor when any operand non-const)
   - ADDMOD slow: line 2444-2455 (slow path; needs `mod[3]==0`)
   - DIV multi-limb: line 1748-1820 (callRuntimeFor on `HasUpperLimbs` branch)
2. **Sanity run**: harness produces non-zero, reproducible numbers across
   5 repetitions; baseline < test loops; coefficient of variation small.
3. **Initial measurement record** captured in `initial-measurement.md`
   alongside this change doc for upstream A2/N6 decision input.

## Checklist

- [ ] Fixtures pass evmone-bench JSON parser (file shape correct)
- [ ] Each fixture exercises `callRuntimeFor` (path coverage verified)
- [ ] `run_microbench.sh` produces reproducible 5-rep results
- [ ] Initial cycle/op measurement recorded
- [ ] `tools/format.sh check` clean (no source changes)
- [ ] No `src/` touched

## Out of scope

- Variations to `callRuntimeFor` impl itself (codegen change). This
  microbench measures the *as-shipped* cost; any reduction effort is a
  separate change.
- WASM frontend microbenches.
- Statetest correctness coverage — `evmone-statetest` already covers
  MULMOD/ADDMOD/DIV correctness.
