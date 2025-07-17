// Copyright (C) 2021-2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "evm/interpreter.h"
#include "evmc/instructions.h"

#define EVM_STACK_CHECK(FramePtr, N)                                           \
  if ((FramePtr)->stackHeight() < (N)) {                                       \
    throw common::getError(common::ErrorCode::UnexpectedNumArgs);              \
  }

namespace {
static intx::uint256 bigEndianToUInt256(const uint8_t *Bytes, size_t NumBytes) {
  intx::uint256 Value = 0;
  for (size_t I = 0; I < 32 && I < NumBytes; ++I) {
    Value = (Value << 8) | Bytes[I];
  }
  return Value;
}

static int cmpInt256(const intx::uint256 &A, const intx::uint256 &B) {
  intx::uint256 SignA = (A >> 255) & 1;
  intx::uint256 SignB = (B >> 255) & 1;

  if (SignA != SignB) {
    return SignA ? -1 : 1;
  }

  if (A < B)
    return -1;
  if (A > B)
    return 1;
  return 0;
}

static uint64_t uint256ToUint64(const intx::uint256 &Value) {
  return static_cast<uint64_t>(Value & 0xFFFFFFFFFFFFFFFFULL);
}

static intx::uint256 signedDiv(const intx::uint256 &A, const intx::uint256 &B) {
  if (B == 0) {
    return intx::uint256(0);
  }

  intx::uint256 SignA = (A >> 255) & 1;
  intx::uint256 SignB = (B >> 255) & 1;

  intx::uint256 AbsA = SignA ? (~A + 1) : A;
  intx::uint256 AbsB = SignB ? (~B + 1) : B;

  intx::uint256 Result = AbsA / AbsB;

  if (SignA != SignB) {
    Result = ~Result + 1;
  }

  return Result;
}

static intx::uint256 signedMod(const intx::uint256 &A, const intx::uint256 &B) {
  if (B == 0) {
    return intx::uint256(0);
  }

  intx::uint256 SignA = (A >> 255) & 1;
  intx::uint256 SignB = (B >> 255) & 1;

  intx::uint256 AbsA = SignA ? (~A + 1) : A;
  intx::uint256 AbsB = SignB ? (~B + 1) : B;

  intx::uint256 Result = AbsA % AbsB;

  if (SignA) {
    Result = ~Result + 1;
  }

  return Result;
}

static intx::uint256 quickPow(intx::uint256 Base, intx::uint256 Exp) {
  intx::uint256 Result = 1;
  while (Exp > 0) {
    if (Exp & 1) {
      Result *= Base;
    }
    Base *= Base;
    Exp >>= 1;
  }
  return Result;
}

} // namespace

using namespace zen;
using namespace zen::evm;
using namespace zen::runtime;

EVMFrame *InterpreterExecContext::allocFrame() {
  FrameStack.emplace_back();

  EVMFrame &Frame = FrameStack.back();
  Frame.GasLeft = 0;

  return &Frame;
}

// We only need to free the last frame (top of the stack),
// since EVM's control flow is purely stack-based.
void InterpreterExecContext::freeBackFrame() {
  if (FrameStack.empty())
    return;

  FrameStack.pop_back();

  if (FrameStack.empty()) {
    return;
  }
}

void BaseInterpreter::interpret() {
  Context.allocFrame();
  EVMFrame *Frame = Context.getCurFrame();

  const EVMModule *Mod = Context.getModule();

  size_t CodeSize = Mod->CodeSize;
  uint8_t *Code = Mod->Code;

  while (Frame->Pc < CodeSize) {
    uint8_t OpcodeByte = Code[Frame->Pc];
    evmc_opcode Op = static_cast<evmc_opcode>(OpcodeByte);

    switch (Op) {
    case evmc_opcode::OP_STOP:
      Context.freeBackFrame();
      Frame = Context.getCurFrame();
      if (!Frame) {
        return;
      }
      continue;

    case evmc_opcode::OP_ADD: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 C = A + B;
      Frame->push(C);
      break;
    }

    case evmc_opcode::OP_SUB: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 Res = A - B;
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_MUL: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 Res = A * B;
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_DIV: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 Q = (B == 0) ? intx::uint256(0) : A / B;
      Frame->push(Q);
      break;
    }

    case evmc_opcode::OP_MOD: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 R = (B == 0) ? intx::uint256(0) : A % B;
      Frame->push(R);
      break;
    }

    case evmc_opcode::OP_AND: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 Res = A & B;
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_EQ: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 Res = (A == B) ? intx::uint256(1) : intx::uint256(0);
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_ISZERO: {
      EVM_STACK_CHECK(Frame, 1);
      intx::uint256 V = Frame->pop();
      intx::uint256 Res = (V == 0) ? intx::uint256(1) : intx::uint256(0);
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_LT: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 Res = (A < B) ? intx::uint256(1) : intx::uint256(0);
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_GT: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 Res = (A > B) ? intx::uint256(1) : intx::uint256(0);
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_SLT: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 Res =
          (cmpInt256(A, B) < 0) ? intx::uint256(1) : intx::uint256(0);
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_SGT: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 Res =
          (cmpInt256(A, B) > 0) ? intx::uint256(1) : intx::uint256(0);
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_ADDMOD: {
      EVM_STACK_CHECK(Frame, 3);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 C = Frame->pop();
      intx::uint256 Res = (C == 0) ? intx::uint256(0) : (A + B) % C;
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_MULMOD: {
      EVM_STACK_CHECK(Frame, 3);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 C = Frame->pop();
      intx::uint256 Res = (C == 0) ? intx::uint256(0) : ((A % C) * (B % C)) % C;
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_EXP: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 Res = quickPow(A, B);
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_SDIV: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 Res = signedDiv(A, B);
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_SMOD: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 Res = signedMod(A, B);
      Frame->push(Res);
      break;
    }
    case evmc_opcode::OP_SIGNEXTEND: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 I = Frame->pop();
      intx::uint256 V = Frame->pop();

      intx::uint256 Res = V;
      if (I < 32) {
        // Calculate the sign bit position (the highest bit of the Ith byte,
        // i.e., bit 8*I+7)
        intx::uint256 SignBitPosition = 8 * I + 7;

        // Extract the sign bit
        bool SignBit = (V & (intx::uint256(1) << SignBitPosition)) != 0;

        if (SignBit) {
          // Generate mask: lower I*8 bits are 0, the rest are 1
          intx::uint256 Mask = (intx::uint256(1) << SignBitPosition) - 1;
          // Apply mask: extend the sign bit to higher bits
          Res |= ~Mask;
        }
        // If the sign bit is 0, no processing is needed, keep the original
        // value unchanged
      }
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_OR: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 Res = A | B;
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_XOR: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 A = Frame->pop();
      intx::uint256 B = Frame->pop();
      intx::uint256 Res = A ^ B;
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_NOT: {
      EVM_STACK_CHECK(Frame, 1);
      intx::uint256 V = Frame->pop();
      intx::uint256 Res = ~V;
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_BYTE: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 I = Frame->pop();
      intx::uint256 Val = Frame->pop();

      intx::uint256 Res = 0;
      if (I < 32) {
        uint8_t ByteVal = static_cast<uint8_t>((Val >> (8 * (31 - I))) & 0xFF);
        Res = intx::uint256(ByteVal);
      }
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_SHL: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 Shift = Frame->pop();
      intx::uint256 Value = Frame->pop();

      intx::uint256 Res = 0;
      if (Shift < 256) {
        Res = Value << Shift;
      }
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_SHR: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 Shift = Frame->pop();
      intx::uint256 Value = Frame->pop();

      intx::uint256 Res = 0;
      if (Shift < 256) {
        Res = Value >> Shift;
      }
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_SAR: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 Shift = Frame->pop();
      intx::uint256 Value = Frame->pop();

      intx::uint256 Res = 0;
      if (Shift < 256) {
        intx::uint256 IsNegative = (Value >> 255) & 1;
        Res = Value >> Shift;

        if (IsNegative && Shift > 0) {
          intx::uint256 Mask = (intx::uint256(1) << (256 - Shift)) - 1;
          Mask = ~Mask;
          Res |= Mask;
        }
      } else {
        intx::uint256 IsNegative = (Value >> 255) & 1;
        Res = IsNegative ? intx::uint256(-1) : intx::uint256(0);
      }
      Frame->push(Res);
      break;
    }

    case evmc_opcode::OP_MSTORE: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 OffsetVal = Frame->pop();
      intx::uint256 Value = Frame->pop();

      uint64_t Offset = uint256ToUint64(OffsetVal);
      if (Offset > UINT32_MAX) {
        throw common::getError(common::ErrorCode::IntegerOverflow);
      }

      uint64_t ReqSize = Offset + 32;
      if (ReqSize > Frame->Memory.size()) {
        Frame->Memory.resize(ReqSize, 0);
      }
      
      uint8_t ValueBytes[32];
      intx::be::store(ValueBytes, Value);
      std::memcpy(Frame->Memory.data() + Offset, ValueBytes, 32);
      break;
    }

    case evmc_opcode::OP_MLOAD: {
      EVM_STACK_CHECK(Frame, 1);
      intx::uint256 OffsetVal = Frame->pop();
      uint64_t Offset = uint256ToUint64(OffsetVal);

      if (Offset > UINT32_MAX) {
        throw common::getError(common::ErrorCode::IntegerOverflow);
      }

      uint64_t ReqSize = Offset + 32;
      if (ReqSize > Frame->Memory.size()) {
        Frame->Memory.resize(ReqSize, 0);
      }

      uint8_t ValueBytes[32];
      std::memcpy(ValueBytes, Frame->Memory.data() + Offset, 32);

      intx::uint256 Value = intx::be::load<intx::uint256>(ValueBytes);
      Frame->push(Value);
      break;
    }

    case evmc_opcode::OP_RETURN: {
      EVM_STACK_CHECK(Frame, 2);
      intx::uint256 OffsetVal = Frame->pop();
      intx::uint256 SizeVal = Frame->pop();
      uint64_t Offset = uint256ToUint64(OffsetVal);
      uint64_t Size = uint256ToUint64(SizeVal);

      if (Offset > UINT32_MAX || Size > UINT32_MAX) {
        throw common::getError(common::ErrorCode::IntegerOverflow);
      }

      uint64_t ReqSize = Offset + Size;
      if (ReqSize > Frame->Memory.size()) {
        Frame->Memory.resize(ReqSize, 0);
      }

      std::vector<uint8_t> ReturnData(Frame->Memory.begin() + Offset,
                                      Frame->Memory.begin() + Offset + Size);
      Context.setReturnData(std::move(ReturnData));

      Context.freeBackFrame();
      Frame = Context.getCurFrame();
      if (!Frame) {
        return;
      }
      break;
    }

    case evmc_opcode::OP_POP: {
      EVM_STACK_CHECK(Frame, 1);
      Frame->pop();
      break;
    }

    default:
      if (OpcodeByte >= 0x60 && OpcodeByte <= 0x7F) {
        // PUSH1 ~ PUSH32
        uint32_t NumBytes = OpcodeByte - 0x60 + 1;
        if (Frame->Pc + NumBytes >= CodeSize) {
          throw common::getError(common::ErrorCode::UnexpectedEnd);
        }
        intx::uint256 Val = bigEndianToUInt256(Code + Frame->Pc + 1, NumBytes);
        Frame->push(Val);
        Frame->Pc += NumBytes;
        break;
      } else if (OpcodeByte >= 0x80 && OpcodeByte <= 0x8F) {
        // DUP1 ~ DUP16
        uint32_t N = OpcodeByte - 0x80 + 1;
        if (Frame->stackHeight() < N) {
          throw common::getError(common::ErrorCode::UnexpectedNumArgs);
        }
        intx::uint256 V = Frame->peek(N - 1);
        Frame->push(V);
        break;
      } else if (OpcodeByte >= 0x90 && OpcodeByte <= 0x9F) {
        // SWAP1 ~ SWAP16
        uint32_t N = OpcodeByte - 0x90 + 1;
        if (Frame->stackHeight() < N + 1) {
          throw common::getError(common::ErrorCode::UnexpectedNumArgs);
        }
        intx::uint256 &Top = Frame->peek(0);
        intx::uint256 &Nth = Frame->peek(N);
        std::swap(Top, Nth);
        break;
      } else {
        throw common::getError(common::ErrorCode::UnsupportedOpcode);
      }
    }

    Frame->Pc++;
  }
}
