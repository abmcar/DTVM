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
std::optional<std::string>
compileTerminatingMemoryMir(const std::vector<uint8_t> &Bytecode) {
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
  return Mir;
}

template <typename FuncType>
bool containsTerminatingRuntimeCall(const std::string &Mir, FuncType Function) {
  const std::string Needle =
      "target = const.i64 " +
      std::to_string(COMPILER::getFunctionAddress(Function)) + ", ";
  return Mir.find(Needle) != std::string::npos;
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

  const auto ZeroReturnMir = compileTerminatingMemoryMir(ZeroSizeReturn);
  const auto ReturnMir = compileTerminatingMemoryMir(CoveredReturn);
  const auto ZeroRevertMir = compileTerminatingMemoryMir(ZeroSizeRevert);
  const auto RevertMir = compileTerminatingMemoryMir(CoveredRevert);
  ASSERT_TRUE(ZeroReturnMir.has_value());
  ASSERT_TRUE(ReturnMir.has_value());
  ASSERT_TRUE(ZeroRevertMir.has_value());
  ASSERT_TRUE(RevertMir.has_value());

  const auto &RuntimeFunctions = COMPILER::getRuntimeFunctionTable();
  EXPECT_TRUE(containsTerminatingRuntimeCall(
      *ZeroReturnMir, RuntimeFunctions.SetReturnNoExpand));
  EXPECT_TRUE(containsTerminatingRuntimeCall(
      *ReturnMir, RuntimeFunctions.SetReturnNoExpand));
  EXPECT_FALSE(
      containsTerminatingRuntimeCall(*ReturnMir, RuntimeFunctions.SetReturn));
  EXPECT_TRUE(containsTerminatingRuntimeCall(
      *ZeroRevertMir, RuntimeFunctions.SetRevertNoExpand));
  EXPECT_TRUE(containsTerminatingRuntimeCall(
      *RevertMir, RuntimeFunctions.SetRevertNoExpand));
  EXPECT_FALSE(
      containsTerminatingRuntimeCall(*RevertMir, RuntimeFunctions.SetRevert));
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

  const auto ZeroReturnMir = compileTerminatingMemoryMir(ZeroSizeReturn);
  const auto UncoveredMir = compileTerminatingMemoryMir(UncoveredReturn);
  const auto HugeOffsetZeroSizeMir =
      compileTerminatingMemoryMir(HugeOffsetZeroSizeRevert);
  const auto ZeroRevertMir = compileTerminatingMemoryMir(ZeroSizeRevert);
  ASSERT_TRUE(ZeroReturnMir.has_value());
  ASSERT_TRUE(UncoveredMir.has_value());
  ASSERT_TRUE(HugeOffsetZeroSizeMir.has_value());
  ASSERT_TRUE(ZeroRevertMir.has_value());

  const auto &RuntimeFunctions = COMPILER::getRuntimeFunctionTable();
  EXPECT_TRUE(containsTerminatingRuntimeCall(
      *ZeroReturnMir, RuntimeFunctions.SetReturnNoExpand));
  EXPECT_TRUE(containsTerminatingRuntimeCall(*UncoveredMir,
                                             RuntimeFunctions.SetReturn));
  EXPECT_FALSE(containsTerminatingRuntimeCall(
      *UncoveredMir, RuntimeFunctions.SetReturnNoExpand));
  EXPECT_TRUE(containsTerminatingRuntimeCall(
      *HugeOffsetZeroSizeMir, RuntimeFunctions.SetRevertNoExpand));
  EXPECT_TRUE(containsTerminatingRuntimeCall(
      *ZeroRevertMir, RuntimeFunctions.SetRevertNoExpand));
}
#endif

} // namespace
