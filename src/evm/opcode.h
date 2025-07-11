// Copyright (C) 2021-2023 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef ZEN_EVM_OPCODE_H
#define ZEN_EVM_OPCODE_H

#include <cstdint>

namespace zen::evm {

// EVM 指令枚举（仅列出核心指令，后续可补充 TODO）
enum class Opcode : uint8_t {
  // 0x0 系
  STOP        = 0x00,
  ADD         = 0x01,
  MUL         = 0x02,
  SUB         = 0x03,
  DIV         = 0x04,
  SDIV        = 0x05,
  MOD         = 0x06,
  SMOD        = 0x07,
  ADDMOD      = 0x08,
  MULMOD      = 0x09,
  EXP         = 0x0A,
  SIGNEXTEND  = 0x0B,

  // 0x10 系
  LT          = 0x10,
  GT          = 0x11,
  SLT         = 0x12,
  SGT         = 0x13,
  EQ          = 0x14,
  ISZERO      = 0x15,
  AND         = 0x16,
  OR          = 0x17,
  XOR         = 0x18,
  NOT         = 0x19,
  BYTE        = 0x1A,

  // 0x30 系（环境信息，示例）
  ADDRESS     = 0x30,
  BALANCE     = 0x31,
  ORIGIN      = 0x32,
  CALLER      = 0x33,
  CALLVALUE   = 0x34,
  CALLDATALOAD= 0x35,
  // ... TODO: 继续补充其余指令

  // PUSH 系列占用 0x60~0x7F，单独处理
  POP         = 0x50,
  MLOAD       = 0x51,
  MSTORE      = 0x52,
  RETURN      = 0xF3,

  // PUSH 系列：仅列出首尾，以便后续范围判断
  PUSH1       = 0x60,
  PUSH32      = 0x7F,

  // DUP 系列
  DUP1        = 0x80,
  DUP16       = 0x8F,

  // SWAP 系列
  SWAP1       = 0x90,
  SWAP16      = 0x9F,
};

} // namespace zen::evm

#endif // ZEN_EVM_OPCODE_H 