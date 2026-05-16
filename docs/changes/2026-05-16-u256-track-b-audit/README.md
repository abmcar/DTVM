# Track B Cherry-Pick Feasibility Audit (read-only)

**Status**: Implemented (audit r1, after Phase 4 review)
**Tier**: Light (doc-only)
**Created**: 2026-05-16
**Branch**: docs/u256-track-b-cherry-pick-audit

## Overview

13 个 evidence-branch commits 保存了 2026-04 的 "Track B" 评估工作 (`evm_u256_bitwise` AND/OR/XOR 并行 lowering、`evm_u256_shl/shr/sar` SHLD/SHRD lowering、`MultiWordAdd/Sub` atomic、`u256-xor-self` synthesized rule)。这些 commits **不在 `origin/main` 上**，但仍在两个 evidence branch 中存活：

- `origin/perf/peephole-rule-expansion` — 含 Chain A 全部 9 commits + Chain C 全部 6 commits
- `origin/feat/cgir-peephole-pr` (PR #439 draft) — 含 Chain B 的 `5738f6f`

本审计**只读**地评估"cherry-pick 到当前 `origin/main` 的可行性"。**Verdict: MAJOR — 不是 cherry-pick，是 re-implementation 任务**。

## Motivation

`docs/research/directions/u256-strength-reduction/analysis/2026-05-12-verified-opportunities.md` §1.C 报告 Phase 1 (bitwise) +3.421% geomean、Phase 2 (shifts) +1.538% additional、blake2b_huff +13%、sha1_shifts +8.5%（2026-04 baseline window 实测）。这是当前 U256 强度削减方向**最大杠杆点**：

- 若 cherry-pick 可行 → 实测 ≥3% geomean → 方向自然收尾，转 next direction
- 若 不可行 → 沿 N1 / C-ISZERO / N5 / N6 走完，6-8 周内拿到 ≥1 个 ≥1% per-bench 信号否则 kill direction (per `2026-05-16-next-steps-direction-scan.md` §5 收尾标准)

因此 audit 结论直接 gate 下一阶段决策。

## Audit Findings

### §1. 13 commits 拓扑

**不是单一线性 chain，是 3 条独立 chain**（用 `git log --pretty=format:'%h %p' -1 <sha>` 抽样验证 parent SHA 全部确认）：

```
Chain A (9 commits, base 856a638):
  856a638 → a34d460 → 3319fdf → 9e00169 → ee0c487 → 54d0aae →
            8dbe3f3 → 19daebe → 69628db → 91174d4

Chain B (1 commit, base 998d9c6, completely independent of A/C):
  998d9c6 → 5738f6f

Chain C (3+ commits, two-rooted; 9e7b2b6 fork + 91174d4-derived fork):
  9e7b2b6 → abfa2a2 → 34656d5
  91174d4 → 49f2ba8 → 60cdf0f → 883a6ac → 250cb7f
```

Chain C 实际有 **两个 base** (`9e7b2b6` 与 `91174d4`)。原 `2026-05-16-next-steps-direction-scan.md` §2 N7 source 列表中 **≥4 个 SHA 顺序错乱**: `54d0aae`、`8dbe3f3`、`91174d4`、`69628db`，真实 chain 序为 `…/ee0c487/54d0aae/8dbe3f3/19daebe/69628db/91174d4`。

### §2. base commit 与 origin/main 距离

| Chain | Base | `rev-list --count base..origin/main` | 是否是 origin/main 祖先 |
|-------|------|--------------------------------------|--------------------------|
| A | `856a638` | 35 | NO |
| B | `998d9c6` | 24 | NO |
| C | `9e7b2b6` / `91174d4` | n/a | NO |

**关键事实（load-bearing）**：Chain A 的 base `856a638` 状态**已经包含 `evm_u256_add/sub` opcodes**（由更早的 commit `5550c9a` 引入；`5550c9a` 是 PR #439 draft 链上的，**不在本 audit 的 13 个 commits 列表内**）。

```bash
$ git show 856a638:src/compiler/mir/opcodes.def | grep evm_u256
# 返回 mul/mul_result + add/add_result + sub/sub_result

$ git show origin/main:src/compiler/mir/opcodes.def | grep evm_u256
# 只返回 mul/mul_result
```

**这就是结构性 blocker**。

### §3. files touched (union of 3 chains)

数据来源：`git show --name-only <sha> | sort -u` for 每个 chain 的全部 commits union。

- **Chain A (10 文件)**: `src/compiler/cgir/lowering.h`、`src/compiler/evm_frontend/evm_mir_compiler.{cpp,h}`、`src/compiler/mir/instruction.h`、`src/compiler/mir/instructions.{cpp,h}`、`src/compiler/mir/opcodes.def`、`src/compiler/mir/pass/visitor.h`、`src/compiler/target/x86/x86lowering.{cpp,h}`
- **Chain B (10 文件，与 Chain A fully overlapping — same 10 paths)**
- **Chain C (9 unique 文件)**:
  - `src/compiler/mir/dmir_rewrite_rules.json`
  - `src/compiler/mir/pass/dmir_rewrite.h`
  - `src/tests/CMakeLists.txt`
  - `src/tests/dmir_validation_tests.cpp`
  - `tools/check_dmir_rewrite_rules.py`
  - `tools/mine_dmir_seed_rules.py`
  - `tools/synthesize_dmir_rules.py`
  - `tools/test_verify_dmir_u256_soundness.py`
  - `tools/verify_dmir_u256_soundness.py`

Chain C 不与 Chain A/B 共享任何 `src/compiler/{mir,evm_frontend,target,cgir}` 表面（其重点在 dmir rewrite 规则与 synthesis 工具）。

### §4. 冲突 surface vs 已合 PR

| PR | Squash SHA | 改 `evm_mir_compiler.cpp` 行数 | 改 cpp+h 行数 | 说明 |
|----|------------|-------------------------------|---------------|------|
| #458 (u256 batch1+2) | `fca0b1a` | `+388 / -31` | `+479 / -34` | 主表面 |
| #487/#494 (zero const fix) | `4bc30f5` | `+12 / -7` | — | 小补丁 |
| #493 (EVMRangeAnalyzer) | `af60336` | `+5 / -2` | — | bulk 在 `evm_analyzer.h` (+546) 与新文件，**不在 evm_mir_compiler.cpp** |

Chain A 改 `evm_mir_compiler.cpp` `+52 / -326`（净负，replace inline expansion with new opcode）。

**评估冲突面**：`evm_mir_compiler.cpp` 同时被 #458 (`+388/-31`) 和 Chain A (`+52/-326`) 改 — sum ≈ 783 lines 在同一文件改动。`evm_analyzer.h` 是 #493 主表面但 Chain A 不动；Chain A 的 `.h` 影响主要在 `evm_mir_compiler.h`。

### §5. cherry-pick dry-run

独立在 `/tmp/dtvm-cherry-test` 与 worktree 双方分别 reproduce：

```bash
git checkout -b cherry-pick-test-N7 origin/main
git cherry-pick --no-commit a34d460  # commit #1 alone
```

立刻在 **5 个文件** 产生 hard semantic conflict（`grep -c '^<<<<<<< ' <file>` 数 hunk）：

| 文件 | hunk 数 | conflict 内容 |
|------|--------|--------------|
| `src/compiler/mir/instruction.h` | 1 | `EVM_U256_ADD/SUB/BITWISE` enum |
| `src/compiler/mir/instructions.cpp` | 2 | dump switch cases |
| `src/compiler/mir/instructions.h` | 1 | `Evm_U256_Add/Sub/Bitwise` classes |
| `src/compiler/mir/opcodes.def` | 1 | `OPCODE(evm_u256_add/sub/bitwise)` lines |
| `src/compiler/mir/pass/visitor.h` | 2 | visit dispatch + virtual methods |

incoming side 携带 **6 opcodes** (`evm_u256_add/add_result/sub/sub_result/bitwise/bitwise_result`)，**不只是** `a34d460` commit message 说的 2 个（bitwise/bitwise_result）。原因：commit `a34d460` 的 base `856a638` 已含 `evm_u256_add/sub`；cherry-pick 把 "delta from main-base" 的 4 个未定义 opcodes 也拉进来。这是 §6 verdict 的 single most decision-relevant insight。

dry-run 后 abort 干净（`git cherry-pick --abort; git branch -D cherry-pick-test-N7`），工作树验证回到 `ci/cache-evmone-bench-fork`。

### §6. Verdict: **MAJOR**

不是 cherry-pick 是 re-implementation：

1. Chain A presupposes `evm_u256_add/sub` opcodes that come from precursor `5550c9a` (PR #439 draft 链)，**not in the 13 listed commits**.
2. `evm_mir_compiler.cpp` 被 #458 (`+388/-31`) 和 Chain A (`+52/-326`) 改动同表面 ≈ 783 lines collision 不是 textual whitespace level。
3. Chain C 的 5 commit 子链 (`91174d4 → 49f2ba8 → 60cdf0f → 883a6ac → 250cb7f`) 依赖 Chain A 尾 `91174d4`，无法独立 cherry-pick。
4. Chain B 与 Chain A files fully overlapping，独立 cherry-pick 也会撞 #458。

### §7. build sanity: SKIPPED

per audit 协议，仅 CLEAN verdict 才跑 build。本审计 verdict 为 MAJOR → skipping。

## Path Forward (建议，本审计不实施)

若仍想保留 Track B perf 杠杆，需要一个独立 spec：

1. **Decide whether `evm_u256_add/sub` pseudo-ops are still needed post-#458**: PR #458 的 batch1+2 已经 inline ADD/SUB barrier 消除策略到 `evm_mir_compiler.cpp` 现有路径上。Chain A 的 add/sub scaffolding 是为了把 ADD/SUB U256 lowered 成专用 opcode；但 #458 实现路径是不引入新 opcode 的。**需要先判定**：保留 #458 路径继续在其上加 bitwise/shift？还是回滚 #458 改用 atomic-opcode 路径？这影响 step 2-5 是否仍然适用。
2. **Re-implement bitwise lowering** (取决于 step 1 决策)：参考 `3319fdf` 的 `target/x86/x86lowering.cpp` 改动思路，但在 #458/#493 之后的 lower 链上重写。
3. **Re-implement shift lowering**：参考 `ee0c487`/`54d0aae` 的 SHLD/SHRD pattern。
4. **Re-implement MultiWordAdd/Sub atomic** (取决于 step 1 决策)：参考 `5738f6f`，或决定保留 #458 路径。
5. **Re-evaluate xor-self synthesized rule** in Chain C：参考 `250cb7f`/`abfa2a2`/`34656d5`，看 #493 EVMRangeAnalyzer 是否已经能 derive 同样优化。
6. **Bench-compare**：post-#458/#493 baseline 实测 Phase 1+2 delta 是否仍然 +4-5%。**显著小于 evidence-branch 数字的概率高**，因为 #458 已经吃掉部分 surplus；具体 fraction 需要实测，audit 不预测。

工作量是 multi-chain re-implementation，远超 §2 N7 原始预设的"audit + cherry-pick"。

## Impact

- **方向决策**：`2026-05-16-next-steps-direction-scan.md` §2 N7 应从 "Tier 1 conditional" demote 到 **Tier 3 研究/重写类**。
- **收尾路径**：next-steps doc §5 收尾标准 中"如果 N7 cherry-pick 落地"分支应删除；当前剩余的 conditional 路径只有"N1 / C-ISZERO / N5 / N6 任一在 27-bench 上 ≥1% per-bench"。
- **kill condition #2** 风险随之上升：剩余候选若全部 sub-1% per-bench → 6-8 周后 kill direction 几率显著增加。

## Compatibility Notes

无代码改动，无 ABI 影响。

## Risks

- **Risk**：reader 读到本 audit 后可能仍想"试试 cherry-pick" — 文档已明确给出 5 文件冲突清单与 6-opcode leakage 解释，应充分。
- **Mitigation**：在 next-steps doc §2 N7 显式 link 本 audit 文件。

## Checklist

- [x] Audit completed: 3 chains / 13 commits 全部覆盖
- [x] Dry-run cherry-pick 已 abort 且无残留
- [x] Conflict 文件清单已 cite (5 files, hunk counts 1/2/1/1/2)
- [x] Verdict (MAJOR) + 一行总结已给
- [x] Path forward 已给（不实施）
- [x] Phase 4 reviewer (Opus + Codex) 已 round 1，all factual edits applied in r1

## References

本 audit 的所有 git command 与 dry-run 都可独立在 `origin/main` 上 reproduce — 不依赖 gitignored 私有路径。验证用关键命令：

```bash
git show 856a638:src/compiler/mir/opcodes.def | grep evm_u256
git show origin/main:src/compiler/mir/opcodes.def | grep evm_u256
git rev-list --count 856a638..origin/main
git rev-list --count 998d9c6..origin/main
git show --name-only <sha> | sort -u   # per Chain C commits
git show --numstat fca0b1a -- src/compiler/evm_frontend/evm_mir_compiler.cpp
git show --numstat af60336 -- src/compiler/evm_frontend/evm_mir_compiler.cpp
git checkout -b temp-verify origin/main && git cherry-pick --no-commit a34d460
# 5 conflicts; then: git cherry-pick --abort; git checkout -; git branch -D temp-verify
```

相关研究方向（在 `docs/research/` nested clone，本 PR 不包含）：
- `docs/research/directions/u256-strength-reduction/` — 方向 SSOT
- `docs/research/directions/u256-strength-reduction/analysis/2026-05-12-verified-opportunities.md` §1.C — Track B Phase 1+2 实测基线
- `docs/research/directions/u256-strength-reduction/analysis/2026-05-16-next-steps-direction-scan.md` §2 N7 — 当前 demote 目标
