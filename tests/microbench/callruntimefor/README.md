# callRuntimeFor microbench

Measures the per-invocation cycle/op cost of the EVM-MIR compiler's
`callRuntimeFor<...>` helper (in `src/compiler/evm_frontend/evm_mir_compiler.cpp`)
as used by `handleMulMod`, `handleAddMod` (slow path), and `handleDiv`
(multi-limb branch).

Output gates upstream u256 optimization decisions:

- **A2 / MULMOD Barrett restart** — Barrett reduction worth the added MIR
  only if the call it replaces is meaningfully more expensive than
  inline Barrett.
- **N6 / MULMOD narrow-path inline** — u64-narrow MULMOD inline worth
  shipping only if the runtime call dominates per-op cost.

Decision matrix:

| cycles/op | A2 (Barrett) | N6 (narrow inline) |
|-----------|--------------|--------------------|
| < 20      | irrelevant   | stay helper-call   |
| 20–100    | gray zone, empirical confirmation needed | gray zone |
| > 100     | strongly motivated | must inline |

## Methodology

Each fixture is a tight EVM bytecode loop iterating N = 65000 times. The
loop body either:

- **test fixtures** — invoke the target opcode (MULMOD / ADDMOD / DIV)
  once per iteration, with operands chosen to defeat constant folding
  *and* the EVM-MIR compiler's narrow / fast paths so `callRuntimeFor`
  is actually hit, OR
- **baseline fixtures** — push the same number of stack items per
  iteration as the matching test fixture, then `POP` them. Matches the
  test fixture's stack-shape delta so the subtraction isolates
  per-call overhead, not "MULMOD on stack of depth K".

```
cycles_per_op = (test_ns_per_run − baseline_ns_per_run) × cpu_freq_GHz / N
```

CPU frequency is read from `/proc/cpuinfo` (max "cpu MHz" entry).

`evmone-bench` is invoked with `--benchmark_min_time=1x` to avoid the
re-JIT loop (per `reference_evmone_bench_iteration_semantics.md`:
default `Ns` semantics re-JITs each iteration), and
`--benchmark_repetitions=5` for mean/stddev.

## Fixtures

| File | Stack delta / iter | Path forced | Compiler reference |
|------|---------------------|------------|----|
| `fixtures/callruntime/baseline_empty.json` | +0 (DUP1, PUSH8, PUSH8, POP×3) | none (loop overhead only) | — |
| `fixtures/callruntime/mulmod_loop.json` | +0 (DUP1, PUSH8, PUSH8, MULMOD, POP) | `callRuntimeFor` unconditional (no narrow path) | `evm_mir_compiler.cpp:2479` |
| `fixtures/callruntime/addmod_loop.json` | +0 (DUP1, PUSH8, PUSH8, ADDMOD, POP) | slow path: u64 modulus → `mod[3] == 0` defeats fast-path eligibility | `evm_mir_compiler.cpp:2444-2455` |
| `fixtures/callruntime/baseline_div.json` | +0 (DUP1, PUSH32, DUP2, POP×3) | none (loop overhead only) | — |
| `fixtures/callruntime/div_nonconst_loop.json` | +0 (DUP1, PUSH32, OR, DUP2, DIV, POP) | multi-limb branch: divisor = `counter \| BIG_CONST`, has upper limbs at runtime | `evm_mir_compiler.cpp:1748-1820,1842` |

In every test fixture, the *first* operand pushed onto the stack each
iteration is a `DUP1` of the loop counter — a non-const value with a
known u64 range. This ensures the constant-folding short-circuit
(`isConstant() && ... && isConstant()`) fails so `callRuntimeFor` is
actually emitted.

### Test/baseline op-count asymmetry

The matched-baseline subtraction is not perfectly clean — each test body
has one fewer POP than its baseline (the opcode itself returns one
result that gets POPed):

| Body              | Ops                                              | Count |
|-------------------|--------------------------------------------------|-------|
| `mulmod_body`     | DUP1, PUSH8, PUSH8, MULMOD, POP                  | 5     |
| `baseline_empty`  | DUP1, PUSH8, PUSH8, POP, POP, POP                | 6     |
| `div_body`        | DUP1, PUSH32, OR, DUP2, DIV, POP                 | 6     |
| `baseline_div`    | DUP1, PUSH32, DUP2, POP, POP, POP                | 6     |

For MULMOD / ADDMOD this means the delta is **`callRuntimeFor − 2·POPs`**
rather than pure `callRuntimeFor`. A POP in DTVM's multipass JIT lowers
to ~2-3 cycles (stack-slot dec), so the systematic underestimate is
≤ 10 cycles — well below the 100-cycle bucket boundary that gates A2/N6.
For DIV the op counts match exactly (6 vs 6).

### What DIV actually measures (wrapper-cost floor)

`evmGetDiv` is a thin wrapper around `intx::uint256::operator/`. When
the dividend (loop counter) is small (< 2^16) and the divisor is large
(> 2^192), `intx` early-exits with quotient 0. So `div_nonconst_loop`
effectively measures **the cost of the `callRuntimeFor` wrapper alone**
(register setup, indirect call, prologue/epilogue, result-store), with
~zero contribution from arithmetic.

This gives a clean three-way decomposition of where the cycles go:

| Op       | cycles/op | Composition                                       |
|----------|-----------|---------------------------------------------------|
| DIV      | ~25       | wrapper only (intx early-exit)                    |
| ADDMOD   | ~110      | wrapper + `intx::addmod`                          |
| MULMOD   | ~225      | wrapper + 512-bit `intx::mulmod`                  |

**MULMOD is the cleanest signal** for the A2/N6 decision:

- No narrow / fast path; unconditional `callRuntimeFor`.
- `intx::mulmod` has no analogous early-exit shortcut.
- Wide 512-bit intermediate forces real arithmetic.
- Bucket assignment ( > 100 ) is robust to the ≤ 10-cycle subtraction error.

## Usage

```bash
# From DTVM repo root or any subdir (script resolves its own path):
bash tests/microbench/callruntimefor/run_microbench.sh

# Override defaults if needed:
DTVMAPI=/path/to/libdtvmapi.so \
EVMONE_BENCH=/path/to/evmone-bench \
REPS=10 \
  bash tests/microbench/callruntimefor/run_microbench.sh
```

The script reuses the main DTVM build's `libdtvmapi.so` if no
worktree-local build exists, so no separate build is required.

Raw evmone-bench output is written to `result.json` in this directory.

## Known limitations

- WSL2 / virtualized timestamp jitter. Stddev typically ~5-10% of mean.
- `--benchmark_min_time=1x` means each repetition is exactly one
  bytecode run (= 65000 iterations). To increase iteration count,
  edit `LOOP_BOUND_HEX` in the bytecode generator and regenerate.
- The "loop overhead" (counter + JUMPI + GT) is subtracted by the
  baseline, but the **MULMOD/ADDMOD/DIV opcode dispatch in dMIR** is
  not isolated from `callRuntimeFor` — what we measure is "everything
  that happens between the previous POP and the next POP".
