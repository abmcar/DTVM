# Impl review round 1 — perf reviewer (Opus persona)

**Reviewer role**: Perf reviewer. Verify microbench methodology:
- Subtract-matched-baseline rigor
- Constant-folding defeat (test ops aren't compile-time evaluated)
- `--benchmark_min_time=1x` to avoid re-JIT
- File:line citations for path coverage

**Files under review**:
- `tests/microbench/callruntimefor/fixtures/callruntime/*.json` (5)
- `tests/microbench/callruntimefor/run_microbench.sh`
- `tests/microbench/callruntimefor/gen_bytecode.py`
- `tests/microbench/callruntimefor/README.md`

## Findings

### M1 — Methodology: matched-baseline subtraction is sound, with caveat

The harness measures `(test_ns − baseline_ns) / N` per the agreed formula.
Each test fixture has a stack-shape-matched baseline:

- `mulmod_loop`, `addmod_loop` ← `baseline_empty` (5 vs 6 ops; both net +0)
- `div_nonconst_loop` ← `baseline_div` (6 vs 6 ops; both net +0)

**Caveat (acknowledged in README §"Test/baseline op-count asymmetry")**:
MULMOD / ADDMOD subtraction yields `callRuntimeFor − 2·POPs`, not pure
`callRuntimeFor`. Each POP is ~2-3 cycles in DTVM multipass JIT, so
systematic underestimate ≤ 10 cycles. Bucket placement (> 100) is robust
to this error. **Acceptable** — README quantifies the error explicitly.

DIV fixture's baseline matches exactly (6 vs 6 ops), no asymmetry there.

### M2 — Constant-folding defeat: verified

The first operand of every test op is `DUP1` of the loop counter — a
non-const value with u64 range. Verified in
`evm_mir_compiler.cpp:2466-2476`: MULMOD constant-folds only if **all
three** operands are `isConstant()`. With one non-const, the all-const
branch is skipped and `callRuntimeFor` is emitted.

Counter is initialized to `PUSH1 0x00` and incremented per iter — it is
never a compile-time constant in the JIT's view.

### M3 — `--benchmark_min_time=1x` is correctly used

`run_microbench.sh` invokes `evmone-bench` with `--benchmark_min_time=1x`,
matching the project memory `reference_evmone_bench_iteration_semantics.md`
which warns that `Ns` semantics re-JITs each iteration. Each repetition
is exactly one bytecode run (= 65000 internal iterations).

### M4 — Repetitions & stddev

`--benchmark_repetitions=5` (REPS env-overridable to 10). Observed
stddev/mean of 5-9% on WSL2, acceptable for bucket-tier decisions.
Two independent runs gave consistent bucket assignments.

### M5 — CPU frequency reading

The script prefers `/proc/cpuinfo`'s max "cpu MHz" entry, falling back
to `lscpu`'s `mhz_per_cpu` from benchmark JSON. This is appropriate for
the WSL2 host where `lscpu` reports the boosted max (3878 MHz on this
machine), not a throttled current-state value. Cycle conversion math
is correct: `ns × GHz = cycles`.

### M6 — File:line path coverage citations

README cross-references each path:

- MULMOD: `evm_mir_compiler.cpp:2479` (unconditional `callRuntimeFor`)
- ADDMOD slow: `evm_mir_compiler.cpp:2444-2455` (SlowBB branch)
- DIV multi-limb: `evm_mir_compiler.cpp:1748-1820`, `:1842,1847`
  (MultiLimbBB branch)

All citations verified against the source file in the parent commit
(origin/main). Note that `handleAddMod` line 2304-2316 builds the
`FastEligible` predicate; reviewer 2 (codex) re-verifies this.

## Verdict

**GO** with the existing acknowledgement of the POP-asymmetry caveat
in README. The harness produces a defensible cycle/op number that
maps cleanly to the A2/N6 decision matrix:

- MULMOD ~210-245 cycles/op → **> 100 bucket** (decision: ship A2 / N6)
- ADDMOD ~100-120 cycles/op → **> 100 bucket** (slow path is real)
- DIV ~25 cycles/op → wrapper-only floor (informational, not actionable)

## Suggestions (non-blocking)

- Consider also adding a `mulmod_loop_intx_friendly` fixture where
  operands are large enough that `intx::mulmod` does its full 512-bit
  work (already largely the case — current u64 × u64 mod u64 still
  triggers full intx::mulmod). The current MULMOD signal is strong
  enough to gate the A2/N6 decision; expanding scope is gold-plating.
- Could record CPU freq once at the start of `run_microbench.sh` to
  guard against scaling during the run; currently read once at the end.
  Stddev ~5% suggests this isn't an issue on this host.
