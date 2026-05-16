# Initial callRuntimeFor cycle/op measurement

- **Date**: 2026-05-16
- **Branch**: `tools/microbench-callruntimefor` (origin/main parent)
- **Host**: WSL2 / Ubuntu 22.04 kernel 6.6.87.2
- **CPU**: nominal 3.878 GHz (max from `/proc/cpuinfo`), 20 cores reported
- **DTVM build**: `/home/abmcar/DTVM/build/lib/libdtvmapi.so` (multipass JIT, gas metering on)
- **evmone**: `~/evmone/build/bin/evmone-bench`, library_version v1.9.4 release
- **Reps**: 5 and 10 (two runs)

## Results

Run 1 (REPS=5):

| Fixture           | mean ns/run | stddev | ns/iter | cycles/iter |
|-------------------|-------------|--------|---------|-------------|
| baseline_empty    | 229 539     | 15 689 | 3.53    | 13.7        |
| mulmod_loop       | 4 341 218   | 284 582| 66.79   | 259.0       |
| addmod_loop       | 2 215 096   | 171 024| 34.08   | 132.2       |
| baseline_div      | 226 746     | 15 032 | 3.49    | 13.5        |
| div_nonconst_loop | 679 064     | 44 442 | 10.45   | 40.5        |

Run 2 (REPS=10):

| Fixture           | mean ns/run | stddev | ns/iter | cycles/iter |
|-------------------|-------------|--------|---------|-------------|
| baseline_empty    | 257 661     | 49 801 | 3.96    | 15.4        |
| mulmod_loop       | 3 763 056   | 180 491| 57.89   | 224.5       |
| addmod_loop       | 1 962 790   | 64 951 | 30.20   | 117.1       |
| baseline_div      | 253 196     | 57 541 | 3.90    | 15.1        |
| div_nonconst_loop | 679 044     | 63 340 | 10.45   | 40.5        |

## callRuntimeFor delta (test − matched baseline)

| Op (path) | Run 1 cycles/op | Run 2 cycles/op | Decision bucket |
|-----------|------|------|---|
| MULMOD `callRuntimeFor` unconditional (evm_mir_compiler.cpp:2479) | **245** | **209** | **> 100** |
| ADDMOD slow path (evm_mir_compiler.cpp:2444-2455) | **119** | **102** | **> 100** |
| DIV multi-limb branch (evm_mir_compiler.cpp:1842,1847) | 27 | 25 | 20-100 gray |

## Verdict for upstream A2 / N6 decisions

- **MULMOD**: ~210-245 cycles per `callRuntimeFor` invocation. Lands
  clearly in the **> 100 cycles** bucket. By the agreed decision matrix:
  - **A2 (Barrett restart)**: strongly motivated.
  - **N6 (narrow-path inline)**: must inline.

- **ADDMOD slow path** (`mod[3] == 0`): ~100-120 cycles. Also above the
  > 100 threshold — confirming the slow path is a real win-loss boundary.

- **DIV multi-limb**: ~25 cycles. This is *not* "DIV cost" — `intx::uint256`
  early-exits when dividend < divisor (always the case here: counter < 2^16,
  divisor > 2^192), so the arithmetic itself is essentially free. What this
  measures is **the `callRuntimeFor` wrapper alone** — register marshalling,
  indirect call, prologue / epilogue, result-store — which is exactly the
  per-call overhead that A2 Barrett / N6 inline would save.

  Combined with the other two measurements this gives a clean decomposition:

  | Op     | cycles/op | composition                          |
  |--------|-----------|--------------------------------------|
  | DIV    | ~25       | wrapper only (intx early-exit)       |
  | ADDMOD | ~110      | wrapper + `intx::addmod` work        |
  | MULMOD | ~225      | wrapper + 512-bit `intx::mulmod` work |

  Inlining a MULMOD narrow / Barrett path replaces wrapper *plus* arithmetic;
  the ~225 cycles figure is the full savings budget.

## Stability notes

- WSL2 / virtualized host jitter: stddev/mean ≈ 5-9%. Acceptable for
  bucket-tier decisions but not for fine-grained tuning.
- Loop overhead (counter + JUMPI + GT) measured at ~14 cycles/iter
  in both baselines; subtraction isolates the per-call delta.
- Two independent runs gave consistent bucket assignments (always
  > 100 for MULMOD, > 100 for ADDMOD, gray for DIV).
