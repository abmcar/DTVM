// Copyright (C) 2021-2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef ZEN_EVM_INTERPRETER_H
#define ZEN_EVM_INTERPRETER_H

#include "common/defines.h"
#include "runtime/destroyer.h"
#include "runtime/object.h"
#include "utils/logging.h"

#include "uint256_t.h"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace zen {

namespace runtime {
struct FunctionInstance;
class Instance;
class Runtime;
} // namespace runtime

namespace evm {

struct EVMFrame {
  static constexpr size_t MAXSTACK = 1024;

  std::array<uint256_t, MAXSTACK> Stack;
  std::vector<uint8_t> Bytecode;
  std::vector<uint8_t> Memory;
  std::map<uint256_t, uint256_t> Storage;

  size_t Sp;
  uint64_t GasLeft;
  uint64_t Pc;
  EVMFrame *PrevFrame;
  uint256_t Value;

  inline void push(const uint256_t &V) {
    ZEN_ASSERT(Sp < MAXSTACK && "EVM data stack overflow");
    Stack[Sp++] = V;
  }

  inline uint256_t pop() {
    ZEN_ASSERT(Sp > 0 && "EVM data stack underflow");
    return Stack[--Sp];
  }

  inline uint256_t &peek(size_t Index = 0) {
    ZEN_ASSERT(Index < Sp && "peek out of range");
    return Stack[Sp - 1 - Index];
  }

  inline size_t stackHeight() const { return Sp; }
};

class InterpreterExecContext {
private:
  runtime::Instance *ModInst;
  EVMFrame *CurFrame;

public:
  InterpreterExecContext(runtime::Instance *Inst)
      : ModInst(Inst), CurFrame(nullptr) {}

  EVMFrame *allocFrame();
  void freeFrame(EVMFrame *Frame);

  EVMFrame *getCurFrame() { return CurFrame; }
  void setCurFrame(EVMFrame *Frame) { CurFrame = Frame; }

  runtime::Instance *getInstance() { return ModInst; }

private:
  std::vector<uint8_t> ReturnData;

public:
  const std::vector<uint8_t> &getReturnData() const { return ReturnData; }
  void setReturnData(std::vector<uint8_t> Data) {
    ReturnData = std::move(Data);
  }
};

class BaseInterpreter {
private:
  InterpreterExecContext &Context;

public:
  explicit BaseInterpreter(InterpreterExecContext &Ctx) : Context(Ctx) {}
  void interpret();
};

} // namespace evm
} // namespace zen

#endif // ZEN_EVM_INTERPRETER_H