# WISA 2026 Camera-Ready Plan (2026-07-10)

Paper accepted (scores 4/4/2). This doc records the reviewer-comment
assessment, the camera-ready edits applied, and the items still blocked on
author input.

## Reviewer assessment (verified against paper text, 6-way + adversarial vet)

- **Expert 1 (4分) / Expert 2 (4分)**: every number quoted (70/13/83, 7.24%,
  27, 5,884) matches the paper. E2's three weaknesses (manual rule selection,
  limited per-rule attribution, x86 layer not formal) are limitations the
  paper already discloses in §3 Selection Asymmetry, §4 Setup + shadow audit,
  and §3 Two-Tier Rule Set respectively. No action required.
- **Expert 3 (2分), 5 points**: all five are *accurate restatements of
  limitations the paper already states in its own voice* (tuned-space
  coverage §4 sec:coverage-scope; SMT scope excludes gas/memory/storage/x86,
  stated in §1 and §3.1; 5,884 tests framed as regression evidence in
  Trusted Base). One residual overclaim found: the abstract's "blocks many
  rules from single-query equivalence checks" lacked the body's consistent
  "direct(ly)" qualifier. No factual error in the paper was identified by
  any reviewer; no number inconsistency except missing thousands separators
  (1966/1856/2964).

## Camera-ready edits applied on branch `submit/wisa2026`

1. De-anonymization scaffold in `main.tex`: author block with
   **Chunyan Zhao as corresponding author** (\Letter via marvosym),
   TODO placeholders for the remaining author fields; pdftitle/pdfkeywords
   filled; commented `credits` block (llncs v2.24 \ackname/\discintname).
2. `sections/06-availability.tex`: de-anonymized — DTVM upstream repo URL
   (x86 CgIR tier verified present on upstream/main), artifact on request
   from the corresponding author.
3. `sections/05-conclusion.tex`: future-work sentence extended with
   randomized bytecode fuzzing (mirrors §3 Trusted Base) and held-out rule
   discovery (answers Expert 3 points 2 and 3).
4. Thousands separators harmonized: 1{,}966 / 1{,}856 / 2{,}964 in
   §3/§4 body text (answers the only real inconsistency found).
5. Pending vet at time of writing (apply if approved): abstract
   "direct single-query" qualifier (+ lockstep `abstract_only.tex`);
   §3.1 scope sentence tie-in to differential suites; §3 rule-population
   sentence folding 70+13=83 into the three-population summary
   (answers Expert 3 point 5's "single place" ask).

Page budget: rebuilt at exactly 12 pages (letter) after edits 1-4;
re-verify after each further edit — page 12 bottom has <1 line slack.

## Blocked on author input

- [ ] Full author list: names, order, affiliations, emails, ORCID iDs
      (placeholders marked `TODO` in `main.tex`; pdfauthor too).
- [ ] Acknowledgements / funding text + disclosure-of-interests wording
      (commented credits block in `main.tex`).
- [ ] Camera-ready deadline + any CCF/Springer submission-system steps from
      the acceptance email (not visible from the machine); consent-to-publish
      form is signed outside LaTeX.
- [ ] Decide whether to publish the dMIR rule/Z3 artifact publicly or keep
      "available upon request" in §6.

## Build

`latexmk -gg -pdf main.tex` in `paper/wisa2026-en/`; verify
`pdfinfo main.pdf` reports 12 pages and the build log has no errors.
