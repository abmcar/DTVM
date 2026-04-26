# DTVM Agent Guide

Instructions for AI assistants working in this repo.

## Project Snapshot

- DTVM is a deterministic VM with EVM ABI compatibility; most core code is C/C++ in `src/`.
- Preserve determinism and avoid host-specific, non-deterministic behavior.
- Prefer touching `third_party/` only when explicitly required.

## Repository Map

- `src/`: core runtime, compiler, execution engines
- `tests/`: WAST spec tests (`tests/wast`), EVM spec tests (`tests/evm_spec_test`), dMIR tests (`tests/mir`)
- `docs/`: build and usage guides (`docs/start.md`, `docs/user-guide.md`)
- `evmc/`: EVM compatibility components
- `rust_crate/`: Rust bindings
- `tools/`: helper scripts and utilities
- `docs/`: documentation, module specifications, change proposals, and feature specs

## Build (CMake)

- Default interpreter build:
  - `cmake -B build -DCMAKE_BUILD_TYPE=Debug`
  - `cmake --build build`
- Singlepass JIT:
  - `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DZEN_ENABLE_SINGLEPASS_JIT=ON`
- Multipass JIT (LLVM 15 required; x86-64 only):
  - `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DZEN_ENABLE_MULTIPASS_JIT=ON -DLLVM_DIR=<llvm>/lib/cmake/llvm`
- Common flags: `ZEN_ENABLE_SPEC_TEST`, `ZEN_ENABLE_ASAN`, `ZEN_ENABLE_JIT_LOGGING`, `ZEN_ENABLE_JIT_BOUND_CHECK`

## Tests

- Spec tests require `ZEN_ENABLE_SPEC_TEST` at build time.
- Run from build output:
  - `ctest --verbose`
  - `./build/specUnitTests <mode>` where mode is `0` (interpreter), `1` (singlepass), `2` (multipass)
  - `./build/specUnitTests <case> <mode>` for a single `.wast` case (omit suffix)
- WAST test sources live under `tests/wast` (see `src/tests/CMakeLists.txt` for categories).
- MIR tests:
  - `pip install lit`
  - `cd tests/compiler && ./test_mir.sh` (also see `docs/start.md`)

## Change Discipline

- Keep edits minimal and localized; follow existing patterns.
- Update or add tests when behavior changes; call out if tests were not run.
- When asked to commit, follow `docs/COMMIT_CONVENTION.md`.

## Workflow

Use the `dev-workflow` skill for feature development. It covers the full cycle: propose, plan, implement, verify, and archive.

For archiving completed features, use the standalone `archive` skill.

## Change Decision Tree

```
New requirement?
├─ Bug fix (restore intended behavior)? -> Fix directly
├─ Formatting/comments/typos?           -> Fix directly
├─ New feature/capability?              -> Create change proposal in docs/changes/
├─ Breaking change?                     -> Create change proposal in docs/changes/
├─ Architecture change?                 -> Create change proposal in docs/changes/
└─ Uncertain?                           -> Create change proposal (safer)
```

## Change Proposals

New changes go in `docs/changes/YYYY-MM-DD-<slug>/README.md`.

Choose a tier:
- **Full**: architecture changes, cross-module impact, new capabilities
- **Light**: single-module improvements, well-scoped enhancements

See `docs/changes/README.md` for naming conventions and status definitions.

## Module Consultation

Before modifying code in a module, consult `docs/modules/<module>/spec.md` for boundaries, API contracts, and invariants.

## General Guidelines

- Do not duplicate module SSOT content in change proposals; use references
- When code conflicts with specifications, code takes precedence, but update specs to stay in sync
- Follow `docs/COMMIT_CONVENTION.md` for commit conventions
- Maintain determinism: avoid introducing host-specific non-deterministic behavior
- Prefer modifying code within `src/`; only modify `third_party/` when explicitly required

## Agent Skills

Skills are defined in `.agents/skills/` (single source of truth). `.claude/skills/` contains auto-generated mirrors for Claude Code; do not edit mirrors directly. After modifying any skill, regenerate mirrors:

```bash
python3 .agents/tooling/generate_skill_mirrors.py
```

## Documentation Pointers

- Overview: `README.md`
- Build/testing: `docs/start.md`
- Usage details: `docs/user-guide.md`


<claude-mem-context>
# Memory Context

# [DTVM/wisa2026-submit] recent context, 2026-04-26 12:02pm GMT+8

Legend: 🎯session 🔴bugfix 🟣feature 🔄refactor ✅change 🔵discovery ⚖️decision 🚨security_alert 🔐security_note
Format: ID TIME TYPE TITLE
Fetch details: get_observations([IDs]) | Search: mem-search skill

Stats: 50 obs (22,999t read) | 1,232,500t work | 98% savings

### Apr 24, 2026
S756 bench-compare 耗时估算与提速方案分析 (Apr 24, 11:41 PM)
### Apr 25, 2026
S802 DTVM bench-compare 命令优化：基线快速路径 + 并行构建 + 漂移警告 (Apr 25, 2:28 PM)
S805 DTVM bench-compare.md workflow优化：基线快速路径、并行构建、漂移警告 (Apr 25, 2:44 PM)
S870 simplify + codex review bench-compare.md — 对 DTVM /bench-compare 命令进行简化和代码审查，修复并发构建退化、fast-path 漏洞等问题 (Apr 25, 2:45 PM)
S909 DTVM U256 研究成果梳理 — 三大核心方向目录结构定位 (Apr 25, 2:58 PM)
### Apr 26, 2026
S913 DTVM U256 研究成果梳理 — 为导师准备问题凝练、解决方案、当前成果与未来期望的结构化汇报材料 (Apr 26, 12:07 AM)
S943 DTVM U256 优化研究多引擎并行分析 — 制定详细 Spec and Plan 导师汇报材料（会话续接） (Apr 26, 12:07 AM)
544 12:23a 🔵 dMIR opcodes.def 中 OP_evm_* 完整枚举确认 — 仅 6 个 EVM 专用 pseudo-op
545 " 🔵 dmir-u256-composite-extension 完整结论确认 — Phase 1+2+3a 性能与验证数据
546 " 🔵 PR #458 当前状态为 OPEN — T1-T5 尚未合入主干
547 " 🔵 U256 JIT IR 层优化文献空白 web 搜索确认无直接竞争者
548 " 🔵 EVM handler 函数架构确认 — 基于 handle* 模板函数而非 case Opcode:: dispatch
551 12:24a 🔵 Track A 多项候选已在主干合入 — MUL/SUB/DIV/value-range 均已落地
552 " 🔵 WISA 2026 论文当前状态 — 7.24% geo-mean、70+13 规则、5884 差分测试
553 " 🔵 Phase 3a Class N 规则 5-rule shortlist 确认 — R3/R4 需多节点结构匹配器
554 " 🔵 Hydra/Iago/Lampo 三个 peephole synthesis 工具文献验证确认
555 " 🔵 dmir_rewrite.h 当前架构 — JSON 驱动生成 + OP_evm_u256_bitwise_result 专用 dispatch
S973 Agent 5 确认 WISA 2026 论文整合策略 — 混合方案：保留 +7.24% 数据，加入 Phase 3a，删除 Track A (Apr 26, 12:29 AM)
556 12:33a 🔵 Track A ROI Analysis: SUB→SBB and MUL→MULX Already Shipped, Two Candidates Remain
557 12:34a 🔵 SUB→SBB and MUL→MULX Commits Verified in Repo; Tier-1 v2 Draft Location Confirmed
558 12:39a ✅ DTVM U256 导师汇报材料整理请求 — 问题凝练 / 方法 / 成果三段式框架
559 12:40a ✅ DTVM U256 导师汇报 Spec.md 写入完成 — 五节完整研究规格文档
569 12:42a ✅ DTVM U256 导师汇报 Plan.md 写入完成 — 含 Track A/B 路线图、WISA 论文整合计划与风险清单
570 12:43a ✅ DTVM U256 导师汇报目录完成 — README.md 索引与6个关键决策点
571 " 🔵 U256 研究 Agent 1（source-quant）中途终止 — 部分覆盖缺口已记录
572 12:44a 🔵 DTVM U256 文献竞争格局完整调查 — Agent 2 确认三处必须修正的论文错误
573 12:47a 🔵 Agent 3 完成 Track A 残余 ROI 路线图 — 含逐行源码位置与 DIV/MULMOD 精确周期计数
578 12:49a 🔵 Agent 4 完成 Track B 生产规则发射计划 — DSL 扩展架构与5条规则优先级清单
579 12:51a ⚖️ Agent 5 确认 WISA 2026 论文整合策略 — 混合方案：保留 +7.24% 数据，加入 Phase 3a，删除 Track A
S1056 DTVM Session Issues Directory — 4 Open Issues Found (Apr 26, 12:51 AM)
593 10:22a 🔵 WISA 2026 Paper — Fourth Independent Review Scope Definition
597 10:23a 🔵 WISA 2026 Paper Diff — Key Changes Identified in submit/wisa2026 Branch
600 " 🔵 WISA 2026 Paper Diff Part 2 — Section 4–6 and Related Work Changes
607 10:26a 🔵 WISA 2026 Paper Fourth Independent Review — Cross-Cutting and LNCS Compliance Angle
608 " 🔵 WISA 2026 Paper — Accumulated Multi-Review Issue Register: 7 Blockers/Majors Across 4 Reviews
612 10:27a 🔵 WISA 2026 Paper — 5884 "Byte-Identical" Tests Are DTVM Self-A/B, Not Cross-VM Consensus Validation
613 " 🔵 WISA 2026 Paper — LaTeX Compilation Log Confirms Multiple Overfull/Underfull hbox Warnings
614 " 🔵 WISA 2026 Paper — CAV'23 Albert et al. Rule Count: N=14 (Artifact) vs 15 (PDF Prose)
615 10:28a 🔵 WISA 2026 Paper — §5 ADCX/ADOX Factual Claim Verified Against Source Code
617 10:29a 🔵 WISA 2026 Paper — splncs04.bst Confirmed Present in Repo and Uses Author-Year-Title Sort
620 10:30a 🔵 WISA 2026 Paper — Potential Benchmark Count Inconsistency: U256-heavy Listed as 1–11 in Figure but "14" in §2
642 10:42a 🔵 DTVM U256 Advisor Briefing Bundle — Independent Diagnostic Review Commissioned
644 10:43a 🔵 DTVM U256 Advisor Bundle — Full Content Read + Git SHA Verification
645 10:44a 🔵 DTVM U256 Bundle Review — Cross-Check Verification Results: SHAs, Source Locations, File Topology
646 10:45a 🔵 DTVM U256 Bundle Review — R1 Hand-Code, DSL Gap, and Upstream PR Verification
647 10:48a 🔵 DTVM U256 Advisor Bundle — Second-Round Independent Re-Review Commissioned
648 10:49a 🔵 DTVM U256 Advisor Bundle Current File State — Post-Edit Content Confirmed
649 " 🔵 DTVM U256 Bundle Technical Claims — SHA and Source-Code Verification Results
650 10:50a 🔵 DTVM U256 Advisor Bundle — Agent Reports Cross-Check Completed
651 10:51a 🔵 DTVM U256 Bundle Review — Branch Context and Source Claim Verification Complete
652 " 🔵 dmir_rewrite.h / dmir_rewrite_rules.json / generate_dmir_rewrite.py Confirmed on feat/peephole-next
653 10:54a ⚖️ DTVM U256 导师汇报文档束 — 独立第二引擎散文审查任务委托
654 10:55a 🔵 DTVM U256 Spec.md 全文内容 — 导师汇报规格文档核心数据确认
655 " 🔵 DTVM U256 文档独立审查 — Plan.md 与 README.md 全文内容确认
656 " 🔵 DTVM U256 Spec Spot-check — PR 提交哈希、源码定位与 dmir_rewrite.h 分支状态核实
657 10:56a 🔵 DTVM U256 Spec 审查 — 引用错误与数值一致性全面核实完成
658 10:57a 🔵 DTVM U256 文档审查 — EXP 固定窗口优化在 Plan.md 三方法族中缺失
723 11:55a 🔵 DTVM Session Issues Directory — 4 Open Issues Found
724 " 🔵 Archive Layout Documentation Diverges From Active-Change File Layout (DTVM Issue 2026-04-16)
S1057 DTVM session-issues review — all 4 open rule/skill/hook consistency issues catalogued and presented to user for triage (Apr 26, 11:55 AM)
**Investigated**: All 4 session-issue files in ~/.claude/projects/-home-abmcar-DTVM/session-issues/ were read in full. Issues span April 16–26, 2026, covering skill documentation, rule ambiguity, rule self-contradiction, and hook false-positive behavior.

**Learned**: Issue 1 (2026-04-16, low): Archive layout docs diverge — dev-workflow skill specifies single README.md per change slug, but historical archived entries use multi-file layout (proposal.md, design.md, tasks.md, specs/). Archive skill completion check only matches single-README style.
    Issue 2 (2026-04-22, low): cpp-code-style.md hardcodes "Copyright (C) 2025" with no guidance on whether new 2026 files should use 2025 (year of first publication) or 2026 (year of creation).
    Issue 3 (2026-04-24, medium): dotfiles-portability.md contradicts itself — header says CLAUDE.local.md is synced, but the "acceptable absolute paths" table says it is not synced. DTVMDotfiles/lib/sync_common.sh confirms CLAUDE.local.md IS in MIRRORED_ITEMS and is synced.
    Issue 4 (2026-04-26, medium): dotfiles-sync PostToolUse hook fires "managed file modified" for files under .claude/ that are not yet in the manifest. Hook matches by directory path pattern, but "managed" actually requires manifest membership. Running store.sh per hook advice is a no-op for unbootstrapped files, causing wasted back-and-forth.

**Completed**: All 4 open session issues fully read and summarized. Summary table presented to user with priority and source labels. User prompted to provide triage decision per issue (fix / skip / delete).

**Next Steps**: Awaiting user triage decisions (e.g. "1=skip, 2=skip, 3=fix(a), 4=fix(1)") before executing any fixes. Likely next work: apply selected fixes to rule/skill/hook files and close resolved issue files.


Access 1233k tokens of past work via get_observations([IDs]) or mem-search skill.
</claude-mem-context>