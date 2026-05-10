# G8 — Per-Module JIT Compile Latency

Source: per-module JIT compile-time measurements collected from the
`dtvmapi` JIT unit-test harness (n=776 modules), full pipeline including
loading, analysis, and JIT code generation. Numbers cited in §4.2
(Compilation Overhead) of the WISA 2026 paper.

## Headline numbers

| metric | value | source |
|---|---|---|
| modules timed | 776 | JIT unit-test suite |
| p50 per-module compile time | 0.45 ms | full-pipeline timing |
| p95 per-module compile time | 0.87 ms | full-pipeline timing |
| p99 per-module compile time (nearest-rank) | 23.59 ms | full-pipeline timing |

## Notes

- p99 (nearest-rank) is dominated by a 1 MB stress fixture; a few earlier
  outliers remain beyond that percentile, so this is an upper-bound
  distribution rather than the incremental rewrite-pass cost.
- Per-pass timing budgets (CI gates) for `dmir_rewrite` and
  `x86_cg_peephole` passes alone are archived under
  `tests/evm_asm/compiler_pass_timing_budget_*.json`; those files report
  per-pass measurements (~0.014 ms p95 for `dmir_rewrite`), which are a
  small fraction of the per-module 0.45 ms p50 reported above.
- Isolating per-pass rewrite latency (rather than the full pipeline) is
  noted as future work in §4.2.

## Reproducibility

The full-pipeline timing harness is not yet archived as a standalone
script under this `data/` directory; reviewers wishing to reproduce can
re-run the JIT unit-test suite with timing instrumentation enabled
against the rules-enabled build referenced in §4.2 Setup.
