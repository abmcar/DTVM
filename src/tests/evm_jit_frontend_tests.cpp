// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "action/evm_bytecode_visitor.h"
#include "compiler/evm_frontend/evm_analyzer.h"
#include "compiler/evm_frontend/evm_mir_compiler.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

namespace {

using COMPILER::EVMAnalyzer;
using COMPILER::EVMMirBuilder;
using COMPILER::EVMValueRange;
using zen::common::BinaryOperator;
using zen::common::CompareOperator;

EVMAnalyzer analyzeBytecode(const std::vector<uint8_t> &Bytecode) {
  EVMAnalyzer Analyzer(EVMC_CANCUN);
  const uint8_t *Data = Bytecode.empty() ? nullptr : Bytecode.data();
  Analyzer.analyze(Data, Bytecode.size());
  return Analyzer;
}

EVMAnalyzer
analyzeSuitabilityOnlyBytecode(const std::vector<uint8_t> &Bytecode) {
  EVMAnalyzer Analyzer(EVMC_CANCUN);
  const uint8_t *Data = Bytecode.empty() ? nullptr : Bytecode.data();
  Analyzer.analyzeSuitabilityOnly(Data, Bytecode.size());
  return Analyzer;
}

const EVMAnalyzer::BlockInfo *findBlock(const EVMAnalyzer &Analyzer,
                                        uint64_t EntryPC) {
  const auto &Blocks = Analyzer.getBlockInfos();
  auto It = Blocks.find(EntryPC);
  if (It == Blocks.end()) {
    return nullptr;
  }
  return &It->second;
}

class MirBuilderConstFoldHarness {
public:
  MirBuilderConstFoldHarness() : Func(Ctx, 0), Builder(Ctx, Func) {
    Ctx.setRevision(EVMC_OSAKA);
    Ctx.setBytecode(nullptr, 0);

    std::array<COMPILER::MType *, 1> ParamTypes = {
        COMPILER::MPointerType::create(Ctx, Ctx.VoidType)};
    Func.setFunctionType(COMPILER::MFunctionType::create(
        Ctx, Ctx.VoidType, llvm::ArrayRef<COMPILER::MType *>(ParamTypes)));
    Builder.initEVM(&Ctx);
  }

  COMPILER::EVMFrontendContext Ctx;
  COMPILER::MFunction Func;
  EVMMirBuilder Builder;
};

void expectPCList(const std::vector<uint64_t> &Actual,
                  std::initializer_list<uint64_t> Expected) {
  ASSERT_EQ(Actual.size(), Expected.size());
  size_t Index = 0;
  for (uint64_t ExpectedPC : Expected) {
    EXPECT_EQ(Actual[Index], ExpectedPC) << "mismatch at index " << Index;
    ++Index;
  }
}

struct MockOperand {
  using U256Value = std::array<uint64_t, 4>;

  MockOperand() = default;
  explicit MockOperand(uint64_t Low) : Value{Low, 0, 0, 0}, Constant(true) {}
  explicit MockOperand(std::shared_ptr<U256Value> Slot)
      : Slot(std::move(Slot)) {}

  bool isConstant() const { return Constant; }

  const U256Value &getConstValue() const {
    ZEN_ASSERT(Constant && "mock operand must be constant");
    return Value;
  }

  U256Value resolvedValue() const {
    if (Slot) {
      return *Slot;
    }
    return Value;
  }

  void assign(const MockOperand &Other) {
    ZEN_ASSERT(Slot && "mock operand slot missing");
    *Slot = Other.resolvedValue();
  }

  void setRange(COMPILER::EVMValueRange) {}

  bool isEmpty() const { return !Constant && !Slot; }

private:
  U256Value Value = {0, 0, 0, 0};
  bool Constant = false;
  std::shared_ptr<U256Value> Slot;
};

struct MockStackAccessStats {
  uint32_t StackPopCount = 0;
  uint32_t StackPushCount = 0;
  uint32_t StackGetCount = 0;
  uint32_t StackSetCount = 0;
};

struct MockMeterOpcodeRangeRecord {
  uint64_t StartPC = 0;
  uint64_t EndPCExclusive = 0;
};

struct MockMStoreRecord {
  MockOperand::U256Value Addr = {0, 0, 0, 0};
  MockOperand::U256Value Value = {0, 0, 0, 0};
};

struct MockKeccakCallDataSlotRecord {
  MockOperand::U256Value Offset = {0, 0, 0, 0};
  MockOperand::U256Value CallDataOffset = {0, 0, 0, 0};
  MockOperand::U256Value Slot = {0, 0, 0, 0};
};

struct MockKeccakCallerSlotRecord {
  MockOperand::U256Value Offset = {0, 0, 0, 0};
  MockOperand::U256Value Slot = {0, 0, 0, 0};
};

class MockEVMBuilder {
public:
  using CompilerContext = COMPILER::EVMFrontendContext;
  using Operand = MockOperand;

#define MOCK_OPERAND_STUB(Name)                                                \
  template <typename... Args> Operand Name(Args...) { return Operand(0); }

#define MOCK_VOID_STUB(Name)                                                   \
  template <typename... Args> void Name(Args...) {}

  void initEVM(CompilerContext *) {
    CurrentOpcode = 0xff;
    Trapped = false;
    Undefined = false;
  }

  void finalizeEVMBase() {}

  bool isOpcodeDefined(evmc_opcode Opcode) const {
    const auto *InstructionNames =
        evmc_get_instruction_names_table(EVMC_CANCUN);
    return InstructionNames && InstructionNames[Opcode] != nullptr;
  }

  void meterOpcode(evmc_opcode Opcode, uint64_t) {
    CurrentOpcode = static_cast<uint8_t>(Opcode);
    MeteredOpcodeCounts[static_cast<uint8_t>(Opcode)]++;
  }

  void meterOpcodeRange(uint64_t StartPC, uint64_t EndPCExclusive) {
    MeteredRanges.push_back({StartPC, EndPCExclusive});
  }

  void enableRuntimeStackChecks() { EnableRuntimeStackChecks = true; }

  void createStackCheckBlock(int32_t MinSize, int32_t MaxSize) {
    if (!EnableRuntimeStackChecks) {
      return;
    }
    if (RuntimeStack.size() < static_cast<size_t>(std::max(MinSize, 0))) {
      Trapped = true;
      return;
    }
    if (RuntimeStack.size() > static_cast<size_t>(std::max(MaxSize, 0))) {
      Trapped = true;
    }
  }

  Operand handlePush(const zen::common::Bytes &Data) {
    uint64_t Low = 0;
    for (zen::common::Byte Byte : Data) {
      Low = (Low << 8) | static_cast<uint64_t>(std::to_integer<uint8_t>(Byte));
    }
    LastPushValue = {Low, 0, 0, 0};
    HasLastPushValue = true;
    return Operand(Low);
  }

  void stackPush(Operand PushValue) {
    Stats[CurrentOpcode].StackPushCount++;
    RuntimeStack.push_back(PushValue);
  }

  Operand stackPop() {
    Stats[CurrentOpcode].StackPopCount++;
    ZEN_ASSERT(!RuntimeStack.empty() && "mock runtime stack underflow");
    Operand Top = RuntimeStack.back();
    RuntimeStack.pop_back();
    return Top;
  }

  void stackSet(int32_t IndexFromTop, Operand SetValue) {
    Stats[CurrentOpcode].StackSetCount++;
    size_t Index = RuntimeStack.size() - static_cast<size_t>(IndexFromTop) - 1;
    RuntimeStack[Index] = SetValue;
  }

  Operand stackGet(int32_t IndexFromTop) {
    Stats[CurrentOpcode].StackGetCount++;
    size_t Index = RuntimeStack.size() - static_cast<size_t>(IndexFromTop) - 1;
    return RuntimeStack[Index];
  }

  void setTrackedStackDepth(uint32_t Depth) {
    if (RuntimeStack.size() > Depth) {
      RuntimeStack.resize(Depth);
    }
  }

  Operand createStackEntryOperand(
      COMPILER::EVMValueRange = COMPILER::EVMValueRange::U256) {
    return Operand(std::make_shared<MockOperand::U256Value>(
        MockOperand::U256Value{0, 0, 0, 0}));
  }

  void assignStackEntryOperand(const Operand &Dest, const Operand &Value) {
    Operand Copy = Dest;
    Copy.assign(Value);
  }

  void spillTrackedStack(const std::vector<Operand> &TrackedStack) {
    RuntimeStack = TrackedStack;
  }

  void setCurrentDebugBlockPC(uint64_t) {}

  template <zen::common::BinaryOperator Opr>
  Operand handleBinaryArithmetic(Operand LHS, Operand RHS) {
    if constexpr (Opr == zen::common::BinaryOperator::BO_ADD) {
      const auto LHSValue = LHS.resolvedValue();
      const auto RHSValue = RHS.resolvedValue();
      return Operand(LHSValue[0] + RHSValue[0]);
    }
    return Operand(0);
  }

  template <zen::common::CompareOperator Opr>
  Operand handleCompareOp(Operand, Operand) {
    return Operand(0);
  }

  template <zen::common::BinaryOperator Opr>
  Operand handleBitwiseOp(Operand, Operand) {
    return Operand(0);
  }

  template <zen::common::BinaryOperator Opr>
  Operand handleShift(Operand, Operand) {
    return Operand(0);
  }

  template <size_t NumTopics, typename... Args>
  void handleLogWithTopics(Args...) {}

  Operand handleCall(Operand, Operand, Operand, Operand, Operand, Operand,
                     Operand) {
    return Operand(0);
  }

  Operand handleCallCode(Operand, Operand, Operand, Operand, Operand, Operand,
                         Operand) {
    return Operand(0);
  }

  Operand handleDelegateCall(Operand, Operand, Operand, Operand, Operand,
                             Operand) {
    return Operand(0);
  }

  Operand handleStaticCall(Operand, Operand, Operand, Operand, Operand,
                           Operand) {
    return Operand(0);
  }

  MOCK_OPERAND_STUB(handleAddMod);
  MOCK_OPERAND_STUB(handleAddress);
  MOCK_OPERAND_STUB(handleBalance);
  MOCK_OPERAND_STUB(handleBaseFee);
  MOCK_OPERAND_STUB(handleBlobBaseFee);
  MOCK_OPERAND_STUB(handleBlobHash);
  MOCK_OPERAND_STUB(handleBlockHash);
  MOCK_OPERAND_STUB(handleByte);
  MOCK_OPERAND_STUB(handleCallDataLoad);
  MOCK_OPERAND_STUB(handleCallDataSize);
  MOCK_OPERAND_STUB(handleCallValue);
  MOCK_OPERAND_STUB(handleCaller);
  MOCK_OPERAND_STUB(handleChainId);
  MOCK_OPERAND_STUB(handleClz);
  MOCK_OPERAND_STUB(handleCodeSize);
  MOCK_OPERAND_STUB(handleCoinBase);
  MOCK_OPERAND_STUB(handleCreate);
  MOCK_OPERAND_STUB(handleCreate2);
  MOCK_OPERAND_STUB(handleDiv);
  MOCK_OPERAND_STUB(handleExp);
  MOCK_OPERAND_STUB(handleExtCodeHash);
  MOCK_OPERAND_STUB(handleExtCodeSize);
  MOCK_OPERAND_STUB(handleGas);
  MOCK_OPERAND_STUB(handleGasLimit);
  MOCK_OPERAND_STUB(handleGasPrice);
  MOCK_OPERAND_STUB(handleMLoad);
  MOCK_OPERAND_STUB(handleMSize);
  MOCK_OPERAND_STUB(handleMod);
  MOCK_OPERAND_STUB(handleMul);
  MOCK_OPERAND_STUB(handleMulMod);
  MOCK_OPERAND_STUB(handleNot);
  MOCK_OPERAND_STUB(handleNumber);
  MOCK_OPERAND_STUB(handleOrigin);
  MOCK_OPERAND_STUB(handlePC);
  MOCK_OPERAND_STUB(handlePrevRandao);
  MOCK_OPERAND_STUB(handleReturnDataSize);
  MOCK_OPERAND_STUB(handleSDiv);
  MOCK_OPERAND_STUB(handleSLoad);
  MOCK_OPERAND_STUB(handleSMod);
  MOCK_OPERAND_STUB(handleSelfBalance);
  MOCK_OPERAND_STUB(handleSignextend);
  MOCK_OPERAND_STUB(handleTimestamp);
  MOCK_OPERAND_STUB(handleTLoad);

  MOCK_VOID_STUB(handleCallDataCopy);
  MOCK_VOID_STUB(handleCodeCopy);
  MOCK_VOID_STUB(handleExtCodeCopy);
  MOCK_VOID_STUB(handleMCopy);
  MOCK_VOID_STUB(handleMStore8);

  void handleMStore(Operand Addr, Operand Value) {
    LastMStore = {Addr.resolvedValue(), Value.resolvedValue()};
    MStoreCount++;
  }

  Operand handleKeccak256(Operand, Operand) {
    KeccakCount++;
    return Operand(0);
  }

  Operand handleKeccak256TwoWord(Operand Offset, Operand Word0, Operand Word1) {
    KeccakTwoWordLastOffset = Offset.resolvedValue();
    KeccakTwoWordLastWord0 = Word0.resolvedValue();
    KeccakTwoWordLastWord1 = Word1.resolvedValue();
    KeccakTwoWordCount++;
    return Operand(0);
  }

  Operand handleKeccak256CallDataConstSlot(Operand Offset,
                                           Operand CallDataOffset,
                                           Operand SlotWord) {
    LastKeccakCallDataSlot = {Offset.resolvedValue(),
                              CallDataOffset.resolvedValue(),
                              SlotWord.resolvedValue()};
    KeccakCallDataSlotCount++;
    return Operand(0);
  }

  Operand handleKeccak256CallerConstSlot(Operand Offset, Operand SlotWord) {
    LastKeccakCallerSlot = {Offset.resolvedValue(), SlotWord.resolvedValue()};
    KeccakCallerSlotCount++;
    return Operand(0);
  }

  MOCK_VOID_STUB(handleReturn);
  MOCK_VOID_STUB(handleReturnDataCopy);
  MOCK_VOID_STUB(handleRevert);
  MOCK_VOID_STUB(handleSStore);
  MOCK_VOID_STUB(handleSelfDestruct);
  MOCK_VOID_STUB(handleTStore);

  void beginMemoryCompileBlock(uint64_t) {}
  void setMemoryCompileBlockConstPrecheckPlan(uint64_t, uint64_t) {}
  void setMemoryCompileBlockLinearPrecheckPlan(uint64_t, uint64_t, bool) {}
  void prepareLinearBlockMemoryPrecheck(Operand) {}
  void noteMemoryOpcodeInBlock(evmc_opcode, uint64_t) {}
  void noteHelperOpcodeInBlock(evmc_opcode, uint64_t) {}
  void endMemoryCompileBlock() {}

  void handleJump(Operand) {}
  void handleJumpI(Operand, Operand) {}
  void handleJumpDest(const uint64_t &) {}
  void handleStop() {}
  void handleUndefined() { Undefined = true; }
  void handleInvalid() { Undefined = true; }
  void handleTrap(zen::common::ErrorCode) { Trapped = true; }
  void fallbackToInterpreter(uint64_t) {}
  void releaseOperand(Operand) {}

  const MockStackAccessStats &accessStats(evmc_opcode Opcode) const {
    return Stats[static_cast<uint8_t>(Opcode)];
  }

  uint32_t meteredOpcodeCount(evmc_opcode Opcode) const {
    return MeteredOpcodeCounts[static_cast<uint8_t>(Opcode)];
  }

  const std::vector<MockMeterOpcodeRangeRecord> &meteredRanges() const {
    return MeteredRanges;
  }

  uint32_t mstoreCount() const { return MStoreCount; }

  uint32_t keccakCount() const { return KeccakCount; }

  uint32_t keccakTwoWordCount() const { return KeccakTwoWordCount; }

  uint32_t keccakCallDataSlotCount() const { return KeccakCallDataSlotCount; }

  uint32_t keccakCallerSlotCount() const { return KeccakCallerSlotCount; }

  const MockMStoreRecord &lastMStore() const {
    ZEN_ASSERT(MStoreCount != 0 && "mock mstore record is missing");
    return LastMStore;
  }

  const MockKeccakCallDataSlotRecord &lastKeccakCallDataSlot() const {
    ZEN_ASSERT(KeccakCallDataSlotCount != 0 &&
               "mock calldata+slot keccak record is missing");
    return LastKeccakCallDataSlot;
  }

  const MockKeccakCallerSlotRecord &lastKeccakCallerSlot() const {
    ZEN_ASSERT(KeccakCallerSlotCount != 0 &&
               "mock caller+slot keccak record is missing");
    return LastKeccakCallerSlot;
  }

  bool hasLastPushValue() const { return HasLastPushValue; }

  MockOperand::U256Value lastPushValue() const {
    ZEN_ASSERT(HasLastPushValue && "mock push value is missing");
    return LastPushValue;
  }

  size_t runtimeStackDepth() const { return RuntimeStack.size(); }

  MockOperand::U256Value topStackValue() const {
    ZEN_ASSERT(!RuntimeStack.empty() && "mock runtime stack is empty");
    return RuntimeStack.back().resolvedValue();
  }

  bool Trapped = false;
  bool Undefined = false;

private:
  bool EnableRuntimeStackChecks = false;
  uint8_t CurrentOpcode = 0xff;
  std::array<MockStackAccessStats, 256> Stats = {};
  std::array<uint32_t, 256> MeteredOpcodeCounts = {};
  std::vector<MockMeterOpcodeRangeRecord> MeteredRanges;
  MockMStoreRecord LastMStore = {};
  uint32_t MStoreCount = 0;
  MockOperand::U256Value KeccakTwoWordLastOffset = {0, 0, 0, 0};
  MockOperand::U256Value KeccakTwoWordLastWord0 = {0, 0, 0, 0};
  MockOperand::U256Value KeccakTwoWordLastWord1 = {0, 0, 0, 0};
  uint32_t KeccakCount = 0;
  uint32_t KeccakTwoWordCount = 0;
  MockKeccakCallDataSlotRecord LastKeccakCallDataSlot = {};
  uint32_t KeccakCallDataSlotCount = 0;
  MockKeccakCallerSlotRecord LastKeccakCallerSlot = {};
  uint32_t KeccakCallerSlotCount = 0;
  std::vector<Operand> RuntimeStack;
  MockOperand::U256Value LastPushValue = {0, 0, 0, 0};
  bool HasLastPushValue = false;

#undef MOCK_OPERAND_STUB
#undef MOCK_VOID_STUB
};

class DynamicPushMockEVMBuilder : public MockEVMBuilder {
public:
  Operand handlePush(const zen::common::Bytes &) { return Operand(); }
};

TEST(EVMMirBuilderConstFoldTest, ExpFoldsConstantOperands) {
  MirBuilderConstFoldHarness Harness;
  using Operand = EVMMirBuilder::Operand;
  using U256Value = EVMMirBuilder::U256Value;

  auto fold = [&](U256Value Base, U256Value Exp) {
    return Harness.Builder.handleExp(Operand(Base), Operand(Exp));
  };

  // 10 ** 2 == 100
  auto R = fold({10, 0, 0, 0}, {2, 0, 0, 0});
  ASSERT_TRUE(R.isConstant());
  EXPECT_EQ(R.getConstValue(), (U256Value{100, 0, 0, 0}));

  // 2 ** 64 carries into limb 1
  EXPECT_EQ(fold({2, 0, 0, 0}, {64, 0, 0, 0}).getConstValue(),
            (U256Value{0, 1, 0, 0}));

  // 2 ** 256 wraps to 0 (mod 2^256)
  EXPECT_EQ(fold({2, 0, 0, 0}, {256, 0, 0, 0}).getConstValue(),
            (U256Value{0, 0, 0, 0}));

  // 0 ** 0 == 1 (EVM convention)
  EXPECT_EQ(fold({0, 0, 0, 0}, {0, 0, 0, 0}).getConstValue(),
            (U256Value{1, 0, 0, 0}));
}

// Pins the gas charged on the EXP const-fold path (the soundness-critical half:
// the exponent magnitude is observable in gas), independent of the folded
// value. Values are the EIP-160 spec constants, not the impl's own constants.
TEST(EVMMirBuilderConstFoldTest, ExpConstDynamicGasMatchesEip160) {
  // Post-Spurious-Dragon: 50 gas / significant exponent byte; 0 for exponent 0.
  EXPECT_EQ(EVMMirBuilder::constExpDynamicGas(intx::uint256{0}, EVMC_CANCUN),
            0u);
  EXPECT_EQ(EVMMirBuilder::constExpDynamicGas(intx::uint256{0xFF}, EVMC_CANCUN),
            50u);
  EXPECT_EQ(
      EVMMirBuilder::constExpDynamicGas(intx::uint256{0x100}, EVMC_CANCUN),
      100u);
  EXPECT_EQ(
      EVMMirBuilder::constExpDynamicGas(intx::uint256{0x010000}, EVMC_CANCUN),
      150u);
  // Full 32-byte exponent: 32 * 50 = 1600.
  EXPECT_EQ(EVMMirBuilder::constExpDynamicGas(~intx::uint256{0}, EVMC_CANCUN),
            1600u);
  // Pre-Spurious-Dragon: 10 gas / byte.
  EXPECT_EQ(
      EVMMirBuilder::constExpDynamicGas(intx::uint256{0xFF}, EVMC_FRONTIER),
      10u);
  EXPECT_EQ(
      EVMMirBuilder::constExpDynamicGas(intx::uint256{0x100}, EVMC_FRONTIER),
      20u);
}

TEST(EVMMirBuilderConstFoldTest, SignextendFoldsConstantOperands) {
  MirBuilderConstFoldHarness Harness;
  using Operand = EVMMirBuilder::Operand;
  using U256Value = EVMMirBuilder::U256Value;
  const uint64_t Ones = ~0ULL;

  auto fold = [&](U256Value Index, U256Value Value) {
    return Harness.Builder.handleSignextend(Operand(Index), Operand(Value));
  };

  // SIGNEXTEND(0, 0xFF): byte 0 sign bit set -> fills all higher bits with 1
  auto R = fold({0, 0, 0, 0}, {0xFF, 0, 0, 0});
  ASSERT_TRUE(R.isConstant());
  EXPECT_EQ(R.getConstValue(), (U256Value{Ones, Ones, Ones, Ones}));

  // SIGNEXTEND(0, 0x7F): byte 0 sign bit clear -> unchanged
  EXPECT_EQ(fold({0, 0, 0, 0}, {0x7F, 0, 0, 0}).getConstValue(),
            (U256Value{0x7F, 0, 0, 0}));

  // SIGNEXTEND(0, 0x1234): low byte 0x34, sign bit clear -> 0x34
  EXPECT_EQ(fold({0, 0, 0, 0}, {0x1234, 0, 0, 0}).getConstValue(),
            (U256Value{0x34, 0, 0, 0}));

  // SIGNEXTEND(1, 0x80FF): sign bit is bit 15 (set) -> ones above bit 15
  EXPECT_EQ(fold({1, 0, 0, 0}, {0x80FF, 0, 0, 0}).getConstValue(),
            (U256Value{0xFFFFFFFFFFFF80FFULL, Ones, Ones, Ones}));

  // SIGNEXTEND(31, x): index >= 31 leaves the value untouched
  EXPECT_EQ(fold({31, 0, 0, 0}, {1, 0, 0, Ones}).getConstValue(),
            (U256Value{1, 0, 0, Ones}));
}

TEST(EVMJITFrontendVisitorTest, DynamicJumpConsistencyErrorEscapesVisitor) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x04, // PC0 PUSH1 0x04 (analyzer resolves a constant destination)
      0x56,       // PC2 JUMP
      0xfe,       // PC3 INVALID padding
      0x5b,       // PC4 JUMPDEST
      0x00,       // PC5 STOP
  };
  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  DynamicPushMockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<DynamicPushMockEVMBuilder> Visitor(Builder,
                                                                  &Ctx);
  try {
    (void)Visitor.compile();
    FAIL() << "dynamic-jump consistency error was swallowed";
  } catch (const zen::common::Error &Error) {
    EXPECT_EQ(Error.getCode(),
              zen::common::ErrorCode::EVMDynamicJumpConsistencyFailed);
  }
}

TEST(EVMJITFrontendAnalyzerTest, ConstantJumpCanonicalizesJumpDestRuns) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x04, // PUSH1 0x04
      0x56,       // JUMP
      0x5b,       // JUMPDEST
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);

  EXPECT_TRUE(Analyzer.hasCanonicalJumpDest(3));
  EXPECT_TRUE(Analyzer.hasCanonicalJumpDest(4));
  EXPECT_EQ(Analyzer.getCanonicalJumpDestPC(3), 4U);
  EXPECT_EQ(Analyzer.getCanonicalJumpDestPC(4), 4U);

  const auto *EntryBlock = findBlock(Analyzer, 0);
  const auto *JumpDestBlock = findBlock(Analyzer, 4);
  ASSERT_NE(EntryBlock, nullptr);
  ASSERT_NE(JumpDestBlock, nullptr);
  EXPECT_EQ(findBlock(Analyzer, 3), nullptr);

  EXPECT_TRUE(EntryBlock->HasConstantJump);
  EXPECT_EQ(EntryBlock->ConstantJumpTargetPC, 4U);
  expectPCList(EntryBlock->Successors, {4});

  EXPECT_TRUE(JumpDestBlock->IsJumpDest);
  expectPCList(JumpDestBlock->Predecessors, {0});
}

TEST(EVMJITFrontendAnalyzerTest,
     SuitabilityOnlyKeepsFallbackMetricsWithoutBuildingCfg) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x04, // PUSH1 0x04
      0x56,       // JUMP
      0x5b,       // JUMPDEST
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  const EVMAnalyzer FullAnalyzer = analyzeBytecode(Bytecode);
  const EVMAnalyzer SuitabilityOnlyAnalyzer =
      analyzeSuitabilityOnlyBytecode(Bytecode);

  const auto &Full = FullAnalyzer.getJITSuitability();
  const auto &SuitabilityOnly = SuitabilityOnlyAnalyzer.getJITSuitability();

  EXPECT_EQ(SuitabilityOnly.ShouldFallback, Full.ShouldFallback);
  EXPECT_EQ(SuitabilityOnly.BytecodeSize, Full.BytecodeSize);
  EXPECT_EQ(SuitabilityOnly.MirEstimate, Full.MirEstimate);
  EXPECT_EQ(SuitabilityOnly.RAExpensiveCount, Full.RAExpensiveCount);
  EXPECT_EQ(SuitabilityOnly.MaxConsecutiveExpensive,
            Full.MaxConsecutiveExpensive);
  EXPECT_EQ(SuitabilityOnly.MaxBlockExpensiveCount,
            Full.MaxBlockExpensiveCount);
  EXPECT_EQ(SuitabilityOnly.DupFeedbackPatternCount,
            Full.DupFeedbackPatternCount);

  EXPECT_TRUE(SuitabilityOnlyAnalyzer.getBlockInfos().empty());
  EXPECT_FALSE(SuitabilityOnlyAnalyzer.hasCanonicalJumpDest(3));
  EXPECT_FALSE(SuitabilityOnlyAnalyzer.hasCanonicalJumpDest(4));
  EXPECT_FALSE(SuitabilityOnlyAnalyzer.hasUnknownDynamicJumpTargets());
}

TEST(EVMJITFrontendAnalyzerTest,
     ConstantJumpiKeepsMatchingEntryDepthSuccessorsLiftable) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x01, // PUSH1 0x01
      0x60, 0x06, // PUSH1 0x06
      0x57,       // JUMPI
      0x00,       // STOP
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);

  const auto *EntryBlock = findBlock(Analyzer, 0);
  const auto *FallthroughBlock = findBlock(Analyzer, 5);
  const auto *JumpDestBlock = findBlock(Analyzer, 6);
  ASSERT_NE(EntryBlock, nullptr);
  ASSERT_NE(FallthroughBlock, nullptr);
  ASSERT_NE(JumpDestBlock, nullptr);

  EXPECT_TRUE(EntryBlock->HasConditionalJump);
  EXPECT_TRUE(EntryBlock->HasConstantJump);
  EXPECT_FALSE(EntryBlock->HasDynamicJump);
  expectPCList(EntryBlock->Successors, {5, 6});

  EXPECT_EQ(FallthroughBlock->ResolvedEntryStackDepth, 0);
  EXPECT_TRUE(FallthroughBlock->CanLiftStack);
  expectPCList(FallthroughBlock->Predecessors, {0});

  EXPECT_TRUE(JumpDestBlock->IsJumpDest);
  EXPECT_EQ(JumpDestBlock->ResolvedEntryStackDepth, 0);
  EXPECT_TRUE(JumpDestBlock->CanLiftStack);
  expectPCList(JumpDestBlock->Predecessors, {0});
}

TEST(EVMJITFrontendAnalyzerTest,
     DynamicJumpForcesReachableJumpDestsToFallback) {
  // The dynamic-jump source block pushes the CALLDATALOAD offset itself so it
  // does not underflow its resolved entry depth: an underflowing block (whose
  // ResolvedEntryStackDepth + MinStackHeight < 0) is never lifted, which would
  // otherwise mask the property under test -- that a *reachable JUMPDEST* is
  // forced to fall back because it is a dynamic-jump target candidate.
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x01, // PUSH1 0x01
      0x60, 0x09, // PUSH1 0x09
      0x57,       // JUMPI
      0x60, 0x00, // PUSH1 0x00 (CALLDATALOAD offset)
      0x35,       // CALLDATALOAD
      0x56,       // JUMP
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);

  const auto *EntryBlock = findBlock(Analyzer, 0);
  const auto *DynamicJumpBlock = findBlock(Analyzer, 5);
  const auto *JumpDestBlock = findBlock(Analyzer, 9);
  ASSERT_NE(EntryBlock, nullptr);
  ASSERT_NE(DynamicJumpBlock, nullptr);
  ASSERT_NE(JumpDestBlock, nullptr);

  EXPECT_TRUE(Analyzer.hasUnknownDynamicJumpTargets());
  EXPECT_TRUE(EntryBlock->HasConditionalJump);
  EXPECT_TRUE(DynamicJumpBlock->HasDynamicJump);
  EXPECT_TRUE(DynamicJumpBlock->CanLiftStack);

  EXPECT_TRUE(JumpDestBlock->IsJumpDest);
  EXPECT_EQ(JumpDestBlock->ResolvedEntryStackDepth, 0);
  EXPECT_FALSE(JumpDestBlock->CanLiftStack);
}

TEST(EVMJITFrontendAnalyzerTest,
     UnderResolvedEntryDepthInvalidatesStaticSuccessors) {
  // A block whose resolved entry depth cannot cover its own stack pops
  // (ResolvedEntryStackDepth + MinStackHeight < 0) has an untrustworthy
  // absolute depth. Any static successor whose depth may depend on that exit
  // is equally untrustworthy and must not feed entry-shape or range analysis.
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x01, // PC0 PUSH1 0x01 (cond)
      0x60, 0x0b, // PC2 PUSH1 0x0b (jumpi dest)
      0x57,       // PC4 JUMPI
      0x01,       // PC5 ADD (fallthrough block pops two slots at entry depth 0)
      0x60, 0x00, // PC6 PUSH1 0x00
      0x60, 0x0b, // PC8 PUSH1 0x0b
      0x56,       // PC10 JUMP
      0x5b,       // PC11 JUMPDEST
      0x00        // PC12 STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *UnderflowBlock = findBlock(Analyzer, 5);
  const auto *SuccessorBlock = findBlock(Analyzer, 11);
  ASSERT_NE(UnderflowBlock, nullptr);
  ASSERT_NE(SuccessorBlock, nullptr);

  EXPECT_EQ(UnderflowBlock->MinStackHeight, -2);
  EXPECT_EQ(UnderflowBlock->ResolvedEntryStackDepth, -1);
  EXPECT_TRUE(UnderflowBlock->HasInconsistentEntryDepth);
  EXPECT_FALSE(UnderflowBlock->CanLiftStack);
  EXPECT_TRUE(UnderflowBlock->EntryStackRanges.empty());

  EXPECT_EQ(SuccessorBlock->ResolvedEntryStackDepth, -1);
  EXPECT_TRUE(SuccessorBlock->HasInconsistentEntryDepth);
  EXPECT_FALSE(SuccessorBlock->CanLiftStack);
  EXPECT_TRUE(SuccessorBlock->EntryStackRanges.empty());
}

TEST(EVMJITFrontendAnalyzerTest, DeadDeepEntryBlocksDoNotTriggerFallbackRisk) {
  // The dynamic source and deep-entry chain are both behind STOP. A module-
  // global unknown-jump flag must not make their JUMPDESTs possibly reachable.
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

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *DynamicSourceBlock = findBlock(Analyzer, 1);
  const auto *DeepEntryBlock = findBlock(Analyzer, 7);
  ASSERT_NE(DynamicSourceBlock, nullptr);
  ASSERT_NE(DeepEntryBlock, nullptr);
  EXPECT_TRUE(Analyzer.hasUnknownDynamicJumpTargets());
  EXPECT_TRUE(DynamicSourceBlock->HasDynamicJump);
  EXPECT_EQ(DynamicSourceBlock->ResolvedEntryStackDepth, -1);
  EXPECT_EQ(DeepEntryBlock->ResolvedEntryStackDepth, -1);
  EXPECT_EQ(DeepEntryBlock->MinStackHeight, -2);
  EXPECT_FALSE(Analyzer.hasUnresolvedNonLiftedDeepEntryRisk());
}

TEST(EVMJITFrontendAnalyzerTest,
     RegionHeuristicDepthTaintPropagatesAndBlocksLifting) {
  // A block whose resolved entry depth was produced by the dynamic-jump region
  // uniform-entry heuristic -- rather than by static propagation from the
  // function entry -- must never be lifted, and the taint must follow the depth
  // along static edges. Here the entry block ends in a dynamic JUMP, so the
  // JUMPDEST region behind it (PC4) is not statically reachable and receives
  // its entry depth from the heuristic. PC4 then flows statically into a plain
  // continuation block (PC10) that inherits the heuristic depth by propagation.
  // Both must be tainted and unlifted; PC10 -- a non-JUMPDEST block with a
  // clean depth-0 entry that would otherwise lift -- is the observable proof
  // that the taint propagated. Without the taint rule PC10 (entry depth 0,
  // MinStackHeight 0) would satisfy the >= 0 sanity check and lift with a
  // heuristic-derived absolute depth.
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x00, // PC0  PUSH1 0x00 (CALLDATALOAD offset)
      0x35,       // PC2  CALLDATALOAD (dynamic jump value)
      0x56,       // PC3  JUMP (dynamic; region entry = PC4)
      0x5b,       // PC4  JUMPDEST (region target, reached only dynamically)
      0x60, 0x01, // PC5  PUSH1 0x01 (cond)
      0x60, 0x0b, // PC7  PUSH1 0x0b (jumpi dest = PC11)
      0x57,       // PC9  JUMPI (fallthrough = PC10, taken = PC11)
      0x00,       // PC10 STOP (static continuation, inherits heuristic depth)
      0x5b,       // PC11 JUMPDEST
      0x00        // PC12 STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);

  const auto *EntryBlock = findBlock(Analyzer, 0);
  const auto *RegionJumpDest = findBlock(Analyzer, 4);
  const auto *Continuation = findBlock(Analyzer, 10);
  ASSERT_NE(EntryBlock, nullptr);
  ASSERT_NE(RegionJumpDest, nullptr);
  ASSERT_NE(Continuation, nullptr);

  EXPECT_TRUE(Analyzer.hasUnknownDynamicJumpTargets());

  // The entry block resolves purely from the function entry (seed depth 0), so
  // it is trusted -- never tainted -- even though it carries the dynamic jump.
  EXPECT_TRUE(EntryBlock->HasDynamicJump);
  EXPECT_FALSE(EntryBlock->EntryDepthFromRegionHeuristic);

  // The region JUMPDEST's depth is the heuristic's guess: tainted and unlifted.
  EXPECT_EQ(RegionJumpDest->ResolvedEntryStackDepth, 0);
  EXPECT_TRUE(RegionJumpDest->EntryDepthFromRegionHeuristic);
  EXPECT_FALSE(RegionJumpDest->CanLiftStack);

  // The static successor inherits the tainted depth by propagation. It is not a
  // JUMPDEST (not a dynamic-jump candidate) and enters at a clean depth 0, so
  // only the taint keeps it out of lifting.
  EXPECT_EQ(Continuation->ResolvedEntryStackDepth, 0);
  EXPECT_EQ(Continuation->MinStackHeight, 0);
  EXPECT_FALSE(Continuation->IsDynamicJumpTargetCandidate);
  EXPECT_TRUE(Continuation->EntryDepthFromRegionHeuristic);
  EXPECT_FALSE(Continuation->CanLiftStack);
}

TEST(EVMJITFrontendAnalyzerTest, HiddenEntryPrefixKeepsStaticMergesLiftable) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0xaa, // PUSH1 preserved prefix
      0x60, 0x01, // PUSH1 cond
      0x60, 0x0a, // PUSH1 jumpdest
      0x57,       // JUMPI
      0x60, 0x00, // PUSH1 0x00
      0x00,       // STOP
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *FallthroughBlock = findBlock(Analyzer, 7);
  const auto *JumpDestBlock = findBlock(Analyzer, 10);
  ASSERT_NE(FallthroughBlock, nullptr);
  ASSERT_NE(JumpDestBlock, nullptr);

  EXPECT_EQ(FallthroughBlock->ResolvedEntryStackDepth, 1);
  EXPECT_EQ(FallthroughBlock->EntryStackDepth, 0);
  EXPECT_TRUE(FallthroughBlock->CanLiftStack);

  EXPECT_EQ(JumpDestBlock->ResolvedEntryStackDepth, 1);
  EXPECT_EQ(JumpDestBlock->EntryStackDepth, 0);
  EXPECT_TRUE(JumpDestBlock->CanLiftStack);
}

TEST(EVMJITFrontendAnalyzerTest, MergeDepthConflictDisablesLiftedEntry) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x01, // PUSH1 0x01
      0x60, 0x0a, // PUSH1 0x0a
      0x57,       // JUMPI
      0x60, 0x02, // PUSH1 0x02
      0x60, 0x0a, // PUSH1 0x0a
      0x56,       // JUMP
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);

  const auto *EntryBlock = findBlock(Analyzer, 0);
  const auto *FallthroughBlock = findBlock(Analyzer, 5);
  const auto *MergeBlock = findBlock(Analyzer, 10);
  ASSERT_NE(EntryBlock, nullptr);
  ASSERT_NE(FallthroughBlock, nullptr);
  ASSERT_NE(MergeBlock, nullptr);

  EXPECT_TRUE(EntryBlock->HasConditionalJump);
  EXPECT_EQ(FallthroughBlock->ResolvedEntryStackDepth, 0);
  EXPECT_EQ(FallthroughBlock->ResolvedExitStackDepth, 1);
  EXPECT_TRUE(FallthroughBlock->CanLiftStack);

  EXPECT_TRUE(MergeBlock->IsJumpDest);
  EXPECT_EQ(MergeBlock->ResolvedEntryStackDepth, -1);
  EXPECT_EQ(MergeBlock->ResolvedExitStackDepth, -1);
  EXPECT_TRUE(MergeBlock->HasInconsistentEntryDepth);
  EXPECT_FALSE(MergeBlock->CanLiftStack);
  expectPCList(MergeBlock->Predecessors, {0, 5});
}

TEST(EVMJITFrontendAnalyzerTest,
     InconsistentMergeInvalidatesReachableSuccessors) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x01, // PUSH1 0x01
      0x60, 0x0a, // PUSH1 0x0a
      0x57,       // JUMPI
      0x60, 0x02, // PUSH1 0x02
      0x60, 0x0a, // PUSH1 0x0a
      0x56,       // JUMP
      0x5b,       // JUMPDEST
      0x60, 0x03, // PUSH1 0x03
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);

  const auto *MergeBlock = findBlock(Analyzer, 10);
  const auto *SuccessorBlock = findBlock(Analyzer, 13);
  ASSERT_NE(MergeBlock, nullptr);
  ASSERT_NE(SuccessorBlock, nullptr);

  EXPECT_EQ(MergeBlock->ResolvedEntryStackDepth, -1);
  EXPECT_EQ(MergeBlock->ResolvedExitStackDepth, -1);
  EXPECT_TRUE(MergeBlock->HasInconsistentEntryDepth);
  EXPECT_FALSE(MergeBlock->CanLiftStack);

  EXPECT_TRUE(SuccessorBlock->IsJumpDest);
  EXPECT_EQ(SuccessorBlock->ResolvedEntryStackDepth, -1);
  EXPECT_EQ(SuccessorBlock->ResolvedExitStackDepth, -1);
  EXPECT_TRUE(SuccessorBlock->HasInconsistentEntryDepth);
  EXPECT_FALSE(SuccessorBlock->CanLiftStack);
  expectPCList(SuccessorBlock->Predecessors, {10});
}

TEST(EVMJITFrontendVisitorTest,
     MaterializedBlockKeepsPopDupSwapAndAddOnLogicalStack) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0xaa, // PUSH1 0xaa
      0x60, 0xbb, // PUSH1 0xbb
      0x60, 0x01, // PUSH1 cond
      0x60, 0x0e, // PUSH1 jumpdest
      0x57,       // JUMPI
      0x60, 0xcc, // PUSH1 0xcc
      0x60, 0x0e, // PUSH1 jumpdest
      0x56,       // JUMP
      0x5b,       // JUMPDEST
      0x50,       // POP
      0x80,       // DUP1
      0x90,       // SWAP1
      0x01,       // ADD
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *TargetBlock = findBlock(Analyzer, 14);
  ASSERT_NE(TargetBlock, nullptr);
  EXPECT_FALSE(TargetBlock->CanLiftStack);
  EXPECT_EQ(TargetBlock->ResolvedEntryStackDepth, -1);
  EXPECT_EQ(TargetBlock->ResolvedExitStackDepth, -1);
  EXPECT_TRUE(TargetBlock->HasInconsistentEntryDepth);

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);

  const auto &PopStats = Builder.accessStats(OP_POP);
  EXPECT_EQ(PopStats.StackPopCount, 0U);
  EXPECT_EQ(PopStats.StackGetCount, 0U);
  EXPECT_EQ(PopStats.StackSetCount, 0U);

  const auto &DupStats = Builder.accessStats(OP_DUP1);
  EXPECT_EQ(DupStats.StackPopCount, 0U);
  EXPECT_EQ(DupStats.StackGetCount, 0U);
  EXPECT_EQ(DupStats.StackSetCount, 0U);

  const auto &SwapStats = Builder.accessStats(OP_SWAP1);
  EXPECT_EQ(SwapStats.StackPopCount, 0U);
  EXPECT_EQ(SwapStats.StackGetCount, 0U);
  EXPECT_EQ(SwapStats.StackSetCount, 0U);

  const auto &AddStats = Builder.accessStats(OP_ADD);
  EXPECT_EQ(AddStats.StackPopCount, 0U);
  EXPECT_EQ(AddStats.StackGetCount, 0U);
  EXPECT_EQ(AddStats.StackSetCount, 0U);
}

TEST(EVMJITFrontendVisitorTest,
     LiftedJumpDestEntryDoesNotDependOnRuntimeStackDepth) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x01, // PUSH1 0x01
      0x5b,       // JUMPDEST
      0x80,       // DUP1
      0x50,       // POP
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *EntryBlock = findBlock(Analyzer, 0);
  const auto *JumpDestBlock = findBlock(Analyzer, 2);
  ASSERT_NE(EntryBlock, nullptr);
  ASSERT_NE(JumpDestBlock, nullptr);
  EXPECT_TRUE(EntryBlock->CanLiftStack);
  EXPECT_TRUE(JumpDestBlock->CanLiftStack);
  EXPECT_EQ(JumpDestBlock->ResolvedEntryStackDepth, 1);

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  Builder.enableRuntimeStackChecks();
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);
}

TEST(EVMJITFrontendVisitorTest, FusesPushConstJumpIntoMeteredRange) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x03, // PUSH1 0x03
      0x56,       // JUMP
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);

  EXPECT_TRUE(Builder.meteredRanges().empty());
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_PUSH1), 1U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_JUMP), 1U);
}

TEST(EVMJITFrontendVisitorTest, FusesPushConstJumpiIntoMeteredRange) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x01, // PUSH1 cond
      0x60, 0x06, // PUSH1 jumpdest
      0x57,       // JUMPI
      0x00,       // STOP
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);

  EXPECT_TRUE(Builder.meteredRanges().empty());
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_PUSH1), 2U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_JUMPI), 1U);
}

TEST(EVMJITFrontendVisitorTest, FusesIszeroPushConstJumpiIntoMeteredRange) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x00, // PUSH1 value
      0x15,       // ISZERO
      0x60, 0x06, // PUSH1 jumpdest
      0x57,       // JUMPI
      0x00,       // STOP
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);

  EXPECT_TRUE(Builder.meteredRanges().empty());
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_ISZERO), 1U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_PUSH1), 2U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_JUMPI), 1U);
}

TEST(EVMJITFrontendVisitorTest, FusesDupAddIntoMeteredRange) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x20, // PUSH1 base
      0x60, 0x04, // PUSH1 offset
      0x5f,       // PUSH0
      0x50,       // POP
      0x81,       // DUP2
      0x01,       // ADD
      0x00        // STOP
  };

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);

  EXPECT_TRUE(Builder.meteredRanges().empty());
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_DUP2), 1U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_ADD), 1U);
  EXPECT_EQ(Builder.runtimeStackDepth(), 2U);
  EXPECT_EQ(Builder.topStackValue()[0], 0x24U);
}

TEST(EVMJITFrontendVisitorTest, FusesPushConstAddIntoMeteredRange) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x20, // PUSH1 base
      0x60, 0x04, // PUSH1 offset
      0x01,       // ADD
      0x00        // STOP
  };

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);

  EXPECT_TRUE(Builder.meteredRanges().empty());
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_PUSH1), 2U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_ADD), 1U);
  EXPECT_EQ(Builder.runtimeStackDepth(), 1U);
  EXPECT_EQ(Builder.topStackValue()[0], 0x24U);
}

TEST(EVMJITFrontendVisitorTest, FusesPushConstDupAddIntoMeteredRange) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x20, // PUSH1 base
      0x60, 0x04, // PUSH1 offset
      0x81,       // DUP2
      0x01,       // ADD
      0x00        // STOP
  };

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);

  EXPECT_TRUE(Builder.meteredRanges().empty());
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_PUSH1), 2U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_DUP2), 1U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_ADD), 1U);
  EXPECT_EQ(Builder.runtimeStackDepth(), 2U);
  EXPECT_EQ(Builder.topStackValue()[0], 0x24U);
}

TEST(EVMJITFrontendVisitorTest, FusesPushConstMStoreIntoMeteredRange) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0xaa, // PUSH1 value
      0x60, 0x20, // PUSH1 addr
      0x52,       // MSTORE
      0x00        // STOP
  };

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);

  EXPECT_TRUE(Builder.meteredRanges().empty());
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_PUSH1), 2U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_MSTORE), 1U);
  EXPECT_EQ(Builder.runtimeStackDepth(), 0U);
  EXPECT_EQ(Builder.mstoreCount(), 1U);
  EXPECT_EQ(Builder.lastMStore().Addr[0], 0x20U);
  EXPECT_EQ(Builder.lastMStore().Value[0], 0xaaU);
}

TEST(EVMJITFrontendVisitorTest, FusesAddMStoreIntoMeteredRange) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0xaa, // PUSH1 value
      0x60, 0x20, // PUSH1 base
      0x60, 0x04, // PUSH1 delta
      0x01,       // ADD
      0x52,       // MSTORE
      0x00        // STOP
  };

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);

  EXPECT_TRUE(Builder.meteredRanges().empty());
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_ADD), 1U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_MSTORE), 1U);
  EXPECT_EQ(Builder.runtimeStackDepth(), 0U);
  EXPECT_EQ(Builder.mstoreCount(), 1U);
  EXPECT_EQ(Builder.lastMStore().Addr[0], 0x24U);
  EXPECT_EQ(Builder.lastMStore().Value[0], 0xaaU);
}

TEST(EVMJITFrontendVisitorTest, FusesLinearMStoreNextMotifIntoMeteredRange) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x20, // PUSH1 stride
      0x60, 0x40, // PUSH1 current
      0x80,       // DUP1
      0x80,       // DUP1
      0x52,       // MSTORE
      0x81,       // DUP2
      0x01,       // ADD
      0x00        // STOP
  };

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);

  EXPECT_TRUE(Builder.meteredRanges().empty());
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_DUP1), 2U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_DUP2), 1U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_MSTORE), 1U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_ADD), 1U);
  EXPECT_EQ(Builder.mstoreCount(), 1U);
  EXPECT_EQ(Builder.lastMStore().Addr[0], 0x40U);
  EXPECT_EQ(Builder.lastMStore().Value[0], 0x40U);
  EXPECT_EQ(Builder.runtimeStackDepth(), 2U);
  EXPECT_EQ(Builder.topStackValue()[0], 0x60U);
}

TEST(EVMJITFrontendVisitorTest, FusesCallerSlotKeccakIntoMeteredRange) {
  const std::vector<uint8_t> Bytecode = {
      0x33,       // CALLER
      0x60, 0x00, // PUSH1 base
      0x52,       // MSTORE
      0x60, 0x05, // PUSH1 slot
      0x60, 0x20, // PUSH1 base + 0x20
      0x52,       // MSTORE
      0x60, 0x40, // PUSH1 0x40
      0x60, 0x00, // PUSH1 base
      0x20,       // KECCAK256
      0x00        // STOP
  };

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);

  EXPECT_TRUE(Builder.meteredRanges().empty());
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_CALLER), 1U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_MSTORE), 2U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_KECCAK256), 1U);
  EXPECT_EQ(Builder.keccakCallerSlotCount(), 1U);
  EXPECT_EQ(Builder.keccakCount(), 0U);
  EXPECT_EQ(Builder.mstoreCount(), 0U);
  EXPECT_EQ(Builder.lastKeccakCallerSlot().Offset[0], 0U);
  EXPECT_EQ(Builder.lastKeccakCallerSlot().Slot[0], 5U);
  EXPECT_EQ(Builder.runtimeStackDepth(), 1U);
}

TEST(EVMJITFrontendVisitorTest, FusesCallDataSlotKeccakIntoMeteredRange) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x04, // PUSH1 calldata offset
      0x35,       // CALLDATALOAD
      0x60, 0x00, // PUSH1 base
      0x52,       // MSTORE
      0x60, 0x07, // PUSH1 slot
      0x60, 0x20, // PUSH1 base + 0x20
      0x52,       // MSTORE
      0x60, 0x40, // PUSH1 0x40
      0x60, 0x00, // PUSH1 base
      0x20,       // KECCAK256
      0x00        // STOP
  };

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);

  EXPECT_TRUE(Builder.meteredRanges().empty());
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_PUSH1), 6U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_CALLDATALOAD), 1U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_MSTORE), 2U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_KECCAK256), 1U);
  EXPECT_EQ(Builder.keccakCallDataSlotCount(), 1U);
  EXPECT_EQ(Builder.keccakCount(), 0U);
  EXPECT_EQ(Builder.mstoreCount(), 0U);
  EXPECT_EQ(Builder.lastKeccakCallDataSlot().Offset[0], 0U);
  EXPECT_EQ(Builder.lastKeccakCallDataSlot().CallDataOffset[0], 4U);
  EXPECT_EQ(Builder.lastKeccakCallDataSlot().Slot[0], 7U);
  EXPECT_EQ(Builder.runtimeStackDepth(), 1U);
}

TEST(EVMJITFrontendVisitorTest,
     KeepsGenericKeccakPathWhenTwoWordLayoutDoesNotMatch) {
  const std::vector<uint8_t> Bytecode = {
      0x33,       // CALLER
      0x60, 0x00, // PUSH1 base
      0x52,       // MSTORE
      0x60, 0x05, // PUSH1 slot
      0x60, 0x21, // PUSH1 mismatched base + 0x21
      0x52,       // MSTORE
      0x60, 0x40, // PUSH1 0x40
      0x60, 0x00, // PUSH1 base
      0x20,       // KECCAK256
      0x00        // STOP
  };

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);

  EXPECT_TRUE(Builder.meteredRanges().empty());
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_CALLER), 1U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_MSTORE), 2U);
  EXPECT_EQ(Builder.meteredOpcodeCount(OP_KECCAK256), 1U);
  EXPECT_EQ(Builder.keccakCallerSlotCount(), 0U);
  EXPECT_EQ(Builder.keccakCallDataSlotCount(), 0U);
  EXPECT_EQ(Builder.keccakCount(), 1U);
  EXPECT_EQ(Builder.mstoreCount(), 2U);
  EXPECT_EQ(Builder.runtimeStackDepth(), 1U);
}

TEST(EVMJITFrontendVisitorTest,
     ImplicitStopMaterializesLiftedStackOnFallthrough) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0xaa // PUSH1 0xaa
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *EntryBlock = findBlock(Analyzer, 0);
  ASSERT_NE(EntryBlock, nullptr);
  EXPECT_TRUE(EntryBlock->CanLiftStack);
  EXPECT_EQ(EntryBlock->ResolvedExitStackDepth, 1);

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);
  EXPECT_EQ(Builder.runtimeStackDepth(), 1U);
  EXPECT_EQ(Builder.topStackValue()[0], 0xaaU);
}

TEST(EVMJITFrontendVisitorTest,
     UndefinedInstructionAfterProducerDoesNotTriggerStackOverflowTrap) {
  const std::vector<uint8_t> Bytecode = {
      0x30, // ADDRESS
      0x2a  // undefined
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *EntryBlock = findBlock(Analyzer, 0);
  ASSERT_NE(EntryBlock, nullptr);
  EXPECT_TRUE(EntryBlock->HasUndefinedInstr);
  EXPECT_EQ(EntryBlock->MaxStackHeight, 1);

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  Builder.enableRuntimeStackChecks();
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_TRUE(Builder.Undefined);
}

TEST(EVMJITFrontendVisitorTest, TruncatedPushIsRightPaddedWithZeros) {
  const std::vector<uint8_t> Bytecode = {
      0x62, 0x12, 0x34 // PUSH3 0x12 0x34 <missing byte>
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *EntryBlock = findBlock(Analyzer, 0);
  ASSERT_NE(EntryBlock, nullptr);
  EXPECT_EQ(EntryBlock->ResolvedExitStackDepth, 1);

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);
  ASSERT_TRUE(Builder.hasLastPushValue());
  EXPECT_EQ(Builder.lastPushValue()[0], 0x123400ULL);
  EXPECT_EQ(Builder.lastPushValue()[1], 0ULL);
  EXPECT_EQ(Builder.lastPushValue()[2], 0ULL);
  EXPECT_EQ(Builder.lastPushValue()[3], 0ULL);
}

} // namespace
