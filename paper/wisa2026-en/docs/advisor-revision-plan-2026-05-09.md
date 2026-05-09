# WISA 2026 Advisor Revision Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` or an equivalent fresh-agent review loop. This plan is tracked with checkbox (`- [x]`) syntax and must be implemented task-by-task.

**Goal:** Revise the WISA 2026 paper so the abstract, introduction, related work, evaluation discussion, conclusion, and future-work framing match the advisor's academic-positioning feedback without rerunning experiments.

**Architecture:** This is a paper-text revision. Keep the existing LNCS structure and current experimental numbers, but change the argumentative framing from an engineering report toward a research contribution: consensus risk -> carry-chain bottleneck -> verification-tool mismatch -> dMIR/Z3 solution -> evaluated system.

**Tech Stack:** LaTeX (`llncs`), BibTeX, `latexmk`, `pdfinfo`, shell text checks.

---

## Scope And Constraints

- Paper source root: `paper/wisa2026-en/`.
- Primary files:
  - Modify: `main.tex`
  - Modify: `sections/01-intro.tex`
  - Modify: `sections/02-related.tex`
  - Modify: `sections/04-evaluation.tex`
  - Modify: `sections/05-conclusion.tex`
  - Leave unless required by review: `sections/03-method.tex`, `sections/06-availability.tex`, tables, figures, and `references.bib`
- Do not rerun performance experiments.
- Preserve verified numeric claims already present in the paper: 70 dMIR rules, 13 x86 CgIR cleanup rules, 83 total rules, 1,966 admitted rewrites, 70/70 coverage, 5,884 Cancun differential tests, and 7.24% geometric-mean speedup.
- Keep the paper double-blind and within the LNCS page limit.
- Do not touch unrelated dirty files, especially generated build outputs and repo-root `AGENTS.md`.

## Review And Iteration Protocol

- [x] **Round 1 plan review:** Ask at least one Codex subagent and one Claude Code review to inspect this plan before implementation.
- [x] **Round 2 plan review:** Revise the plan for actionable feedback, then repeat subagent and Claude review.
- [x] **Round 3 plan review:** Run a final focused review if either Round 1 or Round 2 raises substantive gaps; otherwise record why two rounds are enough.
- [x] **Implementation:** Apply the final plan directly in the LaTeX files.
- [x] **Post-fix review:** Run at least one Codex subagent review and one Claude Code review on the final diff.
- [x] **Post-fix iteration:** Fix all Critical/Important findings. Repeat review until no blocking issue remains.

## Task 1: Abstract Academic Reframing

**Files:**
- Modify: `paper/wisa2026-en/main.tex`

- [x] **Step 1: Rework the abstract's opening motivation**

  Audit the current abstract against this chain and edit only where the
  chain is missing, mis-ordered, or imprecise:
  - consensus-critical EVM JITs need bit-exact rewrites;
  - U256 lowering creates multi-word carry chains;
  - LLVM/Alive2-style multi-return carry modeling creates an abstraction mismatch;
  - dMIR first-class carry operators make these rewrites single SMT-checkable bit-vector queries.

  The current abstract may already satisfy parts of this chain. Do not
  rewrite stable sentences merely for stylistic churn; make U256/carry-chain
  motivation explicit if it is absent.

- [x] **Step 2: Preserve factual result claims**

  Keep the existing result claims in the abstract:
  - offline Z3 admission checks 70 production dMIR rewrites;
  - x86 CgIR has 13 cleanup rewrites validated by matched differential testing;
  - full 83-rule system gives 7.24% geometric-mean speedup on 27 evmone-bench workloads;
  - 5,884 differential Cancun tests report byte-identical final state and matching gas accounting;
  - correctness is bounded by suite coverage.
  - the abstract uses the same two-tier framing as the introduction's
    `Two-tier optimization architecture` contribution, avoiding synonym drift.

- [x] **Step 3: End with the research contribution**

  End the abstract with a non-engineering contribution sentence: the paper addresses a verification bottleneck for consensus-critical EVM JITs by using a domain-specific IR whose carry/overflow semantics are first-class and SMT-checkable.

## Task 2: Introduction Motivation And Contributions

**Files:**
- Modify: `paper/wisa2026-en/sections/01-intro.tex`

- [x] **Step 1: Strengthen the research motivation**

  Ensure the introduction follows this order:
  1. consensus safety risk in replicated EVM execution;
  2. JIT peephole rewrites are in the consensus-critical path;
  3. U256 lowering creates carry-chain patterns on 64-bit hardware;
  4. existing bytecode-level and LLVM/Alive2-style tools leave a gap for JIT backends with carry-chain side products;
  5. dMIR first-class carry operators plus Z3 admission close this gap for arithmetic/flag rewrites.

- [x] **Step 2: Keep a contribution list, but avoid template copying**

  Superseding the temporary prose-only draft, keep the contribution
  material as an `itemize` list because the user restored the
  list-format preference. The content should still be independently
  framed rather than copied from the advisor's suggested three labels.

- [x] **Step 3: Convert contribution content into academic contribution items**

  Replace the prose-only contribution block with a compact `itemize`
  list that states the paper's academic contribution in our own terms.
  The items should cover:

  1. dMIR carry/overflow semantics as first-class verifiable operators
     that avoid indirect LLVM/Alive2 multi-return encodings;
  2. offline Z3 admission and coverage under fixed-width modular
     bit-vector semantics, including the 70 production dMIR rewrites and
     70/70 enumerable coverage;
  3. the two-tier architecture separating SMT-checked dMIR arithmetic
     rewrites from x86-64 CgIR cleanups validated by matched
     differential tests, plus empirical evidence: 7.24% geometric-mean
     speedup, 5,884 Cancun
     differential tests, and the consensus-critical safety/performance
     framing.

  The item labels should not be copied verbatim from the advisor's
  example; prefer labels that reflect the paper's own argument.

- [x] **Step 4: Verify the organization paragraph requirement**

  Confirm `sections/01-intro.tex` contains no roadmap or organization
  paragraph. If absent, record that the advisor requirement is satisfied
  without edits. If present, either delete it or keep exactly one formal
  sentence describing section roles.

- [x] **Step 5: Early page-budget gate after abstract and introduction**

  Run:

  ```bash
  (cd paper/wisa2026-en && latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex)
  pdfinfo paper/wisa2026-en/main.pdf | grep '^Pages:'
  ```

  Expected: exit code 0 and `Pages:` is 12 or less. Record the `Pages:`
  line before continuing.

## Task 3: Related Work Gap Statement

**Files:**
- Modify: `paper/wisa2026-en/sections/02-related.tex`

- [x] **Step 1: Reframe related work around the problem**

  Keep the two existing subsections, but make the opening and closing sentences explicitly organize prior work by:
  - peephole synthesis/verification;
  - LLVM IR-based tools and their carry/overflow modeling limits;
  - EVM bytecode/block verification;
  - post-deployment divergence detection.

  Keep the two `\subsection` boundaries. Map the four buckets in prose:
  peephole synthesis/verification and LLVM IR carry modeling limits live
  under `Peephole Optimization Frameworks`; EVM bytecode/block
  verification and post-deployment divergence detection live under
  `Verification Work on the Consensus-Critical Path`. Add at most one
  bridging sentence per bucket naming its limitation.

- [x] **Step 2: Add a final necessity sentence**

  End the section with one concise gap statement covering all four missing pieces together:

  ```latex
  Existing work therefore does not jointly address JIT-backend rewrites, multi-word carry chains, automated SMT admission, and consensus-path determinism; this combination is the gap targeted by our dMIR-based design.
  ```

  Wording may be improved, but the final sentence must retain those four concepts.

## Task 4: Evaluation Result Analysis

**Files:**
- Modify: `paper/wisa2026-en/sections/04-evaluation.tex`

- [x] **Step 1: Add analytical interpretation after performance numbers**

  Augment the existing hit-rate-vs-speedup paragraph after the main
  speedup numbers. Do not duplicate text already present. Ensure the final
  paragraph explains:
  - the speedup is not merely proportional to rule hit rate;
  - U256-heavy benchmarks benefit from multi-word arithmetic simplification;
  - sha1/blake2b-style gains show the practical value of the x86 cleanup tier;
  - the results support the two-tier architecture rather than a single monolithic SMT-checked layer.

- [x] **Step 2: Make coverage claims read as research evidence**

  Verify the existing production-rule coverage discussion already says
  that 70/70 coverage means the hand-designed production rule set lies
  inside an enumerable Z3-discharged search space. Avoid claiming blind
  generalization or automatic usefulness of all 1,966 admitted rewrites.
  Edit only if that research-evidence framing is missing or weakened.

- [x] **Step 3: Keep no-new-experiment constraint**

  Do not add new numbers. Only analyze the current data already in the paper.

- [x] **Step 4: Early page-budget gate after related-work and evaluation changes**

  Run:

  ```bash
  (cd paper/wisa2026-en && latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex)
  pdfinfo paper/wisa2026-en/main.pdf | grep '^Pages:'
  ```

  Expected: exit code 0 and `Pages:` is 12 or less. Record the `Pages:`
  line before continuing.

## Task 5: Conclusion And Future Work As Academic Problems

**Files:**
- Modify: `paper/wisa2026-en/sections/05-conclusion.tex`

- [x] **Step 1: Keep the conclusion concise**

  Preserve the core result summary, but reduce technology-report phrasing and avoid reciting implementation details that are already in the evaluation.
  Compress the shadow-audit measurement-pipeline paragraph to at most one
  sentence retaining the negative-result framing, or delete it entirely if
  the conclusion reads complete and the page budget is tight.

- [x] **Step 2: Express future work as research questions**

  Delete the existing third future-work item about an extended rule set
  drawn from per-rule attribution analysis. Replace the current
  future-work sentence with academic problem framing around exactly these
  three families:
  - a formal x86 CgIR model and automatic verification for the cleanup tier;
  - cross-implementation equivalence checking across EVM engines such as evmone and revm;
  - automatic generation of carry-chain optimizations via equality saturation or superoptimization.

- [x] **Step 3: Preserve correctness qualification**

  Keep the statement that correctness is bounded by suite coverage.

## Task 6: Verification Gates

**Files:**
- Read: all modified LaTeX files
- Generated: `paper/wisa2026-en/main.pdf`

- [x] **Step 1: Abstract alignment gate**

  Run:

  ```bash
  sed -n '/\\begin{abstract}/,/\\keywords/p' paper/wisa2026-en/main.tex | grep -nEi 'consensus-critical|bit-exact|bit-identical'
  sed -n '/\\begin{abstract}/,/\\keywords/p' paper/wisa2026-en/main.tex | grep -nEi 'U256|256-bit|256 bit'
  sed -n '/\\begin{abstract}/,/\\keywords/p' paper/wisa2026-en/main.tex | grep -nEi 'carry-chain|carry chain|multi-word carry'
  sed -n '/\\begin{abstract}/,/\\keywords/p' paper/wisa2026-en/main.tex | grep -nEi 'LLVM|Alive2|multi-return|abstraction mismatch'
  sed -n '/\\begin{abstract}/,/\\keywords/p' paper/wisa2026-en/main.tex | grep -nEi 'dMIR.*first-class|first-class.*dMIR'
  sed -n '/\\begin{abstract}/,/\\keywords/p' paper/wisa2026-en/main.tex | grep -nEi 'Z3|SMT-checkable|SMT'
  sed -n '/\\begin{abstract}/,/\\keywords/p' paper/wisa2026-en/main.tex | grep -nEi 'correctness (is )?bounded by suite coverage|bounded by (the )?suite coverage'
  ```

  Expected: each command returns at least one abstract line.

- [x] **Step 2: Contribution format gate**

  Run:

  ```bash
  grep -n 'Concretely, we make three contributions' paper/wisa2026-en/sections/01-intro.tex
  perl -0ne 'exit(!(/Concretely, we make three contributions:[\s%]*\\begin\{itemize\}(.*?)\\end\{itemize\}/s && do { my $b = $1; ((() = $b =~ /\\item/g) == 3) && $b =~ /Carry-aware dMIR semantics\./ && $b =~ /SMT admission and production coverage\./ && $b =~ /Layered optimization with consensus-path evidence\./ }))' paper/wisa2026-en/sections/01-intro.tex
  grep -nEi 'carry.*overflow|overflow.*carry|ADC/SBB' paper/wisa2026-en/sections/01-intro.tex
  grep -n 'Z3' paper/wisa2026-en/sections/01-intro.tex
  grep -n '70' paper/wisa2026-en/sections/01-intro.tex
  grep -n '70/70' paper/wisa2026-en/sections/01-intro.tex
  grep -nEi 'two-tier|SMT-checked.*CgIR|CgIR.*differential' paper/wisa2026-en/sections/01-intro.tex
  grep -nEi '7.24|5\{,\}884|5,884' paper/wisa2026-en/sections/01-intro.tex
  ```

  Expected: the contribution content is an `itemize` list with exactly
  three independently named items, preserving the three conceptual
  contribution families plus the key result and correctness evidence.

- [x] **Step 3: Related-work combined-gap gate**

  Run:

  ```bash
  grep -nEi 'JIT[- ]backend|JIT.*code-generation|JIT IR' paper/wisa2026-en/sections/02-related.tex
  grep -nEi 'carry-chain|carry chains|multi-word carry' paper/wisa2026-en/sections/02-related.tex
  grep -nEi 'SMT admission|SMT automatic verification|automated SMT|Z3' paper/wisa2026-en/sections/02-related.tex
  grep -nEi 'consensus-path determinism|consensus determinism|consensus' paper/wisa2026-en/sections/02-related.tex
  tail -n 12 paper/wisa2026-en/sections/02-related.tex | tr '\n' ' ' | grep -Ei 'does not jointly address[^.]*JIT[- ]backends?[^.]*carry[^.]*SMT[^.]*consensus|JIT[- ]backends?[^.]*carry[^.]*SMT[^.]*consensus[^.]*gap'
  ```

  Expected: the section contains all four concepts, and the closing
  paragraph/sentence contains the combined gap in one sentence.

- [x] **Step 4: Section-reference gate**

  Run:

  ```bash
  rg -n -i '(section|sections|sec\.|§)~?\\ref\{sec:[^}]+\}|\\(?:auto|c)?ref\{sec:[^}]+\}' paper/wisa2026-en/sections/*.tex paper/wisa2026-en/main.tex
  ```

  Expected: no output. The rationale is to avoid prose-level section
  cross-pointers in the 12-page LNCS text; table and figure references are
  allowed.

- [x] **Step 5: Future-work gate**

  Run:

  ```bash
  grep -nEi 'formal x86' paper/wisa2026-en/sections/05-conclusion.tex
  grep -n 'CgIR' paper/wisa2026-en/sections/05-conclusion.tex
  grep -nEi 'verification|verified|verify' paper/wisa2026-en/sections/05-conclusion.tex
  grep -n 'evmone' paper/wisa2026-en/sections/05-conclusion.tex
  grep -n 'revm' paper/wisa2026-en/sections/05-conclusion.tex
  grep -nEi 'automatic.*carry-chain.*(equality saturation|superoptimization)|(equality saturation|superoptimization).*carry-chain' paper/wisa2026-en/sections/05-conclusion.tex
  ! grep -nEi 'per-rule attribution' paper/wisa2026-en/sections/05-conclusion.tex
  ```

  Expected: conclusion mentions all three future-work families and no
  longer mentions the removed per-rule-attribution future-work item.

- [x] **Step 6: LaTeX build gate**

  Run:

  ```bash
  (cd paper/wisa2026-en && latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex)
  ```

  Expected: exit code 0 and regenerated `main.pdf`.

- [x] **Step 7: Page-count gate**

  Run:

  ```bash
  pdfinfo paper/wisa2026-en/main.pdf | grep '^Pages:'
  ```

  Expected: `Pages:` is 12 or less. Record the exact `Pages:` line.

- [x] **Step 8: Final review gate**

  Ask one Codex subagent and one Claude Code reviewer to inspect the final diff against this plan and the advisor feedback. Fix all Critical/Important findings and rerun Tasks 6.1-6.7.

## Review Record

- Round 1 Codex subagent review: identified missing structural gates for
  contribution bullets, related-work gap, abstract alignment, and
  future-work completeness; also requested clearer review evidence.
- Round 1 Claude Code review: identified abstract rewrite churn risk,
  missing explicit future-work replacement, missing incremental page gates,
  ambiguous organization-paragraph handling, itemize/page-budget risk, and
  duplicate-risk in evaluation and coverage edits.
- Resolution in this plan revision: Tasks 1-6 now distinguish audit-only
  checks from required edits, add structural gates, add incremental
  page-count gates, explicitly replace the future-work item, and record
  review evidence.
- Round 2 Codex subagent review: identified three remaining gate blockers:
  abstract U256 and carry-chain checks were conflated, the related-work
  closing-gap gate could pass on dispersed terms, and the section-reference
  gate was too narrow for prose `sec:` references.
- Resolution after Round 2 Codex review: the abstract gate now separately
  requires U256/256-bit and carry-chain terms inside the abstract, the
  related-work gate requires all four gap concepts in one closing sentence,
  and the section-reference gate now targets `sec:` labels while allowing
  table and figure references.
- Round 2 Claude Code review: identified a blocking over-escaped Perl
  structural regex, plus brittle related-work, section-reference, and
  future-work regexes.
- Resolution after Round 2 Claude review: the Perl itemize check now uses
  shell-ready single-backslash regex escapes and saves the captured
  itemize block before counting `\item` entries, the related-work
  closing-gap check is newline-normalized and same-sentence scoped, the
  `sec:` reference check uses a dry-run-ready `rg` regex, and the
  future-work gate is split into independent concept checks.
- Round 3 Codex subagent review: identified one remaining brittle abstract
  correctness-bound regex that would reject the compliant phrase
  "correctness is bounded by suite coverage."
- Resolution after Round 3 Codex review: the abstract correctness-bound
  gate now accepts both "correctness bounded by suite coverage" and
  "correctness is bounded by suite coverage" phrasing.
- Final Codex subagent review: no blockers; minor risks were unchecked
  plan boxes and a long future-work sentence.
- Final Claude Code review: no blockers; minor risks were inconsistent
  `5884` formatting in the abstract, missing explicit `blake2b` mention in
  the evaluation interpretation, a harmless `sec:` label prefix, and dense
  abstract prose.
- Final polish: all checklist boxes are checked, the future-work sentence is
  split, the abstract uses `5{,}884`, and the evaluation interpretation now
  names the blake2b x86-cleanup pattern.
- Post-polish final review: after shortening the evaluation interpretation to
  remove the added overfull hbox, both the Codex subagent review and Claude
  Code review reported no blockers. Fresh checks passed: structural gates,
  `git diff --check`, `latexmk`, and `pdfinfo` (`Pages:           12`).
- Follow-up contribution-style revision: the user requested that the
  contribution content not follow the advisor's bullet wording directly and
  not use bullets or numbered points. Claude Code compared prose alternatives
  and recommended a single narrative contribution paragraph, which this plan
  now treats as the active contribution-format requirement.
- Follow-up list-format revision: the user restored the preference for a
  divided contribution list while keeping the requirement that content be
  independently written with Claude discussion. Claude Code recommended four
  academic item families; after the first 4-item draft exceeded the 12-page
  budget, the final version merges architecture and evidence into a compact
  3-item list with independently chosen labels.
