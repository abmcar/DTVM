// Copyright (C) 2021-2023 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "evm/interpreter.h"
#include "evm/opcode.h"

#include "common/errors.h"
#include <cstring>
#include <array>
#include <utility>
#include "runtime/instance.h"

namespace {
// 简易 256 位加法（小端字节数组），忽略溢出进位
static zen::evm::UInt256 addUInt256(const zen::evm::UInt256 &A,
                                    const zen::evm::UInt256 &B) {
  zen::evm::UInt256 Res{};
  uint16_t Carry = 0;
  for (size_t i = 0; i < 32; ++i) {
    uint16_t Sum = static_cast<uint16_t>(A.Bytes[i]) +
                   static_cast<uint16_t>(B.Bytes[i]) + Carry;
    Res.Bytes[i] = static_cast<uint8_t>(Sum & 0xFF);
    Carry = Sum >> 8;
  }
  // 溢出进位被丢弃（EVM 语义是 mod 2^256）
  return Res;
}
} // namespace

using namespace zen;
using namespace zen::evm;
using namespace zen::runtime;

/* =============================
 * InterpreterExecContext 实现
 * ============================= */

EVMFrame *InterpreterExecContext::allocFrame(FunctionInstance *FuncInst) {
  auto *Frame = new EVMFrame();
  Frame->FuncInst = FuncInst;

  // 从 FunctionInstance 获取 EVM 字节码入口与大小
  if (FuncInst) {
    Frame->Pc = FuncInst->CodePtr;
    Frame->CodeEnd = FuncInst->CodePtr + FuncInst->CodeSize;
  }

  Frame->GasLeft = 0; // TODO: 依据上层调用指定 Gas

  Frame->PrevFrame = CurFrame;

  setCurFrame(Frame);
  return Frame;
}

void InterpreterExecContext::freeFrame(EVMFrame *Frame) {
  if (!Frame) return;
  setCurFrame(Frame->PrevFrame);
  delete Frame;
}

/* =============================
 * BaseInterpreter 实现
 * ============================= */

void BaseInterpreter::interpret() {
  EVMFrame *Frame = Context.getCurFrame();
  ZEN_ASSERT(Frame && "Interpreter requires a valid initial frame");

  // 主解释循环
  while (true) {
    const uint8_t *CodeEnd = Frame->CodeEnd;
    const uint8_t *&PcRef = Frame->Pc; // 引用当前帧的 PC

    if (PcRef >= CodeEnd) {
      // 正常执行到字节码末尾，相当于 STOP
      Context.freeFrame(Frame);
      if (Context.getCurFrame() == nullptr) {
        return;
      }
      Frame = Context.getCurFrame();
      continue;
    }

    Opcode Op = static_cast<Opcode>(*PcRef++);

    switch (Op) {
    case Opcode::STOP: {
      // EVM STOP - 结束当前调用
      Context.freeFrame(Frame);
      if (Context.getCurFrame() == nullptr) {
        return; // 最外层结束
      }
      Frame = Context.getCurFrame();
      continue; // 继续解释上层帧
    }

    case Opcode::ADD: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      zen::evm::UInt256 B = Frame->pop();
      zen::evm::UInt256 A = Frame->pop();
      zen::evm::UInt256 C = addUInt256(A, B);
      Frame->push(C);
      break;
    }

    case Opcode::SUB: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      zen::evm::UInt256 B = Frame->pop();
      zen::evm::UInt256 A = Frame->pop();
      // A - B (mod 2^256)
      zen::evm::UInt256 Res{};
      int16_t Borrow = 0;
      for (size_t i = 0; i < 32; ++i) {
        int16_t Diff = static_cast<int16_t>(A.Bytes[i]) -
                       static_cast<int16_t>(B.Bytes[i]) - Borrow;
        if (Diff < 0) {
          Diff += 256;
          Borrow = 1;
        } else {
          Borrow = 0;
        }
        Res.Bytes[i] = static_cast<uint8_t>(Diff & 0xFF);
      }
      Frame->push(Res);
      break;
    }

    case Opcode::MUL: {
      // 极简实现：仅处理低 128 位，或用库。暂简单按字节乘法 (naive 256-bit)
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      zen::evm::UInt256 B = Frame->pop();
      zen::evm::UInt256 A = Frame->pop();
      zen::evm::UInt256 Res{};
      // 基于 16 进制乘法 (学校小学算法)
      uint16_t Temp[64] = {0}; // 512-bit 中间结果，低字节在索引低
      for (size_t i = 0; i < 32; ++i) {
        for (size_t j = 0; j < 32; ++j) {
          size_t k = i + j;
          uint16_t P = static_cast<uint16_t>(A.Bytes[i]) * static_cast<uint16_t>(B.Bytes[j]);
          uint16_t Carry = P;
          size_t idx = k;
          while (Carry) {
            uint16_t Sum = Temp[idx] + Carry;
            Temp[idx] = Sum & 0xFF;
            Carry = Sum >> 8;
            ++idx;
          }
        }
      }
      // 模 2^256，只取前 32 字节
      for (size_t i = 0; i < 32; ++i) {
        Res.Bytes[i] = static_cast<uint8_t>(Temp[i]);
      }
      Frame->push(Res);
      break;
    }

    case Opcode::POP: {
      if (Frame->stackHeight() < 1) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      (void)Frame->pop();
      break;
    }

    case Opcode::MSTORE: {
      // 期望栈顶顺序：offset, value
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      UInt256 OffsetVal = Frame->pop();
      UInt256 Value = Frame->pop();
      // 仅使用低 64 位作为偏移
      uint64_t Offset = 0;
      for (int i = 7; i >= 0; --i) {
        Offset = (Offset << 8) | OffsetVal.Bytes[i];
      }
      size_t Off = static_cast<size_t>(Offset);
      size_t NeedSize = Off + 32;
      if (Frame->Memory.size() < NeedSize) {
        Frame->Memory.resize(NeedSize, 0);
      }
      // 将 Value (小端) 转为大端写入
      for (size_t i = 0; i < 32; ++i) {
        Frame->Memory[Off + i] = Value.Bytes[31 - i];
      }
      break;
    }

    case Opcode::RETURN: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      UInt256 OffsetVal = Frame->pop();
      UInt256 SizeVal = Frame->pop();
      uint64_t Offset = 0;
      uint64_t Size = 0;
      for (int i = 7; i >= 0; --i) {
        Offset = (Offset << 8) | OffsetVal.Bytes[i];
        Size = (Size << 8) | SizeVal.Bytes[i];
      }
      size_t Off = static_cast<size_t>(Offset);
      size_t Len = static_cast<size_t>(Size);
      if (Off + Len > Frame->Memory.size()) {
        // 超界视为 0 填充
        Frame->Memory.resize(Off + Len, 0);
      }
      std::vector<uint8_t> Ret(Frame->Memory.begin() + Off, Frame->Memory.begin() + Off + Len);
      Context.setReturnData(std::move(Ret));

      // 与 STOP 类似：结束当前调用
      Context.freeFrame(Frame);
      if (Context.getCurFrame() == nullptr) {
        return; // 顶层返回
      }
      Frame = Context.getCurFrame();
      continue;
    }

    default: {
      uint8_t RawOp = static_cast<uint8_t>(Op);

      // PUSH1 ~ PUSH32 处理
      if (RawOp >= 0x60 && RawOp <= 0x7F) {
        uint8_t NumBytes = RawOp - 0x5F; // 1..32
        if (PcRef + NumBytes > CodeEnd) {
          throw common::getError(common::ErrorCode::UnexpectedEnd);
        }
        zen::evm::UInt256 Val{};
        // 读取大端立即数，转成小端字节存储
        for (uint8_t i = 0; i < NumBytes; ++i) {
          Val.Bytes[i] = *(PcRef + NumBytes - 1 - i);
        }
        PcRef += NumBytes;
        Frame->push(Val);
        break;
      }

      // DUP1~DUP16
      if (RawOp >= 0x80 && RawOp <= 0x8F) {
        uint8_t N = RawOp - 0x7F; // 1..16
        if (Frame->stackHeight() < N) {
          throw common::getError(common::ErrorCode::UnexpectedNumArgs);
        }
        zen::evm::UInt256 V = Frame->peek(N - 1);
        Frame->push(V);
        break;
      }

      // SWAP1~SWAP16
      if (RawOp >= 0x90 && RawOp <= 0x9F) {
        uint8_t N = RawOp - 0x8F; // 1..16
        if (Frame->stackHeight() <= N) {
          throw common::getError(common::ErrorCode::UnexpectedNumArgs);
        }
        zen::evm::UInt256 &Top = Frame->peek(0);
        zen::evm::UInt256 &Nth = Frame->peek(N);
        std::swap(Top, Nth);
        break;
      }

      // Fallback：未识别指令
      throw common::getError(common::ErrorCode::UnsupportedOpcode);
    }
    }
  }
} 