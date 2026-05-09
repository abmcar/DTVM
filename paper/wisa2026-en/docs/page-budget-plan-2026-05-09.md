# WISA 2026 12-Page Compression Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` or an equivalent fresh-agent review loop before implementation. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring `paper/wisa2026-en/main.pdf` from 13 pages back to the LNCS 12-page limit without weakening the advisor-requested motivation-before-result logic.

**Status:** Implemented through Task 1 plus final verification. Task 2 and Task 3 were not required after the PDF reached 12 pages.

**Architecture:** Prefer semantic compression over formatting hacks. First remove or fold repeated explanatory prose near the end of the paper, where the current PDF spills only references 18--20 onto page 13. If that is insufficient, compact lower-risk table/float spacing that is already local to the paper.

**Tech Stack:** LaTeX (`llncs`), BibTeX, `latexmk`, `pdfinfo`, `pdftotext`, `rg`.

---

## Evidence

- Initial `pdfinfo paper/wisa2026-en/main.pdf` reported `Pages: 13`.
- Initial `pdftotext -layout -f 13 -l 13 paper/wisa2026-en/main.pdf -` showed only references 18--20 on page 13.
- Final `pdfinfo paper/wisa2026-en/main.pdf | rg '^Pages:'` reports `Pages:           12`.
- Final log checks report no LaTeX hard errors, undefined references/citations, or overfull hboxes.
- The minimum target is to save roughly 10--15 rendered lines before the bibliography.
- Keep all numeric claims unchanged: 70 dMIR rules, 13 x86 cleanup rules, 83 total rules, 1,966 admitted rewrites, 70/70 coverage, 5,884 Cancun differential tests, and +7.24% geometric-mean speedup.

## Task 1: High-Value Content Compression Near Page 11

**Files:**
- Modify: `paper/wisa2026-en/sections/04-evaluation.tex:137-167`
- Modify: `paper/wisa2026-en/sections/05-conclusion.tex:1-21`
- Modify: `paper/wisa2026-en/sections/06-availability.tex:1-9`

- [x] **Step 1: Fold the codegen-extension regression paragraph**

  In `sections/04-evaluation.tex`, replace the long `Codegen-Extension Regression` paragraph with two compact sentences after the correctness paragraph. Preserve only:
  - recursive commutative-aware codegen passes the four differential suites;
  - the `erc20` `ctest` failure is pre-existing, reproduced by the baseline,
    and independent of regenerated production-rule artifacts.

  Target replacement:

  ```latex
  \paragraph{Codegen-Extension Regression.} The recursive
  commutative-aware codegen extension passes the four suites of
  Table~\ref{tab:correctness}. The lone in-tree \texttt{erc20}
  \texttt{ctest} failure is pre-existing, reproduced by the baseline,
  and byte-identical for regenerated production-rule artifacts pre/post patch.
  ```

- [x] **Step 2: Compress the conclusion**

  In `sections/05-conclusion.tex`, keep one result-summary paragraph plus a compact second paragraph that retains the shadow-audit negative result in one sentence and folds future work into the same paragraph. Hard invariant: `sections/04-evaluation.tex` must retain a direct `n_{\text{picks}}=0` / no-statistically-valid-recovery statement, not a softened coverage-only spin.

  Target structure:

  ```latex
  This paper extends dMIR with first-class carry and overflow operators,
  making carry-sensitive EVM JIT rewrites direct Z3 bit-vector obligations.
  The resulting two-tier system admits 70 dMIR rules, validates 13 x86
  cleanup rules by matched differential testing, improves evmone-bench
  by +7.24\% geomean, and preserves byte-identical final state across
  5{,}884 Cancun differential tests.

  The shadow audit adds a stricter measurement path: a recursive matcher
  over commutative subtrees plus bench-paired CI-bounded validation reports
  no statistically valid recovery on the current production set
  ($n_{\text{picks}}=0$), so the result is methodological and negative
  rather than a rule yield. Future work is to formalize the x86 CgIR tier, check
  cross-implementation equivalence across EVM engines such as evmone and
  revm, and generate carry-chain optimizations automatically through
  equality saturation or superoptimization.
  ```

- [x] **Step 3: Prefer semantic compression before changing availability structure**

  First compile after Step 1 and Step 2. If the PDF is already 12 pages,
  leave `sections/06-availability.tex` as a standalone section, allowing
  only local wording/vspace cleanup. If it is still 13 pages,
  verify the WISA/LNCS author instructions before changing availability from
  a standalone unnumbered section to a paragraph. If the venue requires or
  appears to expect a standalone availability section, skip this step and
  continue to Task 2.

  Conditional replacement, only if the venue allows paragraph-level
  availability text:

  ```latex
  \paragraph{Data and Code Availability.}
  Rule sets, offline Z3 admission checks, evaluation scripts, and data
  are available from an anonymous repository upon request to the program
  chairs and will be de-anonymized upon acceptance.
  ```

- [x] **Step 4: Compile and check page count**

  Run:

  ```bash
  cd paper/wisa2026-en
  latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex
  cd ../..
  pdfinfo paper/wisa2026-en/main.pdf | rg '^Pages:'
  ```

  Expected: `latexmk` exits 0. If `Pages: 12`, stop content edits and run final verification. If still 13 pages, continue to Task 2.

## Task 2: Secondary Semantic Compression If Still 13 Pages (Not Required)

**Files:**
- Modify: `paper/wisa2026-en/sections/01-intro.tex:40-57`
- Modify: `paper/wisa2026-en/sections/02-related.tex:5-57`
- Modify: `paper/wisa2026-en/sections/04-evaluation.tex:68-85`

- [ ] **Step 1: Tighten the introduction contribution list**

  Keep the three contribution items, but remove repeated phrases already stated in the preceding paragraphs. Target saving: 2--4 rendered lines.

- [ ] **Step 2: Tighten related work enumerations**

  In `sections/02-related.tex`, shorten long citation lists by grouping examples and keeping only the citations needed to support each related-work bucket. Do not remove a cited paper unless its point is still covered by another retained citation.

- [ ] **Step 3: Tighten §4.2 result interpretation**

  Keep `sections/04-evaluation.tex:55-59` unchanged because it is the
  motivation-before-result lead-in for the performance evaluation. Compress
  only the result-interpretation paragraph after the speedup figure by
  merging the sha1/blake2b examples into one sentence. Preserve the claim
  that hit rate and speedup are decoupled.

- [ ] **Step 4: Compile and check page count**

  Run the same `latexmk` and `pdfinfo` commands as Task 1. Stop when `Pages: 12`.

## Task 3: Formatting-Only Fallback (Not Required)

**Files:**
- Modify only if Tasks 1 and 2 are insufficient: `paper/wisa2026-en/main.tex`
- Modify only if needed: local table files under `paper/wisa2026-en/tables/`

- [ ] **Step 1: Reduce local float spacing conservatively**

  Add only local, reviewable spacing settings near the preamble. Do not change font size.

  Candidate:

  ```latex
  \setlength{\textfloatsep}{8pt plus 1pt minus 2pt}
  \setlength{\intextsep}{8pt plus 1pt minus 2pt}
  ```

- [ ] **Step 2: Remove local positive table vspace**

  In `tables/tab4-1-synth-alive2.tex`, remove the local `\vspace{2pt}` if the table still has acceptable spacing.

- [ ] **Step 3: Compile and inspect**

  Run `latexmk`, `pdfinfo`, and visually inspect the affected table/figure pages. Formatting fallback is acceptable only if the PDF remains readable and does not look obviously squeezed.

## Final Verification

- [x] Run:

  ```bash
  cd paper/wisa2026-en
  latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex
  cd ../..
  pdfinfo paper/wisa2026-en/main.pdf | rg '^(Pages|File size|ModDate):'
  rg -n "LaTeX Error|Fatal error|Emergency stop|Overfull" paper/wisa2026-en/main.log || true
  pdftotext -layout -f 12 -l 12 paper/wisa2026-en/main.pdf - | tail -80
  ```

- [ ] Expected:
  - `latexmk` exits 0.
  - `Pages: 12`.
  - No `LaTeX Error`, `Fatal error`, `Emergency stop`, or `Overfull`.
  - Page 12 ends with references, not an orphaned new page or a visually bad widow/orphan.
