// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "compiler/evm_frontend/evm_analyzer.h"
#include "evm/evm.h"
#include "evm_test_host.hpp"
#include "runtime/evm_module.h"
#include "utils/evm.h"
#include "zetaengine.h"

using namespace zen;
using namespace zen::evm;
using namespace zen::runtime;

#ifdef ZEN_ENABLE_MULTIPASS_JIT

namespace {

std::filesystem::path getEvmAsmDirPath() {
  return std::filesystem::path(__FILE__).parent_path() /
         std::filesystem::path("../../tests/evm_asm");
}

struct EVMExecutionResult {
  evmc_status_code Status = EVMC_INTERNAL_ERROR;
  std::string OutputHex;
  bool JITCompiled = false;
};

// Run `Bytecode` in `Mode`, forcing synchronous multipass compilation so the
// multipass run actually executes JIT code. The default async/multithread
// config can fall back to the interpreter for a single call, which would make
// a differential test vacuous; DisableMultipassMultithread compiles before the
// run so the JIT side is non-vacuous.
EVMExecutionResult runEvmBytecode(const std::string &Label,
                                  const std::vector<uint8_t> &Bytecode,
                                  common::RunMode Mode,
                                  const std::vector<uint8_t> &CallData = {}) {
  EVMExecutionResult Empty;

  RuntimeConfig Config;
  Config.Mode = Mode;
  Config.DisableMultipassMultithread = true; // compile before run -> JIT used

  auto MockedHost = std::make_unique<zen::evm::ZenMockedEVMHost>();
  MockedHost->tx_context.tx_origin = zen::evm::DEFAULT_DEPLOYER_ADDRESS;
  auto RT = Runtime::newEVMRuntime(Config, MockedHost.get());
  if (!RT) {
    ADD_FAILURE() << "runtime create failed: " << Label;
    return Empty;
  }
  MockedHost->setRuntime(RT.get());

  auto ModRet = RT->loadEVMModule(Label, Bytecode.data(), Bytecode.size());
  if (!ModRet) {
    ADD_FAILURE() << "module load failed: " << Label;
    return Empty;
  }
  EVMModule *Mod = *ModRet;

  Isolation *Iso = RT->createManagedIsolation();
  if (!Iso) {
    ADD_FAILURE() << "isolation create failed: " << Label;
    return Empty;
  }

  const uint64_t GasLimit = 0xFFFF'FFFF'FFFF - zen::evm::BASIC_EXECUTION_COST;
  auto InstRet = Iso->createEVMInstance(*Mod, GasLimit);
  if (!InstRet) {
    ADD_FAILURE() << "instance create failed: " << Label;
    return Empty;
  }
  EVMInstance *Inst = *InstRet;
  Inst->setRevision(evmc_revision::EVMC_OSAKA);

  evmc_message Msg = {
      .kind = EVMC_CALL,
      .flags = 0u,
      .depth = 0,
      .gas = static_cast<int64_t>(GasLimit),
      .recipient = {},
      .sender = zen::evm::DEFAULT_DEPLOYER_ADDRESS,
      .input_data = CallData.empty() ? nullptr : CallData.data(),
      .input_size = CallData.size(),
      .value = {},
      .create2_salt = {},
      .code_address = {},
      .code = reinterpret_cast<const uint8_t *>(Mod->Code),
      .code_size = Mod->CodeSize,
  };

  evmc::Result RawResult;
  EVMExecutionResult Exec;
#ifdef ZEN_ENABLE_JIT
  Exec.JITCompiled = Mod->getJITCode() != nullptr && Mod->getJITCodeSize() > 0;
#endif
  EXPECT_NO_THROW({ RT->callEVMMain(*Inst, Msg, RawResult); });
  Exec.Status = RawResult.status_code;
  Exec.OutputHex =
      zen::utils::toHex(RawResult.output_data, RawResult.output_size);
  return Exec;
}

EVMExecutionResult
runEvmBytecodeFile(const std::string &FilePath, common::RunMode Mode,
                   const std::vector<uint8_t> &CallData = {}) {
  EVMExecutionResult Empty;

  std::ifstream Fin(FilePath);
  if (!Fin.is_open()) {
    ADD_FAILURE() << "Failed to open test file: " << FilePath;
    return Empty;
  }

  std::string Hex;
  Fin >> Hex;
  zen::utils::trimString(Hex);
  auto BytecodeBuf = zen::utils::fromHex(Hex);
  if (!BytecodeBuf) {
    ADD_FAILURE() << "Failed to convert hex to bytecode: " << FilePath;
    return Empty;
  }

  return runEvmBytecode(FilePath, *BytecodeBuf, Mode, CallData);
}

// Run `Bytecode` through interpreter and multipass, assert the interpreter
// run succeeds (a shared failure such as out-of-gas would make the comparison
// vacuous), the multipass JIT compiled, and that status + output agree.
// Returns false on any violation so matrix callers can stop flooding output.
bool expectInterpMatchesMultipass(const std::string &Label,
                                  const std::vector<uint8_t> &Bytecode,
                                  const std::vector<uint8_t> &CallData) {
  auto Interp = runEvmBytecode(Label + "_interp", Bytecode,
                               common::RunMode::InterpMode, CallData);
  auto Multi = runEvmBytecode(Label + "_multipass", Bytecode,
                              common::RunMode::MultipassMode, CallData);
#ifdef ZEN_ENABLE_JIT
  EXPECT_TRUE(Multi.JITCompiled) << "multipass did not JIT-compile: " << Label;
#endif
  EXPECT_EQ(Interp.Status, EVMC_SUCCESS)
      << "interpreter did not succeed: " << Label;
  EXPECT_EQ(Multi.Status, Interp.Status) << "status diverged: " << Label;
  EXPECT_EQ(Multi.OutputHex, Interp.OutputHex) << "output diverged: " << Label;
  return Interp.Status == EVMC_SUCCESS && Multi.Status == Interp.Status &&
         Multi.OutputHex == Interp.OutputHex;
}

// Fixture-file variant of the assertion above: load a hex fixture, run both
// engines, and require the multipass JIT output to match the interpreter (the
// full-width ground truth) for the named stem.
void expectFixtureInterpMatchesMultipass(const std::string &Stem) {
  const auto FilePath = (getEvmAsmDirPath() / (Stem + ".evm.hex")).string();

  auto Interp = runEvmBytecodeFile(FilePath, common::RunMode::InterpMode);
  auto Multi = runEvmBytecodeFile(FilePath, common::RunMode::MultipassMode);

#ifdef ZEN_ENABLE_JIT
  EXPECT_TRUE(Multi.JITCompiled) << "Multipass JIT should compile " << Stem;
#endif

  EXPECT_EQ(Interp.Status, EVMC_SUCCESS)
      << "Interpreter did not succeed for " << Stem;
  EXPECT_EQ(Multi.Status, Interp.Status)
      << "Multipass status diverged from interpreter for " << Stem;
  EXPECT_EQ(Multi.OutputHex, Interp.OutputHex)
      << "Multipass output diverged from interpreter for " << Stem;
}

std::string fixtureTestName(const testing::TestParamInfo<std::string> &Info) {
  return Info.param;
}

} // namespace

// ===========================================================================
// Fixture-based differential suite.
//
// Each fixture feeds a dynamic (analyzer-unprovable) operand through an opcode
// whose multipass lowering has a value-range fast path, then asserts the JIT
// output matches the interpreter (the full-width reference) exactly. A
// divergence means a narrowed fold produced a wrong value, not merely a slower
// one. The fixtures hold on the plain lowering as well as the narrowed one, so
// they guard the invariant independently of whether the fast paths are present.
// ===========================================================================

class EVMFixtureDifferentialTest
    : public ::testing::TestWithParam<std::string> {};

TEST_P(EVMFixtureDifferentialTest, InterpMatchesMultipass) {
  expectFixtureInterpMatchesMultipass(GetParam());
}

// Range-narrowed lowering paths: the range-narrowed ISZERO/JUMPI folds,
// U64-tagged OR/XOR, and the signed-compare (SLT/SGT) u64-const fast paths in
// the multipass lowering. Each fixture feeds a dynamic operand so the narrowed
// path is actually taken.
INSTANTIATE_TEST_SUITE_P(
    RangeNarrowing, EVMFixtureDifferentialTest,
    ::testing::Values("iszero_dyn_u64_nonzero", "iszero_dyn_highsparse",
                      "iszero_calldatasize", "jumpi_iszero_fused_taken",
                      "jumpi_iszero_fused_nottaken_highsparse",
                      "jumpi_iszero_iszero_fused", "jumpi_u64_cond_taken",
                      "jumpi_u64_cond_nottaken", "or_dyn_u64_u64",
                      "xor_dyn_u64_u64", "or_dyn_u64_wide", "xor_dyn_u64_wide",
                      "slt_dyn_neg_vs_const", "slt_dyn_highsparse_vs_const",
                      "slt_dyn_msb64_vs_const", "slt_dyn_eq_const",
                      "sgt_dyn_neg_vs_const", "sgt_dyn_highsparse_vs_const",
                      "sgt_dyn_msb64_vs_const", "slt_const_vs_dyn",
                      "sgt_const_vs_dyn"),
    fixtureTestName);

// Const-shift guard pruning and range-aware source-limb pruning in handleShift.
// Each fixture must yield identical output in the interpreter and the multipass
// JIT, and the multipass module must JIT.
INSTANTIATE_TEST_SUITE_P(
    ConstShiftPruning, EVMFixtureDifferentialTest,
    ::testing::Values("shl_const4_dyn", "shl_const96_dyn",
                      "shl_const136_u64val", "shl_const200_u64val",
                      "shl_const256_dyn", "shl_const_highlimb_dyn",
                      "shr_const4_dyn", "shr_const72_dyn", "shr_const8_u64val",
                      "shr_const256_dyn", "sar_const8_neg", "sar_const64_pos",
                      "shl_dyn_amount"),
    fixtureTestName);

// Range-based u64 SUB fast path. Each stem exercises (a - b) mod 2^256 with
// both operands range-proven u64 (except the wide control), including the
// adversarial underflow cases where the upper 192 bits sign-fill to all-ones.
// Interpreter and multipass JIT must agree.
INSTANTIATE_TEST_SUITE_P(
    SubWrapU64, EVMFixtureDifferentialTest,
    ::testing::Values("sub_u64_pair_nounderflow", "sub_u64_pair_underflow",
                      "sub_u64_pair_equal", "sub_u64_pair_wrap_boundary",
                      "sub_u64_pair_zero_rhs_dyn", "sub_wide_u64_control"),
    fixtureTestName);

// ===========================================================================
// Value-range lowering differential matrix harness.
//
// A multipass value-range fast path that emits a single- or double-limb result
// MUST be bit-identical to the full 4-limb path for every input the range tag
// claims to cover. A too-narrow tag silently miscompiles only on operands with
// non-zero high limbs, which ordinary corpora rarely carry. Each test drives an
// opcode with an adversarial operand matrix (value-range boundaries +
// high-sparse {0,x,0,0}/{0,0,0,x}) and asserts the multipass JIT agrees
// bit-for-bit with the interpreter (the full-width reference).
//
// Coverage split (the operands decide which lowering path fires):
//   - BinaryOpsMatchInterpreterOnAdversarialOperands feeds two CALLDATALOAD
//     operands, which are dynamic and U256-range, so they do NOT enter the
//     bothFitU64 / AND-narrow fast paths: this test gates the FULL-width
//     4-limb lowering (the #487-class high-limb-corruption surface). It sweeps
//     20 binary opcodes (arithmetic, comparison, bitwise, BYTE, SIGNEXTEND,
//     and the three shifts). EXP is excluded because its gas cost scales with
//     the exponent byte length, so high-limb operands would trip out-of-gas
//     instead of exposing a value-range divergence.
//   - ShiftAmountsMatchInterpreterOnLimbBoundaries sweeps SHL/SHR/SAR with
//     shift amounts in the limb-crossing region 2..255. The 11-value operand
//     matrix, used as a shift amount, only realizes {0, 1, >=2^64}, so the
//     dynamic-shift cross-limb carry/offset logic is unreachable from the
//     binary-op sweep above; this test covers it.
//   - The AndU64Mask* tests construct a provably-U64 operand (AND with a u64
//     constant) and feed it directly / through a bothFitU64 ADD, so they gate
//     the actual narrowing fast paths.
// ===========================================================================

namespace {

// Build a big-endian 32-byte word holding `Val` in 64-bit limb `Limb`
// (limb 0 = least significant). OR several together for multi-limb operands.
std::vector<uint8_t> matrixLimb(int Limb, uint64_t Val) {
  std::vector<uint8_t> W(32, 0);
  const int Base = 24 - Limb * 8; // limb 0 -> bytes [24..31]
  for (int I = 0; I < 8; ++I) {
    W[Base + 7 - I] = static_cast<uint8_t>((Val >> (8 * I)) & 0xff);
  }
  return W;
}

std::vector<uint8_t> matrixOr(std::vector<uint8_t> A,
                              const std::vector<uint8_t> &B) {
  for (size_t I = 0; I < A.size(); ++I) {
    A[I] |= B[I];
  }
  return A;
}

// Adversarial operand matrix: each entry is a 32-byte big-endian u256.
std::vector<std::vector<uint8_t>> matrixOperands() {
  const uint64_t Max = 0xFFFFFFFFFFFFFFFFull;
  std::vector<std::vector<uint8_t>> Ops;
  Ops.push_back(matrixLimb(0, 0));   // 0
  Ops.push_back(matrixLimb(0, 1));   // 1
  Ops.push_back(matrixLimb(0, Max)); // 2^64 - 1 (U64 boundary)
  Ops.push_back(matrixLimb(1, 1));   // 2^64
  Ops.push_back(matrixOr(matrixLimb(0, Max), matrixLimb(1, Max))); // U128-1
  Ops.push_back(matrixLimb(2, 1)); // 2^128 (U128 boundary)
  Ops.push_back(matrixLimb(3, 1)); // 2^192 (high-sparse {0,0,0,x})
  Ops.push_back(matrixOr(matrixLimb(3, 1), matrixLimb(0, 5))); // 2^192+5
  Ops.push_back(matrixLimb(1, 5));                     // high-sparse {0,x,0,0}
  Ops.push_back(matrixLimb(3, 0x8000000000000000ull)); // 2^255 (sign bit)
  Ops.push_back(
      matrixOr(matrixOr(matrixLimb(0, Max), matrixLimb(1, Max)),
               matrixOr(matrixLimb(2, Max), matrixLimb(3, Max)))); // 2^256-1
  return Ops;
}

std::vector<uint8_t> matrixCalldata(const std::vector<uint8_t> &A,
                                    const std::vector<uint8_t> &B) {
  std::vector<uint8_t> CD;
  CD.insert(CD.end(), A.begin(), A.end());
  CD.insert(CD.end(), B.begin(), B.end());
  return CD;
}

// "a = calldata[0:32]; b = calldata[32:64]; OP". After the two pushes, b is on
// top of the stack, so EVM evaluates `b OP a` (e.g. SUB = b - a; for shifts the
// shift amount is b). The exact convention is irrelevant to a differential test
// — both engines run identical bytecode — and the nested A*B sweep feeds every
// matrix value into both slots regardless.
std::vector<uint8_t> matrixBinOp(uint8_t Op) {
  return {0x60, 0x00, 0x35,              // PUSH1 0    CALLDATALOAD -> a
          0x60, 0x20, 0x35,              // PUSH1 0x20 CALLDATALOAD -> b
          Op,                            // b OP a
          0x60, 0x00, 0x52,              // PUSH1 0 MSTORE
          0x60, 0x20, 0x60, 0x00, 0xf3}; // RETURN(0, 32)
}

} // namespace

// Differential over every binary opcode whose lowering touches limbs. Operands
// are dynamic CALLDATALOAD (U256-range), so this gates the FULL-width 4-limb
// path, not the narrow fast paths (those are gated by the AndU64Mask* tests).
// On the first divergent (op, operand) pair the whole test returns, so a single
// regression reports one op and skips the rest — intentional output-flood
// control, not full per-op isolation.
//
// EXP (0x0a) is intentionally excluded: its gas cost scales with the byte
// length of the exponent, so the high-limb operands in this matrix (2^192,
// 2^255, 2^256-1) would charge enormous dynamic gas and obscure a pure
// value-range divergence behind out-of-gas noise. Shift amounts, which need
// the limb-crossing region 2..255, are swept separately by
// ShiftAmountsMatchInterpreterOnLimbBoundaries below.
TEST(EVMRangeDifferential, BinaryOpsMatchInterpreterOnAdversarialOperands) {
  struct OpCase {
    uint8_t Op;
    const char *Name;
  };
  const OpCase Cases[] = {
      {0x01, "ADD"},  {0x02, "MUL"}, {0x03, "SUB"},  {0x04, "DIV"},
      {0x05, "SDIV"}, {0x06, "MOD"}, {0x07, "SMOD"}, {0x0b, "SIGNEXTEND"},
      {0x10, "LT"},   {0x11, "GT"},  {0x12, "SLT"},  {0x13, "SGT"},
      {0x14, "EQ"},   {0x16, "AND"}, {0x17, "OR"},   {0x18, "XOR"},
      {0x1a, "BYTE"}, {0x1b, "SHL"}, {0x1c, "SHR"},  {0x1d, "SAR"},
  };
  const auto Operands = matrixOperands();
  for (const auto &C : Cases) {
    const auto Bytecode = matrixBinOp(C.Op);
    for (const auto &A : Operands) {
      for (const auto &B : Operands) {
        if (!expectInterpMatchesMultipass(C.Name, Bytecode,
                                          matrixCalldata(A, B))) {
          return; // one divergence is enough; avoid output flood
        }
      }
    }
  }
}

// Sweep SHL/SHR/SAR with shift amounts that land inside the limb-crossing
// region 2..255. The 11-value operand matrix, when fed as a shift amount, only
// realizes {0, 1, >=2^64}; it never produces an amount in 2..255, so the
// dynamic-shift lowering's cross-limb carry/offset logic (which moves bits
// across 64-bit limb boundaries at amounts 64/128/192 and within a limb at the
// rest) is never exercised by BinaryOpsMatchInterpreterOnAdversarialOperands.
//
// matrixBinOp puts the shift amount in slot b (calldata[32:64], the stack top
// SHL/SHR/SAR pops first) and the shifted value in slot a (calldata[0:32]).
// Both are CALLDATALOAD, so the full-width dynamic shift path fires. The
// shifted value sweeps the full adversarial matrix; the amount sweeps the
// boundary set below, covering both the on-limb-boundary amounts (64, 128, 192)
// and the in-limb amounts (2, 7, 31, 63, 65, 127, ...).
TEST(EVMRangeDifferential, ShiftAmountsMatchInterpreterOnLimbBoundaries) {
  struct OpCase {
    uint8_t Op;
    const char *Name;
  };
  const OpCase Cases[] = {
      {0x1b, "SHL"},
      {0x1c, "SHR"},
      {0x1d, "SAR"},
  };
  const uint64_t Amounts[] = {2,   7,   8,   31,  63,  64,  65, 127,
                              128, 129, 136, 191, 192, 200, 255};
  const auto Values = matrixOperands();
  for (const auto &C : Cases) {
    const auto Bytecode = matrixBinOp(C.Op);
    for (const auto &Value : Values) {
      for (uint64_t Amount : Amounts) {
        const auto AmountWord = matrixLimb(0, Amount);
        if (!expectInterpMatchesMultipass(C.Name, Bytecode,
                                          matrixCalldata(Value, AmountWord))) {
          return; // one divergence is enough; avoid output flood
        }
      }
    }
  }
}

// Directly exercise the AND-with-u64-constant fast path (CONST_U64): it tags
// the result U64 and MUST zero limbs[1..3]. Returning the result directly (not
// through a later narrow op that would discard the high limbs) is what makes a
// too-narrow / too-wide limb bug observable. Feeding high-sparse calldata, the
// masked result must equal the interpreter's.
//   a = calldata[0:32]; MSTORE(0, a AND 0xFFFFFFFFFFFFFFFF); RETURN 32.
TEST(EVMRangeDifferential, AndU64MaskMatchesInterpreter) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x00, 0x35,                                     // CALLDATALOAD a
      0x67, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // PUSH8 u64 mask
      0x16,                                                 // AND -> m (U64)
      0x60, 0x00, 0x52, 0x60, 0x20, 0x60, 0x00, 0xf3};      // RETURN(0,32)
  const auto Operands = matrixOperands();
  for (const auto &A : Operands) {
    if (!expectInterpMatchesMultipass("and_u64_mask", Bytecode,
                                      matrixCalldata(A, A))) {
      return;
    }
  }
}

// Exercise a narrow-result CONSUMER: AND with a u64 constant produces a U64
// value that feeds a self-ADD on the bothFitU64 narrow path. The summed result
// is returned; for any high-sparse input the narrowed two-limb sum must match
// the interpreter's full-width sum.
//   a = calldata[0:32]; m = a AND 0xFFFFFFFFFFFFFFFF; MSTORE(0, m + m); RETURN.
TEST(EVMRangeDifferential, AndU64MaskThenNarrowAddMatchesInterpreter) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x00, 0x35,                                     // CALLDATALOAD a
      0x67, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // PUSH8 u64 mask
      0x16,                                                 // AND -> m (U64)
      0x80,                                                 // DUP1
      0x01,                                            // ADD m + m (narrow)
      0x60, 0x00, 0x52, 0x60, 0x20, 0x60, 0x00, 0xf3}; // RETURN(0,32)
  const auto Operands = matrixOperands();
  for (const auto &A : Operands) {
    if (!expectInterpMatchesMultipass("and_u64_mask_then_add", Bytecode,
                                      matrixCalldata(A, A))) {
      return;
    }
  }
}

TEST(EVMRangeDifferential, DeadUnderResolvedFallthroughMatchesInterpreter) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x01, // PC0 PUSH1 0x01 (take JUMPI)
      0x60, 0x0b, // PC2 PUSH1 0x0b
      0x57,       // PC4 JUMPI
      0x01,       // PC5 ADD (dead under-resolved fallthrough)
      0x60, 0x00, // PC6 PUSH1 0x00
      0x60, 0x0b, // PC8 PUSH1 0x0b
      0x56,       // PC10 JUMP
      0x5b,       // PC11 JUMPDEST
      0x00,       // PC12 STOP
  };

  EXPECT_TRUE(expectInterpMatchesMultipass("dead_under_resolved_fallthrough",
                                           Bytecode, {}));
}

TEST(EVMDeepEntryFallback, DeadCfgDoesNotBlockJITAndMatchesInterpreter) {
  const std::vector<uint8_t> Bytecode = {
      0x00, // PC0 STOP
      0x5b, // PC1 JUMPDEST (dead dynamic source)
      0x5f, // PC2 PUSH0
      0x35, // PC3 CALLDATALOAD
      0x56, // PC4 JUMP (unreachable dynamic jump)
      0x5b, // PC5 JUMPDEST (dead predecessor)
      0x50, // PC6 POP
      0x5b, // PC7 JUMPDEST (dead, requires two entry slots)
      0x01, // PC8 ADD
      0x00, // PC9 STOP
  };
  COMPILER::EVMAnalyzer Analyzer(EVMC_CANCUN);
  ASSERT_TRUE(Analyzer.analyze(Bytecode.data(), Bytecode.size()));
  ASSERT_FALSE(Analyzer.hasUnresolvedNonLiftedDeepEntryRisk());

  const auto Interp = runEvmBytecode("dead_deep_entry_interp", Bytecode,
                                     common::RunMode::InterpMode);
  const auto Multi = runEvmBytecode("dead_deep_entry_multipass", Bytecode,
                                    common::RunMode::MultipassMode);
#ifdef ZEN_ENABLE_JIT
  EXPECT_TRUE(Multi.JITCompiled) << "dead CFG blocked JIT compilation";
#endif
  EXPECT_EQ(Multi.Status, Interp.Status);
  EXPECT_EQ(Multi.OutputHex, Interp.OutputHex);
}

// Regression: a lifted block with a hidden live-in prefix (HiddenLiveInPrefix
// Depth > 0) that takes a materializing exit must spill its logical stack from
// the stack bottom, not from byte offset Hidden*32. The lifted logical stack
// already spans the full absolute entry depth, so an offset spill writes the
// stack Hidden slots too high and inflates the recorded StackSize by Hidden.
// The inflated depth suppresses a runtime underflow check in a later block,
// diverging from the interpreter.
//
// Shape (confirmed by analysis): block S at PC12 is lifted with Hidden=1 and
// exits via a constant JUMP to the non-lifted block T at PC16, which pops two
// slots while only the single prefix slot is live. The interpreter underflows
// at the second POP; before the fix the JIT's inflated StackSize lets the
// underflow check pass and the block runs to STOP, so status diverges.
TEST(EVMLiftedStackDepth, HiddenPrefixMaterializingExitMatchesInterp) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0xAA, // PC0  PUSH1 0xAA  (live-in prefix)
      0x60, 0x01, // PC2  PUSH1 0x01  (cond = 1, jump taken)
      0x60, 0x0C, // PC4  PUSH1 0x0C  (S entry = 12)
      0x57,       // PC6  JUMPI
      0x60, 0xBB, // PC7  PUSH1 0xBB  (F: second, depth-2 predecessor of T)
      0x60, 0x10, // PC9  PUSH1 0x10  (T = 16)
      0x56,       // PC11 JUMP        (F -> T at depth 2)
      0x5b,       // PC12 JUMPDEST    (S: lifted, Hidden = 1)
      0x60, 0x10, // PC13 PUSH1 0x10  (T = 16)
      0x56,       // PC15 JUMP        (S -> T at depth 1, materializing)
      0x5b,       // PC16 JUMPDEST    (T: non-lifted, pops 2)
      0x50,       // PC17 POP
      0x50,       // PC18 POP         (underflow: only 1 slot present)
      0x00,       // PC19 STOP
  };
  const std::vector<uint8_t> CallData;
  auto Interp = runEvmBytecode("hidden_prefix_interp", Bytecode,
                               common::RunMode::InterpMode, CallData);
  auto Multi = runEvmBytecode("hidden_prefix_multipass", Bytecode,
                              common::RunMode::MultipassMode, CallData);
#ifdef ZEN_ENABLE_JIT
  EXPECT_TRUE(Multi.JITCompiled) << "multipass did not JIT-compile";
#endif
  // The interpreter is the reference: the second POP underflows, so the run
  // must not report success. If this ever succeeds the test has stopped
  // exercising the underflow boundary and must be revisited.
  EXPECT_NE(Interp.Status, EVMC_SUCCESS)
      << "interpreter unexpectedly succeeded; test no longer exercises the "
         "underflow boundary";
  EXPECT_EQ(Multi.Status, Interp.Status) << "status diverged";
  EXPECT_EQ(Multi.OutputHex, Interp.OutputHex) << "output diverged";
}

// Deep-entry risk shape: block PC33 is a shared "increment-and-return"
// subroutine that returns via a stack-passed return PC (SWAP1 then dynamic
// JUMP), so per-block abstract-stack analysis cannot resolve its return
// continuations. Their ResolvedEntryStackDepth stays < 0 and the invalidation
// cascades along the static CFG.
//
// One continuation (PC18) falls through to PC22, whose ADD reads two stack
// slots -- a static successor of an unresolved-depth block that reads 2 slots.
// The conservative admission predicate fires on this reachable pattern. A
// forced-JIT control currently matches the interpreter for this bytecode, but
// this local shape does not prove that every deeper caller frame is safe; the
// mainnet tx46 storage divergence is the external evidence requiring the guard.
//
// Execution: two calls into PC33 turn the initial 0x05 into 0x08 (0x05+1 in the
// outer frame, 0x07+1 in the inner frame), which is MSTOREd and RETURNed as a
// single 32-byte word.
TEST(EVMDeepEntryFallback,
     ReachableInternalCallUsesConservativeAdmissionFallback) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x08, // PC0  PUSH1 0x08
      0x60, 0x05, // PC2  PUSH1 0x05  (value threaded through the subroutine)
      0x60, 0x21, // PC4  PUSH1 0x21  (return PC = 33)
      0x56,       // PC6  JUMP        (call subroutine at PC33)
      0xfe,       // PC7  INVALID
      0x5b,       // PC8  JUMPDEST    (outer return continuation)
      0x60, 0x00, // PC9  PUSH1 0x00
      0x60, 0x12, // PC11 PUSH1 0x12  (return PC = 18)
      0x60, 0x07, // PC13 PUSH1 0x07
      0x60, 0x21, // PC15 PUSH1 0x21  (call subroutine at PC33)
      0x56,       // PC17 JUMP
      0x5b,       // PC18 JUMPDEST    (inner return continuation)
      0x60, 0x00, // PC19 PUSH1 0x00
      0x50,       // PC21 POP
      0x5b,       // PC22 JUMPDEST    (static successor: ADD reads 2 slots)
      0x01,       // PC23 ADD
      0x60, 0x00, // PC24 PUSH1 0x00
      0x52,       // PC26 MSTORE
      0x60, 0x20, // PC27 PUSH1 0x20
      0x60, 0x00, // PC29 PUSH1 0x00
      0xf3,       // PC31 RETURN
      0xfe,       // PC32 INVALID
      0x5b,       // PC33 JUMPDEST    (increment-and-return subroutine)
      0x60, 0x01, // PC34 PUSH1 0x01
      0x01,       // PC36 ADD
      0x90,       // PC37 SWAP1
      0x56,       // PC38 JUMP        (dynamic return via stack PC)
  };
  COMPILER::EVMAnalyzer Analyzer(EVMC_CANCUN);
  ASSERT_TRUE(Analyzer.analyze(Bytecode.data(), Bytecode.size()));
  EXPECT_TRUE(Analyzer.hasUnresolvedNonLiftedDeepEntryRisk());

  const std::vector<uint8_t> CallData;
  auto Interp = runEvmBytecode("deep_entry_interp", Bytecode,
                               common::RunMode::InterpMode, CallData);
  auto Multi = runEvmBytecode("deep_entry_multipass", Bytecode,
                              common::RunMode::MultipassMode, CallData);
#ifdef ZEN_ENABLE_JIT
  EXPECT_FALSE(Multi.JITCompiled)
      << "conservative deep-entry admission guard did not fall back";
#endif
  EXPECT_EQ(Interp.Status, EVMC_SUCCESS) << "interpreter did not succeed";
  EXPECT_EQ(Multi.Status, Interp.Status) << "status diverged";
  EXPECT_EQ(Multi.OutputHex, Interp.OutputHex) << "output diverged";
  // Ground truth: 32-byte word holding 0x08, guarding against both engines
  // agreeing on a wrong value.
  EXPECT_EQ(Interp.OutputHex,
            "0000000000000000000000000000000000000000000000000000000000000008");
}

#endif
