# Track B Cherry-Pick Feasibility Audit (read-only)

**Status**: Implemented (audit-only, no code change)
**Tier**: Light (doc-only)
**Created**: 2026-05-16
**Branch**: docs/u256-track-b-cherry-pick-audit

## Overview

13 个 dangling commits 在 reflog 中保存了 2026-04 的 "Track B" 评估工作 (`evm_u256_bitwise` AND/OR/XOR 并行 lowering、`evm_u256_shl/shr/sar` SHLD/SHRD lowering、`MultiWordAdd/Sub` atomic、`u256-xor-self` synthesized rule)。`docs/research/directions/u256-strength-reduction/analysis/2026-05-16-next-steps-direction-scan.md` §2 N7 把这条路径标为 Tier 1 conditional — audit 是 Tier 1，cherry-pick 后是否升 Tier 取决于实测 delta。

本审计**只读**地评估"cherry-pick 到当前 main 的可行性"。**Verdict: MAJOR — 不是 cherry-pick，是 re-implementation 任务**。

## Motivation

verified-opps §1.C 报告 Phase 1 (bitwise) +3.421% geomean、Phase 2 (shifts) +1.538% additional、blake2b_huff +13%、sha1_shifts +8.5%（2026-04 baseline window 实测）。这是当前 U256 强度削减方向**最大杠杆点**：

- 若 cherry-pick 可行 → 实测 ≥3% geomean → 方向自然收尾，转 next direction
- 若 不可行 → 沿 N1/N2/C-ISZERO/N5/N6 走完，6-8 周内拿到 ≥1 个 ≥1% per-bench 信号否则 kill direction (per §5 收尾标准)

因此 audit 结论直接 gate 下一阶段决策。

## Audit Findings

### §1. 13 commits 拓扑

**不是单一线性 chain，是 3 条独立 chain**：

```
Chain A (9 commits, base 856a638):
  856a638 → a34d460 → 3319fdf → 9e00169 → ee0c487 → 54d0aae →
            8dbe3f3 → 19daebe → 69628db → 91174d4

Chain B (1 commit, base 998d9c6, completely independent of A/C):
  998d9c6 → 5738f6f

Chain C (3 commits, base 9e7b2b6, independent of A/B):
  9e7b2b6 → abfa2a2 → 34656d5
  (加 250cb7f 经由 883a6ac → 60cdf0f → 49f2ba8 → 91174d4 接到 Chain A 尾)
```

原 `2026-05-16-next-steps-direction-scan.md` §2 N7 列表中 `8dbe3f3 / 91174d4 / 69628db` 三 commit 顺序错乱 — 真实 chain 序为 `54d0aae→8dbe3f3→19daebe→69628db→91174d4`。

### §2. base commit 与 origin/main 距离

| Chain | Base | `rev-list --count base..origin/main` | 是否是 origin/main 祖先 |
|-------|------|--------------------------------------|--------------------------|
| A | `856a638` | 35 | NO |
| B | `998d9c6` | 24 | NO |
| C | `883a6ac` / `9e7b2b6` | n/a | NO |

**关键事实**：Chain A 的 base `856a638` 状态**已经包含 `evm_u256_add/sub` opcodes**（由更早的 dangling commit `5550c9a` 引入）。`git show 856a638:src/compiler/mir/opcodes.def | grep evm_u256` 确认 `evm_u256_add/add_result/sub/sub_result` 在 base 已存在。

而当前 `origin/main` 只有 `evm_u256_mul/mul_result`。**这就是结构性 blocker**。

### §3. files touched (union of 3 chains)

- **Chain A (10 文件)**: `src/compiler/cgir/lowering.h`、`evm_frontend/evm_mir_compiler.{cpp,h}`、`mir/instruction.h`、`mir/instructions.{cpp,h}`、`mir/opcodes.def`、`mir/pass/visitor.h`、`target/x86/x86lowering.{cpp,h}`
- **Chain B (10 文件，与 A 几乎完全重叠)**
- **Chain C (22 文件)**: `docs/changes/2026-04-18-...`、`src/compiler/CMakeLists.txt`、`mir/dmir_rewrite_rules.json + v2 (新建)`、`pass/dmir_rewrite.h`、`target/x86/x86_cg_peephole.{cpp,h}`、`tests/CMakeLists.txt`、`dmir_validation_tests.cpp`、`evm_state_tests.cpp`、`utils/evm.cpp`、加 11 个 `tools/*.py`

### §4. 冲突 surface vs 已合 PR

| PR | Squash SHA | 改 `evm_mir_compiler.cpp` 行数 |
|----|------------|-------------------------------|
| #458 (u256 batch1+2) | `fca0b1a` | +419 / -31 |
| #487/#494 (zero const fix) | `4bc30f5` | +12 / -7 |
| #493 (EVMRangeAnalyzer) | `af60336` | 多次 |

Chain A 改 `evm_mir_compiler.cpp` `+52 / -326`（净负，replace inline expansion with new opcode）。
**`evm_mir_compiler.{cpp,h}` 同时被 3 个 PR 和 Chain A+B 改动 → 700+ 行同表面 textual overlap**。

### §5. cherry-pick dry-run

```bash
git checkout -b cherry-pick-test-N7 origin/main
git cherry-pick --no-commit a34d460  # commit #1 alone
```

立刻在 **5 个文件** 产生 hard semantic conflict：

```
src/compiler/mir/instruction.h         (1 hunk)  — EVM_U256_ADD/SUB/BITWISE enum
src/compiler/mir/instructions.cpp      (2 hunks) — dump switch cases
src/compiler/mir/instructions.h        (1 hunk)  — Evm_U256_Add/Sub/Bitwise classes
src/compiler/mir/opcodes.def           (1 hunk)  — OPCODE(evm_u256_add/sub/bitwise) lines
src/compiler/mir/pass/visitor.h        (2 hunks) — visit dispatch + virtual methods
```

incoming side 携带 **6 opcodes** (`evm_u256_add/add_result/sub/sub_result/bitwise/bitwise_result`)，**不只是** `a34d460` commit message 说的 2 个。原因：commit `a34d460` 的 base `856a638` 已含 `evm_u256_add/sub`；cherry-pick 把 "delta from main-base" 的 4 个未定义 opcodes 也拉进来。

abort 干净（`git cherry-pick --abort; git branch -D cherry-pick-test-N7`），工作树验证回到 `ci/cache-evmone-bench-fork`。

### §6. Verdict: **MAJOR**

不是 cherry-pick 是 re-implementation：

1. Chain A presupposes `evm_u256_add/sub` opcodes that come from a dangling precursor (`5550c9a`-equivalent) **not in the 13 listed commits**。
2. `evm_mir_compiler.{cpp,h}` was rewritten by PR #458 / #487 / #493 in the same lines Chain A rewrites by net `-274` — collision is hand-to-hand。
3. Chain C 依赖 Chain A 尾 `91174d4`，所以无法独立 cherry-pick。

### §7. build sanity: SKIPPED

per audit 协议，仅 CLEAN verdict 才跑 build。本审计 verdict 为 MAJOR → skipping。

## Path Forward (建议，本审计不实施)

若仍想保留 Track B perf 杠杆，需要一个独立 spec：

1. **Re-implement scaffolding** in current main：先在 `mir/opcodes.def`/`instructions.{h,cpp}`/`pass/visitor.h` 增加 `evm_u256_add/sub/bitwise` opcodes scaffolding。
2. **Re-implement bitwise lowering**：参考 `3319fdf` 的 `target/x86/x86lowering.cpp` 改动思路，但在 #458/#493 之后的 lower 链上重写。
3. **Re-implement shift lowering**：参考 `ee0c487`/`54d0aae` 的 SHLD/SHRD pattern。
4. **Re-implement MultiWordAdd/Sub atomic**：参考 `5738f6f`。
5. **Re-evaluate xor-self synthesized rule**：参考 `250cb7f`/`abfa2a2`/`34656d5`。
6. **Bench-compare**：post-#458/#493 baseline 实测 Phase 1+2 delta 是否仍然 +4-5% — likely 显著小于 evidence-branch 的数字，因为同表面 surface 已经被 #458 优化吃掉一部分 surplus。

总工作量预估：**multipass×0.5-1 person-week per chain × 3 chains，加上每个 chain 后的 ping-pong bench-compare**。这远超 §2 N7 原始预设的"audit + cherry-pick"。

## Impact

- **方向决策**：next-steps doc §2 N7 应从 "Tier 1 conditional" demote 到 **Tier 3 研究/重写类**，或 **Tier 4 DO NOT START as cherry-pick**。
- **收尾路径**：§5 收尾标准 中"如果 N7 cherry-pick 落地"分支应删除；当前剩余的 conditional 路径只有"N1 / C-ISZERO / N5 / N6 任一在 27-bench 上 ≥1% per-bench"。
- **kill condition #2** 风险随之上升：剩余候选若全部 sub-1% per-bench → 6-8 周后 kill direction 几率显著增加。

## Compatibility Notes

无代码改动，无 ABI 影响。

## Risks

- **Risk**：reader 读到本 audit 后可能仍想"试试 cherry-pick" — 文档已明确给出 5 文件冲突清单，应充分。
- **Mitigation**：在 next-steps doc §2 N7 显式 link 本 audit 文件。

## Checklist

- [x] Audit completed: 3 chains / 13 commits 全部覆盖
- [x] Dry-run cherry-pick 已 abort 且无残留
- [x] Conflict 文件清单已 cite
- [x] Verdict (MAJOR) + 一行总结已给
- [x] Path forward 已给（不实施）

## References

- Source audit: `~/changes/2026-05-16-u256-direction-scan/` 内 Stage 1 reviewer 报告（task `a0fb216c80185901b`）
- Parent direction: `docs/research/directions/u256-strength-reduction/`
- Next-steps scan: `docs/research/directions/u256-strength-reduction/analysis/2026-05-16-next-steps-direction-scan.md` §2 N7
- verified-opps baseline: `docs/research/directions/u256-strength-reduction/analysis/2026-05-12-verified-opportunities.md` §1.C
