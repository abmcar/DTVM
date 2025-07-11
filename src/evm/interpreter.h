// Copyright (C) 2021-2023 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef ZEN_EVM_INTERPRETER_H
#define ZEN_EVM_INTERPRETER_H

#include "common/defines.h"
#include "runtime/object.h"
#include "runtime/destroyer.h"
#include "utils/logging.h"

#include <cstdint>
#include <vector>
#include <array>
#include <memory>

namespace zen {

namespace runtime {
struct FunctionInstance;
class Instance;
class Runtime;
} // namespace runtime

namespace evm {

// 简易 256 位字类型表示（一律小端存储）
struct UInt256 {
  std::array<uint8_t, 32> Bytes{};
};

// EVM 控制块类型（用于处理 JUMPDEST / CALL / RETURN 等） -- 预留
struct ControlBlock {
  const uint8_t *TargetPc{}; // 目标跳转地址
  size_t StackHeight{};      // 进入块时的栈高
  // TODO: 继续补充所需字段
};

// 解释器帧：对应一次 EVM CALL / CREATE / DELEGATECALL
struct EVMFrame {
  runtime::FunctionInstance *FuncInst{}; // 关联的合约代码实例
  const uint8_t *Pc{};                   // 程序计数器
  const uint8_t *CodeEnd{};               // 字节码末尾（专用于解释循环）
  static constexpr size_t MAX_STACK = 1024; // EVM 规范所限

  // ------- 数据栈 -------
  std::array<UInt256, MAX_STACK> Stack{}; // 固定 1024 深度
  size_t Sp{0};                           // 栈顶指针（下一可写位置）

  // ------- 内存 / 控制 -------
  std::vector<uint8_t> Memory;            // 合约内存（按字节寻址，32 字节对齐留给指令处理）
  std::vector<ControlBlock> CtrlStack;    // 控制块栈（目前保留，EVM 跳转语义未来用不到可移除）

  // ------- Gas 与调用链 -------
  uint64_t GasLeft{0};                    // 剩余 Gas
  EVMFrame *PrevFrame{};                  // 调用者帧

  // ===== 栈操作辅助 =====
  inline void push(const UInt256 &V) {
    ZEN_ASSERT(Sp < MAX_STACK && "EVM data stack overflow");
    Stack[Sp++] = V;
  }

  inline UInt256 pop() {
    ZEN_ASSERT(Sp > 0 && "EVM data stack underflow");
    return Stack[--Sp];
  }

  inline UInt256 &peek(size_t Index = 0) {
    ZEN_ASSERT(Index < Sp && "peek out of range");
    return Stack[Sp - 1 - Index];
  }

  inline size_t stackHeight() const { return Sp; }

  // TODO: Memory 扩容、Gas 扣减等
};

class InterpreterStack : public runtime::RuntimeObject<InterpreterStack> {
  friend class runtime::RuntimeObjectDestroyer;

private:
  InterpreterStack(runtime::Runtime &RT) : runtime::RuntimeObject<InterpreterStack>(RT) {}

public:
  static runtime::RuntimeObjectUniquePtr<InterpreterStack>
  newInterpreterStack(runtime::Runtime &RT) {
    // EVM stack/heap 用 C++ 容器即可，不需要手动分配连续内存，这里返回空对象即可
    return runtime::RuntimeObjectUniquePtr<InterpreterStack>(new InterpreterStack(RT));
  }
};

class InterpreterExecContext {
private:
  runtime::Instance *ModInst{};
  InterpreterStack *StackMgr{};
  EVMFrame *CurFrame{};

public:
  InterpreterExecContext(runtime::Instance *Inst, InterpreterStack *Stack)
      : ModInst(Inst), StackMgr(Stack) {}

  // 根据被调函数实例创建新帧
  EVMFrame *allocFrame(runtime::FunctionInstance *FuncInst);
  void freeFrame(EVMFrame *Frame);

  EVMFrame *getCurFrame() { return CurFrame; }
  void setCurFrame(EVMFrame *Frame) { CurFrame = Frame; }

  runtime::Instance *getInstance() { return ModInst; }
  InterpreterStack *getStackMgr() { return StackMgr; }

  // ===== 返回值数据 =====
private:
  std::vector<uint8_t> ReturnData;
public:
  const std::vector<uint8_t> &getReturnData() const { return ReturnData; }
  void setReturnData(std::vector<uint8_t> Data) { ReturnData = std::move(Data); }
};

class BaseInterpreter {
private:
  InterpreterExecContext &Context;

public:
  explicit BaseInterpreter(InterpreterExecContext &Ctx) : Context(Ctx) {}
  // 启动解释执行，直到 Stop / Revert / OutOfGas / 异常
  void interpret();
};

} // namespace evm
} // namespace zen

#endif // ZEN_EVM_INTERPRETER_H 