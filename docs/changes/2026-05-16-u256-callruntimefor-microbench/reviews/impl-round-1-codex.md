# Impl review round 1 — skeptic reviewer (Codex persona)

**Reviewer role**: Skeptic. Verify each fixture actually exercises the
`callRuntimeFor` path — not constant-folded, not on a narrow / fast path.
Cite `src/compiler/evm_frontend/evm_mir_compiler.cpp:2479` and related.

**Files under review**: same as round-1-opus.

## Path-coverage audit

### MULMOD — line 2479 (unconditional)

```cpp
// evm_mir_compiler.cpp:2466-2481
EVMMirBuilder::handleMulMod(... ModulusOp) {
  if (MultiplicandOp.isConstant() && MultiplierOp.isConstant() &&
      ModulusOp.isConstant()) {
    // const fold
    return Operand(intxToU256Value(Result));
  }
  const auto &RuntimeFunctions = getRuntimeFunctionTable();
  return callRuntimeFor<...>(RuntimeFunctions.GetMulMod, ...);
}
```

`handleMulMod` has **no narrow path**. Any non-const operand → unconditional
`callRuntimeFor`. The MULMOD fixture pushes counter (DUP1, non-const) +
two PUSH8 constants. Constant-fold branch fails on `MultiplicandOp.isConstant()`.
**Verified hit.**

Counter ValueRange: after the increment `counter += 1`, the counter is
tagged u64-narrow (range U64). `handleMulMod` doesn't check ValueRange,
so the narrow tag is irrelevant. Still goes to callRuntimeFor. **Verified.**

### ADDMOD slow path — line 2444-2455

```cpp
// evm_mir_compiler.cpp:2304-2316: fast-path eligibility
MInstruction *ModHi = Modulus[3];
MInstruction *ModHiNonZero = createInstruction<CmpInstruction>(
    false, CmpInstruction::ICMP_NE, I64Type, ModHi, Zero);
...
MInstruction *FastEligible = ...; // ModHiNonZero && AugendHiLE && AddendHiLE

// evm_mir_compiler.cpp:2444-2455: slow path emits callRuntimeFor
setInsertBlock(SlowBB);
{
  const auto &RuntimeFunctions = getRuntimeFunctionTable();
  U256Inst SlowResult = extractU256Operand(
      callRuntimeFor<...>(RuntimeFunctions.GetAddMod, ...));
  ...
}
```

The fast / slow branch is **dynamic** — emitted at compile time as a
`BrIfInstruction` whose runtime predicate is `FastEligible`. So both
basic blocks are present in the JIT'd code; the slow path is taken
when `mod[3] == 0 || mod[3] < augend[3] || mod[3] < addend[3]`.

The fixture pushes modulus = `PUSH8 0xfedcba9876543210` — a 64-bit value
whose representation has `mod[3] = mod[2] = mod[1] = 0` and
`mod[0] = 0xfedcba9876543210`. Therefore at runtime `ModHi (= mod[3])`
is 0, `ModHiNonZero` is false, `FastEligible` is false, and the slow
path is taken every iteration. **Verified hit.**

Important: there's no const-fold short-circuit at line 2278-2288 either,
because counter is non-const. The function reaches the dynamic
fast/slow branch on every JIT compilation, and the **runtime** check
chooses slow.

### DIV multi-limb branch — line 1842-1849

```cpp
// evm_mir_compiler.cpp:1758-1762: dynamic upper-limbs check
MInstruction *UpperOr = createInstruction<BinaryInstruction>(
    false, OP_or, I64Type, B[1],
    createInstruction<BinaryInstruction>(false, OP_or, I64Type, B[2], B[3]));
MInstruction *HasUpperLimbs = createInstruction<CmpInstruction>(
    false, CmpInstruction::ICMP_NE, I64Type, UpperOr, Zero);
...
// :1842-1849: multi-limb runtime call
setInsertBlock(MultiLimbBB);
const auto &RuntimeFunctions = getRuntimeFunctionTable();
Operand RuntimeResult;
if (WantQuotient) {
  RuntimeResult = callRuntimeFor<...>(
      RuntimeFunctions.GetDiv, DividendOp, DivisorOp);
}
```

Pre-dispatcher checks (lines 1988-2110) for `handleDiv`:
- All-const? counter non-const → fail.
- Power-of-2 const divisor? no (counter | BIG_CONST has bit 256-128 zero) → fail.
- `bothFitU64` AND `!isConstant`? divisor has BIG_CONST upper limbs ORed in,
  so its runtime value has bits in limbs 2 and 3 set; range tagging from
  the OR operation should also reflect that. **Wait** — does the JIT
  *statically* know divisor doesn't fit U64?

Let me verify: `OR` operand range derives from the union of input ranges.
`PUSH32 BIG_CONST` where BIG_CONST = `0xff...ff00...01` has range U256
(not narrow). `OR(BIG_CONST, counter)` → result has range U256. So
`Operand::bothFitU64` returns false (divisor has range U256). Path
falls through to `handleDivModGeneral` at line 2111. **Verified.**

- `DivisorOp.isConstU64()`? divisor is non-const, fail.
- `DividendOp.isConstU64()`? dividend (counter via DUP2) is non-const, fail.
- Falls to `handleDivModGeneral` (line 2111).

Inside `handleDivModGeneral`:
- Dynamic check `HasUpperLimbs = (B[1] | B[2] | B[3]) != 0`. At runtime,
  divisor = counter | BIG_CONST has B[3] = upper 64 bits of BIG_CONST =
  0xffffffffffffffff (since BIG_CONST starts with `ff..ff` for 16 bytes,
  which spans B[2] and B[3]). So B[3] != 0 every iteration. **Verified
  hit of MultiLimbBB.**

Wait — let me re-verify BIG_CONST's limb structure. The constant is:
```
ffffffffffffffffffffffffffffffff00000000000000000000000000000001
```
That's 32 bytes = 256 bits. In u256 little-endian limb layout (B[0] is
low 64 bits, B[3] is high 64 bits):
- B[0] = low 64 bits  = `0x0000000000000001`
- B[1] = next 64 bits = `0x0000000000000000`
- B[2] = next 64 bits = `0xffffffffffffffff`
- B[3] = high 64 bits = `0xffffffffffffffff`

So `B[2] | B[3] = 0xffffffffffffffff`, `HasUpperLimbs = true`. **Verified.**

### Baselines do NOT hit callRuntimeFor — verified by inspection

- `baseline_empty`: only DUP1 / PUSH8 / POP — no mod / div / mulmod
  opcodes in the bytecode. Cannot reach any handler that calls
  `callRuntimeFor`.
- `baseline_div`: only DUP1 / PUSH32 / DUP2 / POP — no DIV opcode.
  Same conclusion.

Subtraction therefore isolates per-call wrapper + arithmetic cost.

## Suspicion check: ValueRange inference defeating intended paths

For MULMOD: counter has range U64 after the `+1` ADD (range arithmetic
preserves U64 when both inputs are U64). PUSH8 constants are U64. So all
three MULMOD operands have ValueRange U64. **But `handleMulMod` does not
check ValueRange** — it only checks `isConstant()`. So narrow ranges do
not defeat the bench. **Defeat-resistant.**

For ADDMOD: same — fast path check is purely on **modulus's mod[3]
limb value at runtime**, not on ValueRange. **Defeat-resistant.**

For DIV: `bothFitU64` is the only narrow path, and it requires *both*
operands to fit U64. Divisor has BIG_CONST OR'd in, range U256. **Defeat-resistant.**

## Boundary risk: dynamic value-range optimization across BB

The fast/slow branches in handleAddMod and handleDivModGeneral are JIT
basic-blocks; their lowering shouldn't dead-code-eliminate the SlowBB
even though SlowBB is hot on this bench. If a later pass eliminated
SlowBB based on profile guidance, we'd silently measure something
else. **Not currently a risk** — DTVM has no profile-guided JIT.

## Verdict

**GO**. Every test fixture provably hits `callRuntimeFor` for its
intended path via dynamic runtime predicate or unconditional emission.
Baselines provably don't. The measurement is what it claims to be.

## Suggestions (non-blocking)

- Add a defensive log / inspection step in `run_microbench.sh` that
  invokes DTVM with multipass JIT MIR dump on one fixture to *prove* by
  inspection that `evmGetMulMod` symbol resolves in the JIT'd code.
  This would catch future regressions where someone adds a narrow path
  to `handleMulMod` and silently invalidates the bench. **Out of
  scope for this PR**; track as follow-up.
- The README's POP-cycle estimate ("~2-3 cycles") would be tighter if
  it cited a specific cycle count from a separate POP-loop bench. Not
  critical; ≤ 10 cycles error fits well below 100-cycle bucket boundary.
