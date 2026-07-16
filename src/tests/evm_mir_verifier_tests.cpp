// Copyright (C) 2026 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "compiler/mir/pass/verifier.h"
#include "compiler/mir/pointer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

class SharedDagVisitCounter final : public COMPILER::MVisitor {
public:
  SharedDagVisitCounter(COMPILER::MModule &Module, COMPILER::MFunction &Func,
                        COMPILER::MInstruction &Target)
      : MVisitor(Module, Func), Target(Target) {}

  void
  visitBinaryInstruction(COMPILER::BinaryInstruction &Instruction) override {
    if (&Instruction == &Target) {
      ++TargetVisitCount;
    }
    MVisitor::visitBinaryInstruction(Instruction);
  }

  uint32_t getTargetVisitCount() const { return TargetVisitCount; }

private:
  COMPILER::MInstruction &Target;
  uint32_t TargetVisitCount = 0;
};

size_t countSubstring(const std::string &Text, const std::string &Needle) {
  size_t Count = 0;
  for (size_t Pos = 0; (Pos = Text.find(Needle, Pos)) != std::string::npos;
       Pos += Needle.size()) {
    ++Count;
  }
  return Count;
}

TEST(MIRVerifierTest, SharedDagMemoizationIsVerifierLocalAndResetsPerVerify) {
  COMPILER::CompileContext Ctx;
  COMPILER::MModule Module(Ctx);
  COMPILER::MFunction Func(Ctx, 0);
  Func.setFunctionType(COMPILER::MFunctionType::create(
      Ctx, Ctx.VoidType, llvm::ArrayRef<COMPILER::MType *>()));

  COMPILER::MBasicBlock *BB = Func.createBasicBlock();
  Func.appendBlock(BB);
  const uint32_t FirstVar = Func.createVariable(&Ctx.I32Type)->getVarIdx();
  const uint32_t SecondVar = Func.createVariable(&Ctx.I32Type)->getVarIdx();
  const uint32_t ThirdVar = Func.createVariable(&Ctx.I32Type)->getVarIdx();
  const uint32_t FourthVar = Func.createVariable(&Ctx.I32Type)->getVarIdx();
  COMPILER::MPointerType *I32PtrType =
      COMPILER::MPointerType::create(Ctx, Ctx.I32Type);
  const uint32_t PointerVar = Func.createVariable(I32PtrType)->getVarIdx();

  auto *ConstI32 = Func.createInstruction<COMPILER::ConstantInstruction>(
      false, *BB, &Ctx.I32Type,
      *COMPILER::MConstantInt::get(Ctx, Ctx.I32Type, 1));
  auto *ConstI64 = Func.createInstruction<COMPILER::ConstantInstruction>(
      false, *BB, &Ctx.I64Type,
      *COMPILER::MConstantInt::get(Ctx, Ctx.I64Type, 1));
  auto *InvalidShared = Func.createInstruction<COMPILER::BinaryInstruction>(
      false, *BB, COMPILER::OP_add, &Ctx.I32Type, ConstI32, ConstI64);
  auto *Left = Func.createInstruction<COMPILER::BinaryInstruction>(
      false, *BB, COMPILER::OP_add, &Ctx.I32Type, InvalidShared, ConstI32);
  auto *Right = Func.createInstruction<COMPILER::BinaryInstruction>(
      false, *BB, COMPILER::OP_sub, &Ctx.I32Type, InvalidShared, ConstI32);
  auto *InvalidSharedIndex =
      Func.createInstruction<COMPILER::ConstantInstruction>(
          false, *BB, &Ctx.I32Type,
          *COMPILER::MConstantInt::get(Ctx, Ctx.I64Type, 2));
  auto *Base = Func.createInstruction<COMPILER::DreadInstruction>(
      false, *BB, I32PtrType, PointerVar);
  auto *FirstLoad = Func.createInstruction<COMPILER::LoadInstruction>(
      false, *BB, &Ctx.I32Type, Base, 1, InvalidSharedIndex);
  auto *SecondLoad = Func.createInstruction<COMPILER::LoadInstruction>(
      false, *BB, &Ctx.I32Type, Base, 1, InvalidSharedIndex);
  auto *InvalidSharedStoreIndex =
      Func.createInstruction<COMPILER::ConversionInstruction>(
          false, *BB, COMPILER::OP_inttoptr, &Ctx.I32Type, ConstI64);
  auto *InvalidSharedWasmBase =
      Func.createInstruction<COMPILER::ConversionInstruction>(
          false, *BB, COMPILER::OP_ptrtoint, &Ctx.I32Type, Base);
  Func.createInstruction<COMPILER::DassignInstruction>(true, *BB, &Ctx.VoidType,
                                                       Left, FirstVar);
  Func.createInstruction<COMPILER::DassignInstruction>(true, *BB, &Ctx.VoidType,
                                                       Right, SecondVar);
  Func.createInstruction<COMPILER::DassignInstruction>(true, *BB, &Ctx.VoidType,
                                                       FirstLoad, ThirdVar);
  Func.createInstruction<COMPILER::DassignInstruction>(true, *BB, &Ctx.VoidType,
                                                       SecondLoad, FourthVar);
  Func.createInstruction<COMPILER::StoreInstruction>(
      true, *BB, &Ctx.VoidType, ConstI32, Base, 1, InvalidSharedStoreIndex);
  Func.createInstruction<COMPILER::StoreInstruction>(
      true, *BB, &Ctx.VoidType, ConstI32, Base, 1, InvalidSharedStoreIndex);
  Func.createInstruction<COMPILER::WasmCheckMemoryAccessInstruction>(
      true, *BB, Ctx, InvalidSharedWasmBase, 0, 4, ConstI32);
  Func.createInstruction<COMPILER::WasmCheckMemoryAccessInstruction>(
      true, *BB, Ctx, InvalidSharedWasmBase, 0, 4, ConstI32);
  Func.createInstruction<COMPILER::ReturnInstruction>(true, *BB, &Ctx.VoidType,
                                                      nullptr);

  SharedDagVisitCounter Counter(Module, Func, *InvalidShared);
  Counter.visit();
  EXPECT_EQ(Counter.getTargetVisitCount(), 2U);

  std::string Diagnostics;
  llvm::raw_string_ostream OS(Diagnostics);
  COMPILER::MVerifier Verifier(Module, Func, OS);
  const std::string ExpectedBinaryError =
      "The operands of binary expession must be of the same type";
  const std::string ExpectedAuxiliaryOperandError =
      "The type of constant instruction result must be the same as the "
      "constant type";
  const std::string ExpectedStoreIndexError =
      "The result of inttoptr instruction must be pointer";
  const std::string ExpectedWasmBaseError =
      "The result of ptrtoint instruction must be i64";

  EXPECT_FALSE(Verifier.verify());
  OS.flush();
  EXPECT_EQ(countSubstring(Diagnostics, ExpectedBinaryError), 1U);
  EXPECT_EQ(countSubstring(Diagnostics, ExpectedAuxiliaryOperandError), 1U);
  EXPECT_EQ(countSubstring(Diagnostics, ExpectedStoreIndexError), 1U);
  EXPECT_EQ(countSubstring(Diagnostics, ExpectedWasmBaseError), 1U);

  EXPECT_FALSE(Verifier.verify());
  OS.flush();
  EXPECT_EQ(countSubstring(Diagnostics, ExpectedBinaryError), 2U);
  EXPECT_EQ(countSubstring(Diagnostics, ExpectedAuxiliaryOperandError), 2U);
  EXPECT_EQ(countSubstring(Diagnostics, ExpectedStoreIndexError), 2U);
  EXPECT_EQ(countSubstring(Diagnostics, ExpectedWasmBaseError), 2U);
}

} // namespace
