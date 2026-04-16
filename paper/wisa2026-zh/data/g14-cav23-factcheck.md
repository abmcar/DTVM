# G14 — CAV 2023 Albert et al. Anchor Fact-Check

Date: 2026-04-17
Sources:
- cav23_rules.json (artifact-faithful, parsed from Coq `optimizations.v` on stack-only branch): N = 14
- PDF Verified_EVM_Block_Opt_CAV2023.pdf (main body, 14 pages): 1 `optimize_*` name mentioned verbatim (`optimize_add_zero`, §4.2); Appendix A lives in the extended version [10], not reproduced in the main PDF
- review.md: "约 15"（二手，弃用；与 PDF §5 正文句 "currently includes 15 simplification rules (see App. A in [10])" 吻合但非 artifact-faithful）

## Paper Metadata (verified from PDF, line numbers refer to /tmp/cav23.txt)

- Title: Formally Verified EVM Block-Optimizations (lines 1–2)
- Authors: Elvira Albert, Samir Genaim, Daniel Kirchner, Enrique Martin-Martin (lines 4–5)
- Venue: CAV 2023, LNCS 13966, pp. 176–189 (line 50)
- DOI: 10.1007/978-3-031-37709-9_9 (line 51)
- Artifact DOI: 10.5281/zenodo.7863483 (line 30, 453)
- bib entry `verified_evm_cav23` match: Y — cav23_rules.json 的 paper_bibkey 字段直接记为 `verified_evm_cav23`，与论文库条目一致

## Authoritative N = 14

口径说明：**以 artifact Coq 源为主（14），PDF 正文声称的 "15" 存在一个无法消除的差异。** 具体判断：

1. `cav23_rules.json` 是直接解析 `github.com/costa-group/forves/tree/stack-only` 的
   `optimizations.v` 文件生成的，每条对应一个 top-level `optimize_<name>` Coq 定义。
   这是最硬的可复现基准。
2. PDF §5 第 454 行原文：
   > "The tool currently includes 15 simpliﬁcation rules (see App. A in [10])."
   但 Appendix A 位于 **extended version [10]**（`AlbertGKMM23_extended.pdf`），不是
   正式出版的主 PDF 的一部分。主 PDF 只具名引用一条规则（`optimize_add_zero`，§4.2
   lines 352/355/357），其余规则以 LHS→RHS 形式非正式出现（"ADD(X,0)=X"，
   "NOT(NOT(X))=X" lines 126, 131）。
3. `cav23_rules.json` 的 `notes` 字段已经明确预警了这种差异：
   > "Paper Appendix A may list additional rules that were not ported to the
   > stack-only release, or may count some rules twice (e.g., left/right variants
   > counted separately). We report the artifact-faithful number (14) here."
4. 论文 artifact 是 CAV 2023 投稿时接受同行复核的部分；"15" 只出现在正文散文
   叙述中。在 §5.2 规模不对等比较里，引 artifact 计数更可防御（可检索、可复现、
   与 DTVM 的 83 条对等口径一致——DTVM 的 83 也是源码枚举，不是散文声称）。

**结论：N = 14（artifact-faithful），和 PDF 正文 15 的 -1 差异单独在脚注交代。**

## Per-Rule Verification (7 overlap candidates)

全部 7 条候选 overlap 规则均在 cav23_rules.json 中命中（index/14, coq_def_line 有
记录），其中仅 `optimize_add_zero` 在 PDF 主文被显式点名，其余 6 条需要通过
artifact Coq 源验证（这符合 PDF 作者的做法：主文仅举例，完整列表在 artifact +
extended version）。

| # | DTVM rule | CAV'23 optimize_* | In main PDF? | In JSON? | Evidence |
|---|---|---|---|---|---|
| 1 | dmir_add_zero | optimize_add_zero | YES (lines 352, 355, 357) | YES (idx 1/14, coq line 779) | PDF §4.2 以 `optimize_add_zero` 作示例具名引用；Coq artifact 首条 |
| 2 | dmir_mul_one_rhs | optimize_mul_one | NO (只在 App. A of [10]) | YES (idx 2/14, coq line 1044) | Coq `optimize_mul_one`: MUL(1,X)/MUL(X,1) → X；与 DTVM `dmir_mul_one_rhs` 语义完全等价 |
| 3 | dmir_mul_zero_rhs | optimize_mul_zero | NO | YES (idx 3/14, coq line 1308) | Coq `optimize_mul_zero`: MUL(0,X)/MUL(X,0) → 0；吸收律 |
| 4 | dmir_or_zero | optimize_or_zero | NO | YES (idx 10/14, coq line 3317) | Coq `optimize_or_zero`: OR(X,0)/OR(0,X) → X；幺元律 |
| 5 | dmir_double_not | optimize_not_not | NO（但 PDF line 126 非正式引用 "NOT(NOT(X))=X" 作示例） | YES (idx 4/14, coq line 1607) | Coq `optimize_not_not`: NOT(NOT(X)) → X；对合律 |
| 6 | dmir_sub_self | optimize_sub_x_x | NO | YES (idx 11/14, coq line 3567) | Coq `optimize_sub_x_x`: SUB(X,X) → 0 |
| 7 | dmir_and_factor_lhs | optimize_and_and_l | NO | YES (idx 13/14, coq line 4208) | Coq `optimize_and_and_l`: AND(AND(X,Y),X) 或 AND(AND(X,Y),Y) → AND(X,Y)；吸收律 |

**"In main PDF?" 的 NO 并非否定**：PDF §4.2 line 352 写明规则完整列表见 App. A
in [10]（extended version）。主 PDF 不展开列表是因为 12 页会议页限，不代表这些
规则不属于 artifact。JSON 命中即 artifact 命中。

## Overlap Computation

- DTVM rule count: 83（70 dMIR + 13 x86；来自 dtvm_rules.json 枚举）
- Overlap: 7（全部位于 dMIR 层，x86 层 13 条无一与 CAV'23 重合——CAV'23 完全
  工作在 EVM 栈层）
- **DTVM-side ratio: 7/83 = 8.43% ≈ 8.4%**
- **Albert-side ratio: 7/14 = 50.0%**
  - 备选（若取 PDF 散文数）: 7/15 = 46.7%

两个百分比讲的是不同事实：
- 8.4% = "DTVM 的 83 条规则里有多少能与 CAV'23 对齐" — **这是摘要/§1.4/§4.3
  脚注/§5.2/§6 五处硬锚的实际语义**。
- 50% = "CAV'23 的 14 条规则里有多少被 DTVM 覆盖"，是对称的另一侧，可用于
  §5.2 说明"不对等规模"时的佐证（CAV'23 只做了少数高频代数规则，DTVM 覆盖
  了其中 1/2）。

## Verdict

- [x] **锚点 8.4% 成立**（全部 7 条在 artifact JSON 中命中且 DTVM 分母 83 已
  核验）。可直接进正文。**无需修订 8.4% 这个数。**

- [x] **建议在 §5.2 或脚注补一句口径说明**，澄清 N=14 的选择依据，防止审稿人
  读到 PDF 正文 "15 simplification rules" 时质疑。推荐原文（中文）：
  > "Albert et al. 的 CAV'23 论文正文声称实现 15 条化简规则（§5），而其
  > 开源 artifact (Coq 源 optimizations.v, stack-only 分支) 实际定义 14 条
  > top-level `optimize_*`。本文以 artifact 可复现计数为准，取 N=14。"

- 需同步修订的位置：**无**。abstract / §1.4 / §4.3 脚注 / §5.2 / §6 五处
  "7/83 = 8.4%" 不变。Tab 4.3 中 CAV'23 overlap 相关行（若有）保持现状。

- **额外建议**（非阻塞）：在 §5.2 规模对照里补 "Albert 14 vs DTVM 83"
  的不对等论据，利用 7/14=50% 讲述"DTVM 覆盖 CAV'23 高频规则的一半"，
  而非仅单侧讲 8.4%。这是 Task 3 Step 3 的料，不在本 G14 范围内。

## Notes for downstream tasks

- **Task 3 Step 3**（§5.2 规模不对等说明）：用 **N = 14** 填入模板；附带可选
  口径脚注见上方推荐原文。
- **Task 9 Codex prompt**：N = 14。若 Codex 提出 "15" 是反例，用本报告 §
  "Authoritative N" 段回复（PDF 15 是散文声称，artifact 14 是可复现源）。
- **carry_zero_only 相关规则**（Task 0 Preflight 提醒）：本 fact-check 不涉及，
  归 Task 3 处理。
- **cav23_rules.json 不需要修改**；其 `notes` 字段已正确预警 15 vs 14 差异，
  现在被本报告引用为正式口径依据。
