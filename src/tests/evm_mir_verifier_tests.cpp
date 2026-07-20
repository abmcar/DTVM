// Copyright (C) 2026 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "compiler/mir/pass/verifier.h"

#include <gtest/gtest.h>

#include <string>

namespace {

size_t countSubstring(const std::string &Text, const std::string &Needle) {
  size_t Count = 0;
  for (size_t Pos = 0; (Pos = Text.find(Needle, Pos)) != std::string::npos;
       Pos += Needle.size()) {
    ++Count;
  }
  return Count;
}

TEST(MIRVerifierTest, MemoizesDagResetsTraversalAndRejectsCycle) {
  COMPILER::CompileContext Ctx;
  COMPILER::MModule Module(Ctx);
  COMPILER::MFunction Func(Ctx, 0);
  Func.setFunctionType(COMPILER::MFunctionType::create(
      Ctx, Ctx.I32Type, llvm::ArrayRef<COMPILER::MType *>()));

  COMPILER::MBasicBlock *BB = Func.createBasicBlock();
  Func.appendBlock(BB);
  auto *ConstI32 = Func.createInstruction<COMPILER::ConstantInstruction>(
      false, *BB, &Ctx.I32Type,
      *COMPILER::MConstantInt::get(Ctx, Ctx.I32Type, 1));
  auto *ConstI64 = Func.createInstruction<COMPILER::ConstantInstruction>(
      false, *BB, &Ctx.I64Type,
      *COMPILER::MConstantInt::get(Ctx, Ctx.I64Type, 1));
  auto *InvalidShared = Func.createInstruction<COMPILER::BinaryInstruction>(
      false, *BB, COMPILER::OP_add, &Ctx.I32Type, ConstI32, ConstI64);
  auto *Root = Func.createInstruction<COMPILER::BinaryInstruction>(
      false, *BB, COMPILER::OP_add, &Ctx.I32Type, InvalidShared, InvalidShared);
  Func.createInstruction<COMPILER::ReturnInstruction>(true, *BB, &Ctx.I32Type,
                                                      Root);

  std::string Diagnostics;
  llvm::raw_string_ostream OS(Diagnostics);
  COMPILER::MVerifier Verifier(Module, Func, OS);
  const std::string ExpectedBinaryError =
      "The operands of binary expession must be of the same type";

  EXPECT_FALSE(Verifier.verify());
  OS.flush();
  EXPECT_EQ(countSubstring(Diagnostics, ExpectedBinaryError), 1U);
  EXPECT_EQ(countSubstring(Diagnostics,
                           "The MIR instruction operand graph must be acyclic"),
            0U);

  Verifier.visit();
  OS.flush();
  EXPECT_EQ(countSubstring(Diagnostics, ExpectedBinaryError), 2U);
  EXPECT_EQ(countSubstring(Diagnostics,
                           "The MIR instruction operand graph must be acyclic"),
            0U);

  Root->setOperand<0>(Root);
  EXPECT_FALSE(Verifier.verify());
  OS.flush();
  EXPECT_EQ(countSubstring(Diagnostics,
                           "The MIR instruction operand graph must be acyclic"),
            1U);
}

} // namespace
