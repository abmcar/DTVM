// Copyright (C) 2021-2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "evm/interpreter.h"

#include "common/errors.h"
#include "evm/opcode.h"
#include "runtime/instance.h"
#include "uint256_t.h"

namespace {
static std::array<uint8_t, 32> uint256ToBytes(const uint256_t &Value) {
  std::array<uint8_t, 32> Bytes{};
  for (size_t I = 0; I < 32; ++I) {
    Bytes[I] = static_cast<uint8_t>((Value >> (8 * (31 - I))) & 0xFF);
  }
  return Bytes;
}

static uint256_t bytesToUInt256(const std::array<uint8_t, 32> &Bytes) {
  uint256_t Value = 0;
  for (size_t I = 0; I < 32; ++I) {
    Value = (Value << 8) | Bytes[I];
  }
  return Value;
}

static uint256_t bigEndianToUInt256(const uint8_t *Bytes, size_t NumBytes) {
  uint256_t Value = 0;
  for (size_t I = 0; I < 32 && I < NumBytes; ++I) {
    Value = (Value << 8) | Bytes[I];
  }
  return Value;
}

static int cmpInt256(const uint256_t &A, const uint256_t &B) {
  bool SignA = (A >> 255) & 1;
  bool SignB = (B >> 255) & 1;

  if (SignA != SignB) {
    return SignA ? -1 : 1;
  }

  if (A < B)
    return -1;
  if (A > B)
    return 1;
  return 0;
}

static uint64_t uint256ToUint64(const uint256_t &Value) {
  return static_cast<uint64_t>(Value & 0xFFFFFFFFFFFFFFFFULL);
}

static uint256_t signedDiv(const uint256_t &A, const uint256_t &B) {
  if (B == 0) {
    return uint256_t(0);
  }

  bool SignA = (A >> 255) & 1;
  bool SignB = (B >> 255) & 1;

  uint256_t AbsA = SignA ? (~A + 1) : A;
  uint256_t AbsB = SignB ? (~B + 1) : B;

  uint256_t Result = AbsA / AbsB;

  if (SignA != SignB) {
    Result = ~Result + 1;
  }

  return Result;
}

static uint256_t signedMod(const uint256_t &A, const uint256_t &B) {
  if (B == 0) {
    return uint256_t(0);
  }

  bool SignA = (A >> 255) & 1;
  bool SignB = (B >> 255) & 1;

  uint256_t AbsA = SignA ? (~A + 1) : A;
  uint256_t AbsB = SignB ? (~B + 1) : B;

  uint256_t Result = AbsA % AbsB;

  if (SignA) {
    Result = ~Result + 1;
  }

  return Result;
}

static uint256_t quickPow(uint256_t Base, uint256_t Exp) {
  uint256_t Result = 1;
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
  auto *Frame = new EVMFrame();

  Frame->GasLeft = 0;
  Frame->PrevFrame = CurFrame;
  setCurFrame(Frame);
  return Frame;
}

void InterpreterExecContext::freeFrame(EVMFrame *Frame) {
  if (!Frame)
    return;
  setCurFrame(Frame->PrevFrame);
  delete Frame;
}

void BaseInterpreter::interpret() {
  EVMFrame *Frame = Context.getCurFrame();
  ZEN_ASSERT(Frame && "Interpreter requires a valid initial frame");

  while (Frame->Pc < Frame->Bytecode.size()) {
    uint8_t OpcodeByte = Frame->Bytecode[Frame->Pc];
    Opcode Op = static_cast<Opcode>(OpcodeByte);

    switch (Op) {
    case Opcode::STOP:
      Context.freeFrame(Frame);
      if (Context.getCurFrame() == nullptr) {
        return;
      }
      Frame = Context.getCurFrame();
      continue;

    case Opcode::ADD: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t C = A + B;
      Frame->push(C);
      break;
    }

    case Opcode::SUB: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t Res = A - B;
      Frame->push(Res);
      break;
    }

    case Opcode::MUL: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t Res = A * B;
      Frame->push(Res);
      break;
    }

    case Opcode::DIV: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t Q = (B == 0) ? uint256_t(0) : A / B;
      Frame->push(Q);
      break;
    }

    case Opcode::MOD: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t R = (B == 0) ? uint256_t(0) : A % B;
      Frame->push(R);
      break;
    }

    case Opcode::AND: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t Res = A & B;
      Frame->push(Res);
      break;
    }

    case Opcode::EQ: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t Res = (A == B) ? uint256_t(1) : uint256_t(0);
      Frame->push(Res);
      break;
    }

    case Opcode::ISZERO: {
      if (Frame->stackHeight() < 1) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t V = Frame->pop();
      uint256_t Res = (V == 0) ? uint256_t(1) : uint256_t(0);
      Frame->push(Res);
      break;
    }

    case Opcode::LT: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t Res = (A < B) ? uint256_t(1) : uint256_t(0);
      Frame->push(Res);
      break;
    }

    case Opcode::GT: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t Res = (A > B) ? uint256_t(1) : uint256_t(0);
      Frame->push(Res);
      break;
    }

    case Opcode::SLT: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t Res = (cmpInt256(A, B) < 0) ? uint256_t(1) : uint256_t(0);
      Frame->push(Res);
      break;
    }

    case Opcode::SGT: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t Res = (cmpInt256(A, B) > 0) ? uint256_t(1) : uint256_t(0);
      Frame->push(Res);
      break;
    }

    case Opcode::ADDMOD: {
      if (Frame->stackHeight() < 3) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t C = Frame->pop();
      uint256_t Res = (C == 0) ? uint256_t(0) : (A + B) % C;
      Frame->push(Res);
      break;
    }

    case Opcode::MULMOD: {
      if (Frame->stackHeight() < 3) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t C = Frame->pop();
      uint256_t Res = (C == 0) ? uint256_t(0) : ((A % C) * (B % C)) % C;
      Frame->push(Res);
      break;
    }

    case Opcode::EXP: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t Res = quickPow(A, B);
      Frame->push(Res);
      break;
    }

    case Opcode::SDIV: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t Res = signedDiv(A, B);
      Frame->push(Res);
      break;
    }

    case Opcode::SMOD: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t Res = signedMod(A, B);
      Frame->push(Res);
      break;
    }
    case Opcode::SIGNEXTEND: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t I = Frame->pop();
      uint256_t V = Frame->pop();

      uint256_t Res = V;
      if (I < 32) {
        // Calculate the sign bit position (the highest bit of the Ith byte,
        // i.e., bit 8*I+7)
        uint256_t SignBitPosition = 8 * I + 7;

        // Extract the sign bit
        bool SignBit = (V & (uint256_t(1) << SignBitPosition)) != 0;

        if (SignBit) {
          // Generate mask: lower I*8 bits are 0, the rest are 1
          uint256_t Mask = (uint256_t(1) << SignBitPosition) - 1;
          // Apply mask: extend the sign bit to higher bits
          Res |= ~Mask;
        }
        // If the sign bit is 0, no processing is needed, keep the original
        // value unchanged
      }
      Frame->push(Res);
      break;
    }

    case Opcode::OR: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t Res = A | B;
      Frame->push(Res);
      break;
    }

    case Opcode::XOR: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t A = Frame->pop();
      uint256_t B = Frame->pop();
      uint256_t Res = A ^ B;
      Frame->push(Res);
      break;
    }

    case Opcode::NOT: {
      if (Frame->stackHeight() < 1) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t V = Frame->pop();
      uint256_t Res = ~V;
      Frame->push(Res);
      break;
    }

    case Opcode::BYTE: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t I = Frame->pop();
      uint256_t Val = Frame->pop();

      uint256_t Res = 0;
      if (I < 32) {
        uint8_t ByteVal = static_cast<uint8_t>((Val >> (8 * (31 - I))) & 0xFF);
        Res = uint256_t(ByteVal);
      }
      Frame->push(Res);
      break;
    }

    case Opcode::SHL: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t Shift = Frame->pop();
      uint256_t Value = Frame->pop();

      uint256_t Res = 0;
      if (Shift < 256) {
        Res = Value << Shift;
      }
      Frame->push(Res);
      break;
    }

    case Opcode::SHR: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t Shift = Frame->pop();
      uint256_t Value = Frame->pop();

      uint256_t Res = 0;
      if (Shift < 256) {
        Res = Value >> Shift;
      }
      Frame->push(Res);
      break;
    }

    case Opcode::SAR: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t Shift = Frame->pop();
      uint256_t Value = Frame->pop();

      uint256_t Res = 0;
      if (Shift < 256) {
        bool IsNegative = (Value >> 255) & 1;
        Res = Value >> Shift;

        if (IsNegative && Shift > 0) {
          uint256_t Mask = (uint256_t(1) << (256 - Shift)) - 1;
          Mask = ~Mask;
          Res |= Mask;
        }
      } else {
        bool IsNegative = (Value >> 255) & 1;
        Res = IsNegative ? uint256_t(-1) : uint256_t(0);
      }
      Frame->push(Res);
      break;
    }

    case Opcode::MSTORE: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t OffsetVal = Frame->pop();
      uint256_t Value = Frame->pop();

      uint64_t Offset = uint256ToUint64(OffsetVal);
      if (Offset > UINT32_MAX) {
        throw common::getError(common::ErrorCode::IntegerOverflow);
      }

      std::array<uint8_t, 32> ValueBytes = uint256ToBytes(Value);
      uint64_t ReqSize = Offset + 32;
      if (ReqSize > Frame->Memory.size()) {
        Frame->Memory.resize(ReqSize, 0);
      }
      std::memcpy(Frame->Memory.data() + Offset, ValueBytes.data(), 32);
      break;
    }

    case Opcode::MLOAD: {
      if (Frame->stackHeight() < 1) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t OffsetVal = Frame->pop();
      uint64_t Offset = uint256ToUint64(OffsetVal);

      if (Offset > UINT32_MAX) {
        throw common::getError(common::ErrorCode::IntegerOverflow);
      }

      uint64_t ReqSize = Offset + 32;
      if (ReqSize > Frame->Memory.size()) {
        Frame->Memory.resize(ReqSize, 0);
      }

      std::array<uint8_t, 32> ValueBytes{};
      std::memcpy(ValueBytes.data(), Frame->Memory.data() + Offset, 32);

      uint256_t Value = bytesToUInt256(ValueBytes);
      Frame->push(Value);
      break;
    }

    case Opcode::RETURN: {
      if (Frame->stackHeight() < 2) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      uint256_t OffsetVal = Frame->pop();
      uint256_t SizeVal = Frame->pop();
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

      Context.freeFrame(Frame);
      if (Context.getCurFrame() == nullptr) {
        return;
      }
      Frame = Context.getCurFrame();
      break;
    }

    case Opcode::POP: {
      if (Frame->stackHeight() < 1) {
        throw common::getError(common::ErrorCode::UnexpectedNumArgs);
      }
      Frame->pop();
      break;
    }

    default:
      if (OpcodeByte >= 0x60 && OpcodeByte <= 0x7F) {
        // PUSH1 ~ PUSH32
        uint32_t NumBytes = OpcodeByte - 0x60 + 1;
        if (Frame->Pc + NumBytes >= Frame->Bytecode.size()) {
          throw common::getError(common::ErrorCode::UnexpectedEnd);
        }
        uint256_t Val = bigEndianToUInt256(
            Frame->Bytecode.data() + Frame->Pc + 1, NumBytes);
        Frame->push(Val);
        Frame->Pc += NumBytes;
        break;
      } else if (OpcodeByte >= 0x80 && OpcodeByte <= 0x8F) {
        // DUP1 ~ DUP16
        uint32_t N = OpcodeByte - 0x80 + 1;
        if (Frame->stackHeight() < N) {
          throw common::getError(common::ErrorCode::UnexpectedNumArgs);
        }
        uint256_t V = Frame->peek(N - 1);
        Frame->push(V);
        break;
      } else if (OpcodeByte >= 0x90 && OpcodeByte <= 0x9F) {
        // SWAP1 ~ SWAP16
        uint32_t N = OpcodeByte - 0x90 + 1;
        if (Frame->stackHeight() < N + 1) {
          throw common::getError(common::ErrorCode::UnexpectedNumArgs);
        }
        uint256_t &Top = Frame->peek(0);
        uint256_t &Nth = Frame->peek(N);
        std::swap(Top, Nth);
        break;
      } else {
        throw common::getError(common::ErrorCode::UnsupportedOpcode);
      }
    }

    Frame->Pc++;
  }
}