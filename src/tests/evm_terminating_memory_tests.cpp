// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "compiler/evm_frontend/evm_imported.h"
#include "compiler/evm_frontend/evm_mir_compiler.h"
#include "compiler/mir/module.h"

#include "llvm/Support/raw_ostream.h"
#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace {

using COMPILER::EVMMirBuilder;

#ifdef ZEN_ENABLE_EVM_MEMORY_PLAN_FRAMEWORK
std::optional<size_t>
countTerminatingExpandMemoryCalls(const std::vector<uint8_t> &Bytecode) {
  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  COMPILER::MModule Mod(Ctx);
  std::array<COMPILER::MType *, 1> ParamTypes = {
      COMPILER::MPointerType::create(Ctx, Ctx.VoidType)};
  COMPILER::MFunctionType *FuncType = COMPILER::MFunctionType::create(
      Ctx, Ctx.VoidType, llvm::ArrayRef<COMPILER::MType *>(ParamTypes));
  Mod.addFuncType(FuncType);

  COMPILER::MFunction Func(Ctx, 0);
  Func.setFunctionType(FuncType);
  EVMMirBuilder Builder(Ctx, Func);
  if (!Builder.compile(&Ctx)) {
    return std::nullopt;
  }

  std::string Mir;
  llvm::raw_string_ostream OS(Mir);
  Func.print(OS);
  OS.flush();

  const auto &RuntimeFunctions = COMPILER::getRuntimeFunctionTable();
  const auto Address =
      COMPILER::getFunctionAddress(RuntimeFunctions.ExpandMemoryNoGas);
  const std::string Needle =
      "target = const.i64 " + std::to_string(Address) + ", ";
  size_t Count = 0;
  for (size_t Pos = Mir.find(Needle); Pos != std::string::npos;
       Pos = Mir.find(Needle, Pos + Needle.size())) {
    ++Count;
  }
  return Count;
}

TEST(EVMMirBuilderTerminatingMemoryProofTest,
     ReusesCrossBlockProofForReturnAndRevert) {
  const std::vector<uint8_t> ZeroSizeReturn = {
      OP_PUSH1, 0x01,    OP_PUSH1,    0x80,     OP_MSTORE, OP_PUSH1,
      0x08,     OP_JUMP, OP_JUMPDEST, OP_PUSH0, OP_PUSH0,  OP_RETURN};
  const std::vector<uint8_t> CoveredReturn = {
      OP_PUSH1, 0x01,        OP_PUSH1, 0x80, OP_MSTORE, OP_PUSH1, 0x08,
      OP_JUMP,  OP_JUMPDEST, OP_PUSH1, 0x20, OP_PUSH1,  0x80,     OP_RETURN};
  const std::vector<uint8_t> ZeroSizeRevert = {
      OP_PUSH1, 0x01,    OP_PUSH1,    0x80,     OP_MSTORE, OP_PUSH1,
      0x08,     OP_JUMP, OP_JUMPDEST, OP_PUSH0, OP_PUSH0,  OP_REVERT};
  const std::vector<uint8_t> CoveredRevert = {
      OP_PUSH1, 0x01,        OP_PUSH1, 0x80, OP_MSTORE, OP_PUSH1, 0x08,
      OP_JUMP,  OP_JUMPDEST, OP_PUSH1, 0x20, OP_PUSH1,  0x80,     OP_REVERT};

  const auto ZeroReturnCalls =
      countTerminatingExpandMemoryCalls(ZeroSizeReturn);
  const auto ReturnCalls = countTerminatingExpandMemoryCalls(CoveredReturn);
  const auto ZeroRevertCalls =
      countTerminatingExpandMemoryCalls(ZeroSizeRevert);
  const auto RevertCalls = countTerminatingExpandMemoryCalls(CoveredRevert);
  ASSERT_TRUE(ZeroReturnCalls.has_value());
  ASSERT_TRUE(ReturnCalls.has_value());
  ASSERT_TRUE(ZeroRevertCalls.has_value());
  ASSERT_TRUE(RevertCalls.has_value());
  EXPECT_EQ(*ReturnCalls, *ZeroReturnCalls);
  EXPECT_EQ(*RevertCalls, *ZeroRevertCalls);
}

TEST(EVMMirBuilderTerminatingMemoryProofTest,
     RetainsUncoveredFallbackAndZeroSizeSemantics) {
  const std::vector<uint8_t> ZeroSizeReturn = {
      OP_PUSH1, 0x01,    OP_PUSH1,    0x80,     OP_MSTORE, OP_PUSH1,
      0x08,     OP_JUMP, OP_JUMPDEST, OP_PUSH0, OP_PUSH0,  OP_RETURN};
  const std::vector<uint8_t> UncoveredReturn = {
      OP_PUSH1, 0x01,        OP_PUSH1, 0x80, OP_MSTORE, OP_PUSH1, 0x08,
      OP_JUMP,  OP_JUMPDEST, OP_PUSH1, 0x20, OP_PUSH1,  0xa0,     OP_RETURN};
  const std::vector<uint8_t> HugeOffsetZeroSizeRevert = {
      OP_PUSH1, 0x01,        OP_PUSH1, 0x80,     OP_MSTORE, OP_PUSH1, 0x08,
      OP_JUMP,  OP_JUMPDEST, OP_PUSH0, OP_PUSH8, 0xff,      0xff,     0xff,
      0xff,     0xff,        0xff,     0xff,     0xff,      OP_REVERT};
  const std::vector<uint8_t> ZeroSizeRevert = {
      OP_PUSH1, 0x01,    OP_PUSH1,    0x80,     OP_MSTORE, OP_PUSH1,
      0x08,     OP_JUMP, OP_JUMPDEST, OP_PUSH0, OP_PUSH0,  OP_REVERT};

  const auto ZeroReturnCalls =
      countTerminatingExpandMemoryCalls(ZeroSizeReturn);
  const auto UncoveredCalls =
      countTerminatingExpandMemoryCalls(UncoveredReturn);
  const auto HugeOffsetZeroSizeCalls =
      countTerminatingExpandMemoryCalls(HugeOffsetZeroSizeRevert);
  const auto ZeroRevertCalls =
      countTerminatingExpandMemoryCalls(ZeroSizeRevert);
  ASSERT_TRUE(ZeroReturnCalls.has_value());
  ASSERT_TRUE(UncoveredCalls.has_value());
  ASSERT_TRUE(HugeOffsetZeroSizeCalls.has_value());
  ASSERT_TRUE(ZeroRevertCalls.has_value());
  EXPECT_EQ(*UncoveredCalls, *ZeroReturnCalls + 1);
  EXPECT_EQ(*HugeOffsetZeroSizeCalls, *ZeroRevertCalls);
}
#endif

} // namespace
