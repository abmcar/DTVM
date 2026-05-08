# WISA 2026 论文修订计划（导师 review 2026-05-08；二轮 reviewer Opus 收口）

> 论文源：`paper/wisa2026-en/`（worktree `wisa2026-submit`，HEAD `a732290`）
> 当前 PDF：12 页 LNCS，章节 `01-intro / 02-problem / 03-method / 04-evaluation / 05-related / 06-conclusion / 07-availability`
> 约束：必须保持 LNCS 12 页上限；所有引用 key 不变（只动顺序与展示）；保持双盲匿名

## 1. 导师反馈逐条对应当前状态

| # | 反馈 | 当前论文实际状态 | 是否需要改 |
|---|------|------------------|-----------|
| F1 | Related Work 应放 §2，不应放第 5 部分 | §5 是 Related Work + Limitations 混合块 | 是（结构性改动） |
| F2 | Contributions 不要列要点，要写连贯段落 | §1 行 41-61 用 `\begin{enumerate}` 列了 3 条 | 是 |
| F3 | 参考文献按号小到大排，且大多数应在 intro/related | bib 用 `splncs04`（按作者字母排），文中编号乱跳 | 是 |
| F4 | 不要满文 `§4.1`、`§5.2` 这种交叉引用；只在 intro 末尾给 roadmap，正文别再点 | 全文 **26** 处章节交叉引用（`\S\ref` × 25 + `Section~\ref` × 1，含 `02-problem.tex:39`），集中在 §3 / §4 | 是 |
| (附) | Conclusion 不要大篇幅写 limitation/future work | §5.3 Limitations 35 行 + §6 Conclusion 13 行（有两段 future work） | 是 |

## 2. 目标章节结构（方案 B：严格 5 段，§2 严格 related-work-shaped）

```
§1 Introduction        — 含 prose contributions 与 1 句 roadmap
§2 Related Work        — 严格只放别人做了什么 + 我们怎么不同
                          §2.1 Peephole optimization frameworks (旧 §5.1)
                          §2.2 Consensus-critical verification (旧 §5.2，
                               已含 7/83 vs 7/14 比较；intro/eval 的两处指针都删而不是迁)
                          删掉旧 §5.3 Limitations and Future Work
§3 Method              — 开篇 motivation: U256 4-limb 硬件代价 + cannot-express 表
                            (从旧 02-problem.tex 整合，含 "five lines vs single ADC" 句)
                          §3.1 Bit-vector model (旧 §3.1)
                          §3.2 Enumeration & admission (旧 §3.2)
§4 Evaluation          — 旧 §4
§5 Conclusion          — 压成 1 段；强调创新 + 意义 + suite-coverage 限定句；未来工作一句话带过
§6 Data Availability   — 保留
```

> **关键调整（来自 Opus reviewer #1）**：U256 硬件代价段不放 §2.0 — 否则 §2 顶部 30 行讲 "我们的 4-limb adc 长什么样"，advisor 翻 §2 期待 related-work，会读到错位的自家 motivation。U256 代价 + cannot-express 表都进 §3 opener 作 motivation，§2 严格只放别人和定位。

## 3. 分阶段任务清单

### Phase A — 结构重排（拆 §2 Background；动 main.tex 与 sections/）

A1. **`main.tex`** 调换 `\input` 顺序：
   ```diff
   - \input{sections/01-intro}
   - \input{sections/02-problem}
   - \input{sections/03-method}
   - \input{sections/04-evaluation}
   - \FloatBarrier
   - \input{sections/05-related}
   - \FloatBarrier
   - \input{sections/06-conclusion}
   - \input{sections/07-availability}
   + \input{sections/01-intro}
   + \input{sections/02-related}       % 旧 05-related 去 §5.3
   + \input{sections/03-method}        % 旧 03-method + U256 cost + cannot-express opener
   + \FloatBarrier
   + \input{sections/04-evaluation}    % 旧 04-evaluation 不改名
   + \FloatBarrier
   + \input{sections/05-conclusion}    % 旧 06-conclusion 改名
   + \input{sections/06-availability}  % 旧 07-availability 改名
   ```

A2. **`git mv`** 文件重命名（保历史）：
   - `04-evaluation.tex` → `04-evaluation.tex`（不变）
   - `05-related.tex`    → `02-related.tex`
   - `06-conclusion.tex` → `05-conclusion.tex`
   - `07-availability.tex` → `06-availability.tex`
   - `02-problem.tex`    → **删除**（U256 代价段 + cannot-express 引子全部迁到 03-method.tex）
   - `03-method.tex`     → `03-method.tex`（不变；但开篇会改）

A3. **新 `02-related.tex`**（严格 related-work-shaped）：
   - 顶层 `\section{Related Work}`（删 "and Discussion"）
   - `\subsection{Peephole Optimization Frameworks}`（旧 §5.1 原样保留）
   - `\subsection{Verification Work on the Consensus-Critical Path}`（旧 §5.2 原样保留——已含 CAV 2023 的 7/83 vs 7/14 比较，**不再追加 intro/eval 的指针句**，只删指针）
   - **删除整个 §5.3 Limitations and Future Work**（行 52-89）——(1)(2) 不再写（abstract 已涵盖），(3)(4)(5) 弃或归 artifact 附录

A4. **新 `03-method.tex` 开篇插入 U256 motivation**（从旧 02-problem.tex 整合）：

   开篇插入 `\subsection*{Motivation}`（unnumbered）或直接两段开篇（保留 `\section{...Peephole Rewrites}` 之后、`\subsection{...Bit-Vector Semantics}` 之前）：

   - **第一段：U256 硬件代价**（旧 02-problem.tex 行 4-43 浓缩，**保留** 4-limb dMIR/x86 表 + 2.15% 占比 + 25-99.9% hit rate 范围）。约 25 行。
   - **第二段：表达力极限 + cannot-express 表**：
     - 一句话陈述："In LLVM IR, basic arithmetic is defined only on $N$-bit integers; the carry bit must be retrieved through `@llvm.uadd.with.overflow.iN`, which returns a multi-return tuple of `{iN, i1\}`."
     - `\input{tables/tab2-1-cannot-express}` —— 表 1 作为视觉锚
     - **保留** "five lines vs single ADC" 比较句（旧 02-problem.tex L69-73）："In LLVM IR, expressing one carry-propagating addition takes five lines around `uadd.with.overflow`; in dMIR, a single \texttt{ADC} suffices."
     - 收尾过渡："The dMIR bit-vector semantics defined below close this gap with first-class carry and overflow operators."
   - 删掉旧 03-method.tex 行 7-8 的 `\S\ref` 路径图（"The dMIR bit-vector model is in §X; enumeration and admission are in §Y"），改写为自然过渡："We first define dMIR's bit-vector semantics, then describe enumeration and SMT admission, and finally summarize the two-tier rule organization."

A5. **副作用检查**：
   - 旧 `\label{sec:bvmodel}`、`\label{sec:synthesis}`、`\label{sec:perf}`、`\label{sec:correctness}`、`\label{sec:evaluation}`、`\label{sec:related}`、`\label{sec:availability}`、`\label{sec:coverage-scope}` 全保留（防止 hyperref undefined），但**正文里所有指向它们的 `\ref` 都按 Phase C 删除**。
   - `\label{sec:limitations}` 与对应 `\ref` 全部删掉（§5.3 已不存在）。
   - `Table~\ref{tab:cannot-express}` 现在在 §3 开篇，§1 contributions 里的 `Table~\ref{tab:cannot-express}` 仍可前向引用（LaTeX 自动解决）。

### Phase B — Contributions 改散文（动 §1）

B1. **`01-intro.tex` 行 40-61** 改写为三句短散文（参 Opus reviewer #1 建议，避翻译腔与 abstract/conclusion 重复）：
   - 删掉 `\paragraph{Contributions.}` + `\begin{enumerate}` + 3 个 `\item`
   - 替换为：
     ```latex
     Our main contributions are as follows. First, we extend dMIR with
     first-class carry and overflow operators; the four carry-chain
     rewrites of Table~\ref{tab:cannot-express} reduce to single Z3
     bit-vector queries, replacing the indirect LLVM/Alive2 multi-return
     tuple encoding. The two-tier rule set contains 70 SMT-checked dMIR
     rules and 13 differentially tested x86 cleanup rules. Second, an
     enumerable, Z3-admitted search space covers the production set: a
     capacity run admits 853 candidates---98.7\% of algebraic and 21.8\%
     of carry templates---and a coverage run produces 1{,}966 admitted
     rewrites whose canonical left-hand sides match all 70 hand-designed
     production rules. Third, the full 83-rule set delivers a +7.24\%
     geometric-mean speedup on the 27 evmone-bench workloads (95\% CI
     [+2.59\%, +12.11\%], peak +25.79\%) and passes 5{,}884 differential
     Cancun tests with byte-identical final state and matching gas
     accounting (correctness bounded by suite coverage).
     ```
   - 行 26 处 `Alive~\cite{alive}` 在改写后保留；不要再动其他散文。
   - 修订点（Opus #3）：
     - 把 "98.7% algebraic and 21.8% carry under Z3" 改成 "98.7% **of** algebraic and 21.8% **of** carry templates"——避免读成 "98.7-percent-algebraic candidates"
     - 删掉 "post-hoc reachability, not blind recall" 短语——abstract 与旧 §3 已说过两遍，第三处会被 LNCS reviewer 标 boilerplate
     - 删掉收尾论题句 "domain IR with first-class bit-vector operators yields..." —— 与 abstract 末句逐字重复
     - 加上 "(correctness bounded by suite coverage)"——保持与 abstract / conclusion 的限定语一致

B2. **§1 末尾追加 1 行 roadmap**（替代 F4 中所有正文 `\S\ref` 用法）：
   ```latex
   The remainder of the paper surveys related work, presents the dMIR
   bit-vector model and admission pipeline together with the U256
   carry-chain motivation, reports end-to-end performance and
   correctness, and concludes.
   ```
   只此一句，不分点，不带章节号。

### Phase C — 清理交叉引用（**26 处全部清零**；目标 0 残留）

> 导师原话："后文中不要再出现，顺序写就可以"——目标 **0 处** 章节交叉引用，覆盖 `\S\ref{sec:*}`、`Section~\ref{sec:*}` 两种语法形式。`Table~\ref` / `Figure~\ref` / `\label{...}` 全部保留。

| 类别 | 处理 | 例 |
|------|------|----|
| 指向后文（forward） | 直接删除括号引用，必要时改成"later"或拆短语 | `(\S\ref{sec:perf})` → 删 |
| 指向前文（backward） | 改成"as discussed earlier"或直接删 | `methodology of \S\ref{sec:synthesis}` → `the canonical-LHS methodology` |
| 指向 Limitations（已删） | 直接删；§5.3 已不存在 | `(\S\ref{sec:limitations}, item 4)` → 删 |

**逐处清单（26 处；按新文件名 + 内容定位 + 改法；行号会因 Phase A 移动而变，按内容找）**：

`sections/01-intro.tex` (2 处):
- 旧 L53 `(\S\ref{sec:coverage-scope})` — 已在新 contributions 散文里改述（contribution 2 改写后已无此短语），删括号引用
- 旧 L67 `(quantitative comparison in \S\ref{sec:related})` — **整个括号短语删除**；§2.2 已有 7/83 vs 7/14 比较，**不要再追加** intro 这句到 §2.2，是 delete-only

`sections/02-related.tex`（旧 05-related，1 处）:
- 旧 05-related L41 `\S\ref{sec:bvmodel}` — 改 "first-class carry-chain operators"

`sections/03-method.tex`（开篇插入 U256 motivation 后；含旧 02-problem 行 30/38/39/64 + 旧 03-method 行 7/8/32/33/58/73/92/109/111/118/121/131）:

迁入开篇的旧 02-problem 部分（4 处）:
- 旧 02-problem L30 `\S\ref{sec:evaluation}` — 改 "the U256-heavy subset of our 27 benchmarks"
- 旧 02-problem L38 `(\S\ref{sec:perf})` — 删括号
- **旧 02-problem L39 `Section~\ref{sec:perf} reports per-family hit rates...`** — **整句删**，紧跟的下一段已陈述 hit-rate 范围（Opus 抓出的 reviewer #1 漏网点）
- 旧 02-problem L64 `(\S\ref{sec:availability})` — 改 "(archived in our artifact)"

旧 03-method 部分（11 处）:
- 旧 L7-8 整段路径图 `\S\ref{sec:bvmodel}; ... \S\ref{sec:synthesis}` — **整段删掉**（已在 Phase A4 改为自然过渡）
- 旧 L32-33 `\S\ref{sec:correctness} ... \S\ref{sec:limitations}` — 改 "5{,}884 differential tests show no divergence; random-bytecode fuzzing remains future work"
- 旧 L58 `\S\ref{sec:evaluation}` — 删（"Coverage results follow in the next section" 已隐含）
- 旧 L73 `Table~\ref{tab:synth-alive2} (\S\ref{sec:evaluation})` — 保留 `Table~\ref{tab:synth-alive2}`，删 `(\S...)`
- 旧 L92 `\S\ref{sec:evaluation}` — 删
- 旧 L109-111 `\S\ref{sec:correctness} and Table~\ref{tab:correctness}; ... (\S\ref{sec:limitations})` — 改 "covered by the matched 5{,}884-test suites and Table~\ref{tab:correctness}; a formal x86 SMT model remains future work"
- 旧 L118 `(\S\ref{sec:synthesis})` — 删
- 旧 L121 `extension of \S\ref{sec:bvmodel}` — 改 "extension introduced earlier in this section"
- 旧 L131 `\S\ref{sec:limitations}` — 整句尾巴删（直接以前一句作为段落结尾）

`sections/04-evaluation.tex` (4 处):
- 旧 L8 `methodology of \S\ref{sec:synthesis}` — 改 "the canonical-LHS methodology"
- 旧 L22 `methodology of \S\ref{sec:synthesis}` — 同上
- 旧 L86 `(per-pass attribution is future work, \S\ref{sec:limitations}, item 4)` — 改 "(per-pass attribution remains future work)"
- 旧 L104-107 `(timing in \S\ref{sec:evaluation}... \S\ref{sec:perf}). Comparison with the CAV 2023 rule set ... \S\ref{sec:related}` — **整段两行删掉**（CAV 2023 比较已在 §2.2，**不要再追加**，是 delete-only）；CI timing 一句改 "CI re-runs the admission check on every change to the rule file; the timing is distinct from the per-module JIT compile time reported above."

**Phase C 退出门槛（gate）**：
```bash
grep -nE '\\S\\ref|Section~\\ref|\\autoref|\\cref' paper/wisa2026-en/sections/*.tex paper/wisa2026-en/main.tex
```
**输出必须为空**。`\label{...}` 全保留（防 hyperref 报错）；`Table~\ref` / `Figure~\ref` 不属本 grep。

### Phase D — Conclusion 重写（§5）

D1. **`05-conclusion.tex`**（旧 `06-conclusion.tex`）整体替换：
   ```latex
   \section{Conclusion}

   This paper extends a domain-specific JIT IR (dMIR) with first-class
   carry and overflow operators and builds an SMT-checked peephole layer
   on top of it: 70 dMIR rules pass Z3 admission over arithmetic and
   flags, and 13 x86 cleanup rules are covered by matched differential
   testing. The combination delivers a +7.24\% geometric-mean speedup on
   evmone-bench while preserving byte-identical final state across
   5{,}884 Cancun differential tests, with correctness bounded by suite
   coverage. Future work includes a formal x86 model to replace the
   differential-test fallback at the cleanup layer and cross-engine
   validation against evmone and revm.
   ```
   （目标长度：约 9 行；不再分小节，不再列要点。）
   - **关键修订（Opus #6）**：保留 "with correctness bounded by suite coverage" 限定句——与 abstract 末句一致；删除原 draft 中的 "demonstrating that domain IR... bytecode-level abstractions cannot reach"（与 abstract 重复，且去掉后段落更紧）。
   - 删 §5.3 中 (3) dead-carry inertness、(4) compile attribution、(5) corpus mining provenance — 都不进 conclusion；(5) 太长且属内务，归 artifact 附录文件。
   - (1)(2) **不另写**：scope claim abstract 已涵盖（"only arithmetic and flags are SMT-checked, leaving gas, memory aliasing, and storage unmodeled"），不在 Background 或 conclusion 重提。

### Phase E — 参考文献按引用顺序排（bibliography）

E1. **`main.tex`** 替换 bib style：
   ```diff
   - \bibliographystyle{splncs04}
   + \bibliographystyle{unsrt}
   ```
   `unsrt` 按 `\cite` 出现顺序编号；LNCS 模板也接受（许多 LNCS 论文用 unsrt 替代 splncs04 的字母排）。

   **更稳妥的备选**：保 LNCS 风格但换 `splncsnat` + natbib：
   ```diff
   + \usepackage[numbers,sort&compress]{natbib}
   - \bibliographystyle{splncs04}
   + \bibliographystyle{splncsnat}
   ```
   推荐先试 `unsrt`（零侵入）；如果 LNCS 编辑因风格驳回，再切 `splncsnat`。

E2. **审计未引用条目**。`references.bib` 共 30 条，全文 `\cite` 命中 ~22 条 key（含 §5.2 的 5-key 句束 `maxsmt_evm,vmil24_superinst,ist25_neural_superopt,jar25_proof_producing,scico24_translation_cert`）。
   - 列出未被引用条目：
     ```bash
     grep -oE '\\cite\{[^}]+\}' sections/*.tex \
       | grep -oE '[a-zA-Z_0-9]+' | sort -u > /tmp/cited.txt
     grep -oE '^@[a-zA-Z]+\{[^,]+' references.bib | sed 's/.*{//' | sort -u > /tmp/all.txt
     comm -23 /tmp/all.txt /tmp/cited.txt
     ```
   - **判据（Opus #8 收紧）**：**默认全部删**（cite 总量目前 ~22，删 ~8 后落到 22 仍在 LNCS 常规区间）。例外：仅当二轮 advisor 明确指出"为何没引 X"时再补。不要为了凑数把弱相关条目硬塞 intro。

E3. **执行顺序约束**：E1 改 bib style 之后**先单独跑一次 `latexmk -pdf -bibtex main.tex`**，肉眼对照 PDF 中 `\bibitem` 的卷期/DOI/缩进是否符合 LNCS 视觉（条目列在 References 末页）。不通过立即切 E1 备选 `splncsnat`；通过再做 E2，最后 Phase F 守门——这样 page count 与 bib style 两件事不会撞在一起难定位。

E4. **检查编号视觉**：rebuild PDF 后翻 §1、§2，确认引用编号 `[1][2][3]...` 单调递增；§3-§5 偶尔出现已用过的低编号是正常。

### Phase F — 编译与验收

F1. `cd paper/wisa2026-en && latexmk -pdf -bibtex main.tex` —— 0 error，warnings 不增。
F2. 页数检查：`pdfinfo main.pdf | grep Pages` —— **必须 ≤ 12**。
   - 行数估算（Opus #9 修正）：
     - −73 行（删 02-problem.tex 整份）
     - +25 行（U256 代价段重新插入 §3 opener）
     - +20 行（cannot-express 块重新插入 §3 opener）
     - −38 行（删 §5.3 limitations）
     - −5 行（contributions 散文比 enumerate 紧）
     - −4 行（conclusion 9 行 vs 13 行）
     - 约 ±2 行（Phase C 删 26 个 ref 的微调）
     - **净 ≈ −75 行 ≈ −1.5 页 LaTeX 缓冲**
   - 若超页（不太可能）：先压 §3 opener 的 hit rate 重复段；再考虑 §3.2 enumeration 压缩。
F3. **匿名性回归**：`grep -iE 'abmcar|DTVM|github\.com|@\w+\.\w+' paper/wisa2026-en/sections/*.tex paper/wisa2026-en/main.tex` 必须空。
F4. PDF 视觉检查：
   - §1 Contributions 段是否流畅（无 enumerate 标记）
   - §2 是否就是 Related Work（标题、目录顺序），且 §2 顶部不出现 U256 4-limb 表（已迁 §3）
   - §3-§5 不再出现 `§4.1`、`§5.2`、`Section~\ref{sec:*}` 任意形式 — `grep -E '\\\\S\\\\ref|Section~\\\\ref'` **结果必须空**
   - 文末 References 编号是否随阅读顺序单调递增
   - §5 Conclusion ≤ 1 段、含创新意义+suite-coverage 限定句+1 句 future work
F5. live log 在 `docs/research/directions/peephole-optimization/log.md` 追加一条 "2026-05-08 advisor review round-1 fixes applied"。

## 4. 风险与回退

- **风险 R1**：`unsrt` 改写后 LNCS 模板吐 warning。回退：切 `splncsnat`。Phase E3 已强制单独 build 验。
- **风险 R2**：散文化 contributions 后段落超出 §1 已有空间。回退：把 contributions 段拆成更短句，或允许保留紧凑 `\paragraph{Contributions.}` 标签 + 散文。
- **风险 R3**：删 §5.3 后 `\ref{sec:limitations}` 报 undefined。**Phase C** 已逐处替换；F1 编译时若仍报，grep 漏网点修补。
- **风险 R4**：`git mv` 重命名后 `main.tex` 的 `\input` 路径未同步。Commit 1 必须同时改内容 + `\input` + `git rm 02-problem.tex`，避免半态（见 §5 拓扑修订）。
- **风险 R5**：页数 > 12。降级路径见 F2 备注；按 Opus #9 的 -75 行估算，缓冲充足。
- **风险 R6（Opus #4）**：advisor 二轮可能仍认为 §3 opener 的 U256 motivation 太长。回退：把 U256 4-limb 表浓缩成单段一句话 + 表，省 10 行；或把 hit-rate 段移到 §4 Evaluation 开头。

## 5. 执行 commit 拓扑（Opus #10 修订：避免半态）

> 一个 logical change 一个 commit；不混合 prose 和结构。

```
commit 1  paper(wisa2026): collapse §2 background — create §3 opener motivation,
                            rename sections, drop §5.3 limitations, swap main.tex \input
          (atomic: 全部内容迁移 + git rm 02-problem.tex + git mv 重命名 + main.tex 同步)
commit 2  paper(wisa2026): rewrite §1 contributions as prose + add roadmap
commit 3  paper(wisa2026): remove §-cross-references throughout body (target 0)
commit 4  paper(wisa2026): rewrite conclusion (innovation + suite-coverage caveat + brief future work)
commit 5  paper(wisa2026): switch bibliography to citation-order + cut uncited keys
commit 6  paper(wisa2026): build verification — page count, anonymity, link integrity
```

每个 commit 后跑 `latexmk -pdf -bibtex main.tex` + `pdfinfo main.pdf | grep Pages` 守门。
**Commit 1 是原子结构 commit**，原 plan 把它拆成两个会留下 `\input{sections/02-problem}` 还在但 `02-problem.tex` 已半空的中间态——合并即可；其它 commit 可独立 verify。

## 6. 不在本 plan 范围内

- 内容论证、数据、图表、abstract 本体改写——导师说"内容明天再仔细看后回复"，等他第二轮反馈再动。
- artifact 附录、submission portal 元数据。
- 中文版（已 2026-04-26 dropped）。

---
plan owner：abmcar；review by：导师 round-1（2026-05-08）+ Opus reviewer #1（2026-05-08）；执行窗口：等用户确认后启动。

## 修订历史

- 2026-05-08 v1：初稿，6 段方案 A
- 2026-05-08 v2：用户裁决方案 B（严格 5 段），§2.0 Problem Setting 顶部加 U256 块
- 2026-05-08 v3：Opus reviewer #1 收口——U256 块从 §2.0 迁到 §3 opener；Phase B1 prose 改翻译腔；F4 gate 收紧到 0；补 26 ref 实数 + `02-problem.tex:39` 漏网；E2 默认全删；conclusion 加回 suite-coverage 限定；commit 1+2 合并避半态；保留 "five lines vs single ADC" 句；Codex reviewer #2 两轮跑空，按 3-round cap 停。
