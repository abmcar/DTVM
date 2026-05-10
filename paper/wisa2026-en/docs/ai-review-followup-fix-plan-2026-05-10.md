# WISA AI-Review Follow-up Fix Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` or an equivalent review-and-verify loop. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the actionable findings from the second review of commit `aadc69b`, bring the generated PDF back to the 12-page LNCS limit, and run a fresh review after rebuilding.

**Architecture:** Prefer semantic compression and claim tightening over formatting hacks. Keep the DTVM/double-blind naming issue out of scope, per user instruction.

**Tech Stack:** LaTeX (`llncs`), BibTeX (`splncs04`), `latexmk`, `pdfinfo`, `pdftotext`, `rg`, Git.

---

## Task 1: Claim and Language Fixes

**Files:**
- Modify: `paper/wisa2026-en/sections/02-related.tex`
- Modify: `paper/wisa2026-en/sections/03-method.tex`
- Modify: `paper/wisa2026-en/sections/04-evaluation.tex`
- Modify: `paper/wisa2026-en/figures/fig4-speedup.tex`
- Modify: `paper/wisa2026-en/references.bib`

- [x] Replace JOG/PyPy related-work wording with source-verified wording:
  - PyPy: integer-peephole DSL with build-time Z3 proofs.
  - JOG: derives Java JIT optimizations and tests from patterns and detects shadowed optimization relations.
- [x] Replace x86 CgIR “risk is bounded” wording with empirical-suite coverage wording.
- [x] Replace “consensus-path determinism” in related work with “consensus-critical JIT rewrite validation.”
- [x] Rewrite compile-latency p99 sentence so nearest-rank p99 and larger outliers are not conflated.
- [x] Protect BibTeX title case for `{DSL}` and `{Java}` under `splncs04`.
- [x] Make Figure 2 caption self-contained by naming `external/total` execution samples.

## Task 2: 12-Page Semantic Compression

**Files:**
- Modify: `paper/wisa2026-en/sections/04-evaluation.tex`
- Modify: `paper/wisa2026-en/sections/05-conclusion.tex`
- Modify: `paper/wisa2026-en/sections/06-availability.tex`

- [x] Compress the correctness paragraph and fold codegen-extension regression into it.
- [x] Compress the conclusion to one concise paragraph plus shadow-audit/future-work sentence.
- [x] Change Data and Code Availability from a standalone starred section to a paragraph-level block if needed for the 12-page budget.
- [x] Preserve all numeric claims: 70 dMIR rules, 13 x86 cleanup rules, 83 total rules, 5,884 differential tests, +7.24% geomean, and `n_{\text{picks}}=0`.

## Task 3: Build and Review Gates

**Files:**
- Generated: `paper/wisa2026-en/main.bbl`
- Generated/tracked: `paper/wisa2026-en/main.pdf`, `main.aux`, `main.blg`, `main.fdb_latexmk`

- [x] Run `latexmk -pdf -bibtex main.tex` from `paper/wisa2026-en`.
- [x] Verify `pdfinfo main.pdf | rg '^Pages:'` reports `Pages:           12`.
- [x] Verify no undefined citations/references, missing BibTeX entries, or overfull hboxes.
- [x] Verify stale/overclaiming wording grep has no hits.
- [x] Inspect pages 11--12 textually with `pdftotext -layout`.
- [x] Run a fresh read-only review after rebuilding, covering technical claims, language, LaTeX/build, and visual/page layout.

## Task 4: Commit Boundary

- [x] Stage only the follow-up plan, changed source files, regenerated `main.bbl`, and final tracked PDF/build artifacts that are intentionally synchronized.
- [x] Leave unrelated `AGENTS.md` untouched.
- [x] Commit with `paper(wisa2026): fix follow-up review issues`.
- [x] Push `submit/wisa2026`.

## Final Review Notes

- First follow-up review found page-budget, wording, bibliography, and visual issues; all were fixed.
- Second follow-up review found loose evaluation paragraph spacing and a list-like conclusion; both were fixed.
- Final follow-up review found bibliography capitalization/line-break issues for `ASPLOS` and `Alive`; both were fixed and rebuilt.
