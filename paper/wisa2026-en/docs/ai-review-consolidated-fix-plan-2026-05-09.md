# AI Review Consolidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the two AI review passes into a bounded paper-revision checklist that fixes real reviewer-risk issues without adding low-value citations or overclaiming.

**Architecture:** Keep the revision as a text-only paper polish pass. The main edits are boundary clarification, related-work positioning, compile-latency disclosure, and PDF/build consistency checks.

**Tech Stack:** LaTeX LNCS paper under `paper/wisa2026-en/`, BibTeX `splncs04`, existing experiment artifacts under `docs/research/directions/peephole-optimization/submissions/experiments/`.

---

## Consolidated Risk Register

| Priority | Issue | Evidence in current draft | Required action |
|---|---|---|---|
| P0 | x86 CgIR boundary still invites reviewer pushback | `sections/01-intro.tex:51-53` says dMIR rules "compose with" x86 CgIR cleanups; `sections/03-method.tex:172-177` says 13 x86 rules have no SMT script, but not how this limits the proof boundary | Remove proof-composition wording. State that dMIR Z3 guarantees do not compose through x86 CgIR; x86 rules are post-lowering local syntactic rewrites bounded by matched differential tests. |
| P0 | 61 carry-template rejections are mischaracterized | `sections/03-method.tex:119-120` says "mostly width-mismatch edge cases"; artifact taxonomy says drop-carry, wrong polarity, cross-operand substitution | Replace the wording and foreground this as semantic bug catching, not fragility. |
| P1 | Compile-latency paragraph lacks p99/tail and artifact consistency audit | `sections/04-evaluation.tex:85-90` gives only p50/p95; artifact summary reports p99, but raw CSV has cold-start/tail ambiguity | Reconcile raw CSV with the summary, then add a cautious p99/tail sentence. |
| P1 | Figure 2 CI measurement bounds are underspecified | `figures/fig4-speedup.tex:117-120` says bootstrap CI but not whether compile overhead is included | Clarify that bars use evmone-bench `external/total` execution measurements and compile-latency distribution is reported separately. |
| P1 | Related work misses JIT peephole contemporaries | `sections/02-related.tex:7-34` cites LLVM/SMT/LeanMLIR/EVM bytecode work but not PyPy DSL or JOG | Add one compact sentence covering PyPy's Z3-backed integer peephole DSL and JOG's Java JIT pattern-test workflow. |
| P1 | Commit boundary is unsafe in the current dirty paper worktree | `git status --short` shows existing modified LaTeX inputs outside this plan; rebuilding `main.pdf` would bake those inputs into generated outputs | Add a pre-commit gate. Either stage all PDF-affecting inputs together, or do not stage generated PDF/build outputs in this consolidation commit. |
| P2 | Some AI-suggested related work is already covered or too indirect | Souper and LeanMLIR are already cited; C memory model/register allocator/BMC-certification suggestions are not central to dMIR arithmetic rules | Do not add these unless the final page budget unexpectedly allows a broader compiler-verification sentence. |
| Non-action | DTVM naming / double-blind concern | The abstract names `DTVM`, but the user explicitly said this double-blind concern does not need fixing | Do not modify DTVM naming for this pass. Keep any DTVM foundational citation decision outside this consolidation unless requested separately. |

## Files

- Modify: `paper/wisa2026-en/sections/01-intro.tex`
  - Remove proof-composition wording from the contribution summary.
- Modify: `paper/wisa2026-en/sections/02-related.tex`
  - Add only high-signal related work.
  - Avoid adding a broad survey paragraph.
- Modify: `paper/wisa2026-en/sections/03-method.tex`
  - Fix the 61-reject taxonomy.
  - Tighten the dMIR/x86 proof boundary.
- Modify: `paper/wisa2026-en/sections/04-evaluation.tex`
  - Clarify speedup measurement bounds.
  - Add compile-latency p99/tail after auditing the artifact.
- Modify: `paper/wisa2026-en/figures/fig4-speedup.tex`
  - Update caption if the prose sentence in `04-evaluation.tex` is not enough.
- Modify: `paper/wisa2026-en/references.bib`
  - Add PyPy DSL and JOG entries only after verifying metadata from primary sources.
- Generated after build: `paper/wisa2026-en/main.pdf`, `main.aux`, `main.bbl`, `main.blg`, `main.fdb_latexmk`, `main.fls`
  - Stage generated files only if every modified source input that affects them is staged in the same commit.
  - In the current dirty worktree, prefer committing only source edits plus `main.bbl` when citations change; leave `main.pdf` and other generated build logs unstaged unless doing a broader paper snapshot commit.
- Do not modify for this pass: `paper/wisa2026-en/main.tex`, `paper/wisa2026-en/abstract_only.tex`, unless a later non-DTVM edit genuinely requires synchronization.

## Task 1: Fix Carry-Rejection Taxonomy

**Files:**
- Modify: `paper/wisa2026-en/sections/03-method.tex:113-123`
- Source check: `/home/abmcar/DTVM/docs/research/directions/peephole-optimization/submissions/experiments/e2a-synth-stats/README.md:114-129`

- [ ] **Step 1: Replace the inaccurate "width-mismatch" explanation**

Replace the sentence spanning `sections/03-method.tex:117-121`:

```tex
capacity run admits 765/775 algebraic candidates (98.7\%, 10 timeouts)
and 17/78 carry templates (21.8\%, 61 semantic rejections):
carry-template admission is limited by 61 semantic rejections, mostly
width-mismatch edge cases (full breakdown in the artifact), while the
algebraic side has only 10 timeouts and zero semantic rejections.
```

With:

```tex
capacity run admits 765/775 algebraic candidates (98.7\%, 10 timeouts)
and 17/78 carry templates (21.8\%, 61 semantic rejections):
the rejected carry templates are semantic counterexamples, grouped in
the artifact as dropped carry inputs, wrong carry polarity, and
cross-operand substitution. Thus the high rejection rate reflects the
fragility of hand-written carry-chain shortcuts, not parser or width
clerical failures; the algebraic side has only 10 timeouts and zero
semantic rejections.
```

- [ ] **Step 2: Keep the existing counterexample paragraph**

Do not expand `sections/03-method.tex:157-163` unless the page count remains 12 after all edits. The existing `SBB(x,x,cf) -> 0` counterexample already illustrates the taxonomy.

- [ ] **Step 3: Verify no stale wording remains**

Run:

```bash
cd /home/abmcar/DTVM/.worktrees/wisa2026-submit
rg -n 'width-mismatch|clerical|61 semantic|drop|polarity|cross-operand' paper/wisa2026-en/sections/03-method.tex
```

Expected: no `width-mismatch`; the new taxonomy terms appear once.

## Task 2: Tighten dMIR/x86 CgIR Boundary

**Files:**
- Modify: `paper/wisa2026-en/sections/01-intro.tex:51-57`
- Modify: `paper/wisa2026-en/sections/03-method.tex:165-177`
- Optional modify: `paper/wisa2026-en/tables/tab3-1-rule-classes.tex:4-7`

- [ ] **Step 1: Remove proof-composition wording from the introduction contribution**

Replace `sections/01-intro.tex:51-57`:

```tex
\item \textbf{Layered optimization with consensus-path evidence.}
SMT-checked dMIR arithmetic simplifications compose with x86-64 CgIR
cleanups validated by matched differential testing; across 27
evmone-bench workloads, the 83-rule system attains a \textbf{+7.24\%}
geometric-mean speedup and passes \textbf{5{,}884} Cancun differential
tests with byte-identical final state and matching gas accounting within
suite coverage.
```

With:

```tex
\item \textbf{Layered optimization with consensus-path evidence.}
The production system combines SMT-checked dMIR arithmetic
simplifications with x86-64 CgIR cleanups validated by matched
differential testing; across 27 evmone-bench workloads, the 83-rule
system attains a \textbf{+7.24\%} geometric-mean speedup and passes
\textbf{5{,}884} Cancun differential tests with byte-identical final
state and matching gas accounting within suite coverage.
```

- [ ] **Step 2: Replace the two-tier paragraph with explicit proof-boundary wording**

Replace `sections/03-method.tex:172-177`:

```tex
\paragraph{Two-Tier Rule Set.} The dMIR layer handles
machine-independent algebraic simplification (all 70 dMIR rules pass Z3);
the x86 CgIR layer handles hardware-specific cleanup on registers, flags,
and branches (13 syntactic rewrites with \textbf{no separate SMT script},
covered by the matched differential suites summarized in
Table~\ref{tab:correctness}); a formal x86 SMT model remains future work.
```

With:

```tex
\paragraph{Two-Tier Rule Set.} The dMIR layer handles
machine-independent algebraic simplification (all 70 dMIR rules pass Z3).
The x86 CgIR layer runs after lowering and performs local syntactic cleanup
on register, flag, and branch patterns: 13 rewrites have
\textbf{no separate SMT script}, and the dMIR proof is not claimed to
compose through this layer. Its residual machine-code risk is bounded by
the matched differential suites summarized in Table~\ref{tab:correctness};
a formal x86 SMT model remains future work.
```

- [ ] **Step 3: Keep Table 3 classification unless page fit requires caption compression**

Current table caption already says x86 CgIR rules are covered by matched differential tests, not SMT. Do not duplicate the new paragraph in the table unless a reviewer comment specifically targets the table.

- [ ] **Step 4: Verify no overclaim remains**

Run:

```bash
cd /home/abmcar/DTVM/.worktrees/wisa2026-submit
rg -n 'consensus determinism|complete determinism|guarantee|verified.*x86|x86.*verified|compose' paper/wisa2026-en/sections paper/wisa2026-en/main.tex
```

Expected: no statement claims formal verification of the x86 layer or complete consensus determinism.

## Task 3: Clarify Figure 2 Measurement Bounds

**Files:**
- Modify: `paper/wisa2026-en/sections/04-evaluation.tex:62-65`
- Optional modify: `paper/wisa2026-en/figures/fig4-speedup.tex:117-120`

- [ ] **Step 1: Add measurement-bound wording after the setup sentence**

Replace `sections/04-evaluation.tex:62-65`:

```tex
\paragraph{Setup.} Intel Core Ultra 7 265K/Ubuntu 22.04; evmone-bench,
baseline (upstream \texttt{main}) vs.\ rules-enabled build, 20 runs
(30 if CV $\geq3\%$), bootstrap CIs ($B{=}10\,000$), pinned CPU,
turbo off.
```

With:

```tex
\paragraph{Setup.} Intel Core Ultra 7 265K/Ubuntu 22.04; evmone-bench,
baseline (upstream \texttt{main}) vs.\ rules-enabled build, 20 runs
(30 if CV $\geq3\%$), bootstrap CIs ($B{=}10\,000$), pinned CPU,
turbo off. The bars use evmone-bench \texttt{external/total} execution
measurements; compile latency is measured separately below.
```

- [ ] **Step 2: Only edit the figure caption if the prose sentence is not visually close to Figure 2**

If the setup sentence and figure are split by a page break, replace `figures/fig4-speedup.tex:117-120`:

```tex
\caption{Positive point-estimate speedup configurations over upstream main.
Error bars are 95\% bootstrap CIs ($B{=}10\,000$); the dashed line marks
the \textbf{+7.24\%} 27-benchmark geometric mean. Bars are grouped as
U256-heavy, Hash, Arithmetic-Logic, and Control-flow.}
```

With:

```tex
\caption{Positive point-estimate speedup configurations over upstream main.
Error bars are 95\% bootstrap CIs ($B{=}10\,000$) over evmone-bench
\texttt{external/total} execution samples; the dashed line marks the
\textbf{+7.24\%} 27-benchmark geometric mean. Bars are grouped as
U256-heavy, Hash, Arithmetic-Logic, and Control-flow.}
```

- [ ] **Step 3: Verify caption does not become overfull**

Run after rebuild:

```bash
cd /home/abmcar/DTVM/.worktrees/wisa2026-submit/paper/wisa2026-en
rg -n 'Overfull \\\\hbox' main.log
```

Expected: no new overfull line above the known title-block noise level from the existing plan.

## Task 4: Reconcile and Expand Compile-Latency Disclosure

**Files:**
- Modify: `paper/wisa2026-en/sections/04-evaluation.tex:85-90`
- Inspect: `/home/abmcar/DTVM/docs/research/directions/peephole-optimization/submissions/experiments/e5-compile-overhead/measured-20260415.csv`
- Inspect: `/home/abmcar/DTVM/docs/research/directions/peephole-optimization/submissions/experiments/e5-compile-overhead/measured-20260415-summary.md`

- [ ] **Step 1: Audit the raw compile-latency tail before changing prose**

Run:

```bash
cd /home/abmcar/DTVM
awk -F, 'NR>1{printf "%.3f ms  code_size=%s  %s\n", $3/1e6, $2, $4}' \
  docs/research/directions/peephole-optimization/submissions/experiments/e5-compile-overhead/measured-20260415.csv \
  | sort -n | tail -12
```

Expected: a visible tail including 1 MB fixtures and early cold-start records. Use this output to avoid claiming the p99 is representative of every production contract.

- [ ] **Step 2: Replace the compile-overhead paragraph with a cautious paragraph**

Replace `sections/04-evaluation.tex:85-90`:

```tex
\paragraph{Compilation Overhead.}
{\sloppy
Across 776 JIT unit-test modules, median per-module compile time is
0.45\,ms (p95 0.87\,ms) for the full pipeline---an upper bound, not
the incremental rewrite-pass cost (per-pass attribution is future
work).\par}
```

With:

```tex
\paragraph{Compilation Overhead.}
{\sloppy
Across 776 JIT unit-test modules, median per-module compile time is
0.45\,ms (p95 0.87\,ms; nearest-rank p99 23.59\,ms) for the full
pipeline---loading, analysis, and JIT code generation. The p99 tail is
dominated by a small number of large fixtures and cold-start records,
including stress fixtures above realistic on-chain code size; this remains
an upper-bound distribution, not the incremental rewrite-pass cost.
Isolating per-pass rewrite latency is future work.\par}
```

- [ ] **Step 3: Verify the percentile convention before using numeric p99**

Use this command to compute both percentile conventions:

```bash
cd /home/abmcar/DTVM
python3 - <<'PY'
import csv, math
path='docs/research/directions/peephole-optimization/submissions/experiments/e5-compile-overhead/measured-20260415.csv'
xs=sorted(int(r['wall_ns']) for r in csv.DictReader(open(path)))
for label, idx in [('floor-index', int((len(xs)-1)*0.99)), ('nearest-rank', math.ceil(len(xs)*0.99)-1)]:
    print(label, len(xs), idx+1, xs[idx]/1e6)
print('max', xs[-1]/1e6)
PY
```

Expected: floor-index p99 is about `2.090 ms`, nearest-rank p99 is about `23.593 ms`, and max is about `2430.091 ms`. Use nearest-rank p99 in the paper because the existing artifact summary uses that convention; mention the tail caveat in the same sentence.

## Task 5: Add Only High-Signal Related Work

**Files:**
- Modify: `paper/wisa2026-en/sections/02-related.tex:7-22`
- Modify: `paper/wisa2026-en/references.bib`

- [ ] **Step 1: Verify primary-source metadata before editing BibTeX**

Run:

```bash
python3 - <<'PY'
from urllib.request import urlopen, Request
urls = [
  'https://pypy.org/posts/2024/10/jit-peephole-dsl.html',
  'https://2023.issta.org/details/issta-2023-technical-papers/112/Pattern-Based-Peephole-Optimizations-with-Java-JIT-Tests',
  'https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ITP.2024.9',
]
needles = {
  urls[0]: ['Peephole', 'Z3', 'CF Bolz-Tereick'],
  urls[1]: ['Pattern-Based Peephole Optimizations with Java JIT Tests', 'Zhiqiang Zang', '10.1145/3597926.3598038'],
  urls[2]: ['Verifying Peephole Rewriting in SSA Compiler IRs', 'Bhat', 'ITP 2024'],
}
for url in urls:
    req = Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    html = urlopen(req, timeout=20).read().decode('utf-8', 'ignore')
    print(url)
    for needle in needles[url]:
        print(' ', needle, 'OK' if needle in html else 'MISSING')
PY
```

Expected: all needles print `OK`. If the DOI site blocks direct fetch, the ISSTA page still provides the official DOI link; do not invent metadata from a secondary source.

- [ ] **Step 2: Add compact related-work prose**

In `sections/02-related.tex`, replace `lines 15-22`:

```tex
egg~\cite{egg}; SMT-based peephole checking is exemplified by
Alive~\cite{alive}, Alive2~\cite{alive2}, Souper~\cite{souper}, and
PEEK~\cite{peek}; dataflow filtering~\cite{dataflow_pruning} accelerates
search; recent LLM-driven peephole synthesis on LLVM
IR~\cite{lampo2025} is a generative complement to deductive enumeration.
We follow the verify-after-enumeration paradigm but move the checked IR
from LLVM IR to dMIR, addressing the multi-return carry and overflow
encoding limit that motivates this paper.\par}
```

With:

```tex
egg~\cite{egg}; SMT-based peephole checking is exemplified by
Alive~\cite{alive}, Alive2~\cite{alive2}, Souper~\cite{souper}, and
PEEK~\cite{peek}; dataflow filtering~\cite{dataflow_pruning} accelerates
search. Recent JIT-focused systems include PyPy's Z3-backed integer
peephole DSL~\cite{pypy_ruleopt} and JOG's pattern-based Java JIT tests
with shadow validation~\cite{jog_jit}; LLM-driven LLVM peephole
synthesis~\cite{lampo2025} is a generative complement to deductive
enumeration. We follow the verify-after-enumeration paradigm but move
the checked IR from LLVM IR to dMIR, addressing the multi-return carry
and overflow encoding limit that motivates this paper.\par}
```

- [ ] **Step 3: Add BibTeX entries after `dataflow_pruning` or near `lampo2025`**

Use verified metadata from the primary pages. Keep key names exactly as used in the prose:

```bibtex
@misc{pypy_ruleopt,
  title = {A DSL for Peephole Transformation Rules of Integer Operations in the {PyPy} {JIT}},
  author = {Bolz-Tereick, C. F.},
  year = {2024},
  howpublished = {\url{https://pypy.org/posts/2024/10/jit-peephole-dsl.html}}
}
```

For JOG, use the official ISSTA page's title/authors/year and DOI. A verified minimal entry is:

```bibtex
@inproceedings{jog_jit,
  title = {Pattern-Based Peephole Optimizations with Java {JIT} Tests},
  author = {Zang, Zhiqiang and Thimmaiah, Aditya and Gligoric, Milos},
  booktitle = {Proc. ISSTA},
  year = {2023},
  doi = {10.1145/3597926.3598038}
}
```

- [ ] **Step 4: Do not add these AI-suggested items in this pass**

Do not add separate entries for Souper, LeanMLIR, or CAV 2023 EVM block optimization: they are already in the bibliography. Do not add the C integer-pointer memory model, SSA register allocator, or bounded-model-checker certification papers unless a human reviewer explicitly asks for a broader verified-compilers survey; they do not directly clarify dMIR fixed-width arithmetic or carry-chain peepholes.

## Task 6: Build and Regression Checks

**Files:**
- Verify all LaTeX files changed by Tasks 1-5.

- [ ] **Step 1: Snapshot dirty PDF-affecting inputs before rebuild**

Run:

```bash
cd /home/abmcar/DTVM/.worktrees/wisa2026-submit
git status --short -- paper/wisa2026-en
```

Expected: record the output in the revision notes. If modified source inputs outside this plan remain dirty, the later commit must either stage those inputs too as part of a broader paper snapshot, or must not stage generated `main.pdf` / build-log outputs produced by the rebuild.

- [ ] **Step 2: Rebuild the paper**

Run:

```bash
cd /home/abmcar/DTVM/.worktrees/wisa2026-submit/paper/wisa2026-en
latexmk -pdf -bibtex main.tex
```

Expected: exit code 0.

- [ ] **Step 3: Verify page count**

Run:

```bash
pdfinfo main.pdf | grep '^Pages:'
```

Expected: `Pages:          12` or fewer.

- [ ] **Step 4: Verify citation and reference health**

Run:

```bash
rg -n "undefined citations|undefined references|Citation .* undefined|Reference .* undefined|Warning--I didn't find a database entry|There were undefined references|Label\\(s\\) may have changed" main.log main.blg
```

Expected: no output.

- [ ] **Step 5: Skip abstract synchronization unless abstract files were touched**

Only run this check if this pass modifies `main.tex` or `abstract_only.tex` for non-DTVM reasons:

```bash
python3 - <<'PY'
from pathlib import Path
import re
root = Path('/home/abmcar/DTVM/.worktrees/wisa2026-submit/paper/wisa2026-en')
def abstract(path):
    text = (root/path).read_text()
    return re.search(r'\\begin\{abstract\}(.*?)\\keywords', text, re.S).group(1).strip()
print('MATCH' if abstract(Path('main.tex')) == abstract(Path('abstract_only.tex')) else 'MISMATCH')
PY
```

Expected if run: `MATCH`. This pass intentionally does not fix DTVM/double-blind wording and should not force abstract synchronization just for that concern.

- [ ] **Step 6: Verify stale and overclaiming wording**

Run:

```bash
cd /home/abmcar/DTVM/.worktrees/wisa2026-submit
rg -n 'width-mismatch|complete determinism|verified x86|x86.*Z3|guarantee.*x86' paper/wisa2026-en/main.tex paper/wisa2026-en/abstract_only.tex paper/wisa2026-en/sections
```

Expected: no stale `width-mismatch`; no formal-verification claim for x86 CgIR. `DTVM` naming is intentionally not gated in this pass.

- [ ] **Step 7: Record final diff**

Run:

```bash
cd /home/abmcar/DTVM/.worktrees/wisa2026-submit
git diff -- paper/wisa2026-en/sections/01-intro.tex paper/wisa2026-en/sections/02-related.tex paper/wisa2026-en/sections/03-method.tex paper/wisa2026-en/sections/04-evaluation.tex paper/wisa2026-en/figures/fig4-speedup.tex paper/wisa2026-en/references.bib
```

Expected: source diff contains only the planned wording, citation, and generated bibliography updates. Generated files are handled by the commit-boundary policy below.

## Task 7: Commit Boundary

**Files:**
- Stage only files changed by the AI-review consolidation pass.
- Stage generated PDF/build outputs only if their source inputs are staged in the same commit.

- [ ] **Step 1: Re-check dirty source boundary**

Run:

```bash
cd /home/abmcar/DTVM/.worktrees/wisa2026-submit
git status --short -- paper/wisa2026-en
```

Expected: decide one of two policies before staging:
- Source-only consolidation commit: stage planned source edits and `main.bbl` if citation numbering changed; do not stage `main.pdf`, `main.aux`, `main.blg`, `main.fdb_latexmk`, or `main.fls`.
- Broader paper snapshot commit: stage every modified LaTeX/source input that affects `main.pdf`, plus generated outputs.

- [ ] **Step 2: Stage the source-only consolidation revision**

Run:

```bash
cd /home/abmcar/DTVM/.worktrees/wisa2026-submit
git add paper/wisa2026-en/sections/01-intro.tex \
  paper/wisa2026-en/sections/02-related.tex \
  paper/wisa2026-en/sections/03-method.tex \
  paper/wisa2026-en/sections/04-evaluation.tex \
  paper/wisa2026-en/figures/fig4-speedup.tex \
  paper/wisa2026-en/references.bib \
  paper/wisa2026-en/main.bbl
```

Expected: only AI-review consolidation source files and bibliography output are staged; unrelated pre-existing edits and generated PDF/build logs remain unstaged unless using the broader snapshot policy.

- [ ] **Step 3: Commit**

Run:

```bash
git commit -m "paper(wisa2026): address consolidated AI review risks"
```

Expected: commit succeeds after the build and grep gates pass.

## Non-Goals

- Do not formalize the 13 x86 CgIR rules in this revision.
- Do not rerun the 27-benchmark performance suite.
- Do not add a broad verified-compilers survey.
- Do not change the `DTVM` abstract wording for double-blind reasons in this pass; user explicitly marked it as not needing a fix.
- Do not claim full consensus determinism beyond the stated Z3 model and matched differential-suite coverage.

## Self-Review Checklist

- Every issue from both AI reviews maps to a P0/P1/P2 row in the risk register.
- All P0/P1 issues have concrete file edits and verification commands.
- Rejected AI suggestions are documented with a reason.
- No step requires new experiments beyond artifact audit and LaTeX rebuild.
- Final acceptance condition is a 12-page PDF with no undefined citations, no stale `width-mismatch` wording, and no x86 formal-verification overclaim.
