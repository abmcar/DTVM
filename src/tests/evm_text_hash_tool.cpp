// Copyright (C) 2026 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Position-independence / determinism probe for the EVM multipass JIT.
//
// Compiles each given bytecode (hex file) in this process and prints
//   <label>,<jit_size>,<fnv1a64 of the emitted JIT .text>
// Run the binary twice as separate processes (ASLR active) and compare
// output: identical lines mean the emitted code embeds no process-specific
// absolute addresses and codegen is deterministic for these inputs.
//
// Usage: evmTextHashTool <hex-file> [<hex-file> ...]

#include "action/compiler.h"
#include "evm_test_host.hpp"
#include "runtime/evm_module.h"
#include "zetaengine.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> readHexFile(const std::string &Path) {
  std::ifstream In(Path);
  std::stringstream Ss;
  Ss << In.rdbuf();
  std::string Hex = Ss.str();
  std::string Clean;
  for (char C : Hex) {
    if (std::isxdigit(static_cast<unsigned char>(C))) {
      Clean.push_back(C);
    }
  }
  if (Clean.size() >= 2 && Clean[0] == '0' && (Clean[1] == 'x' || Clean[1] == 'X')) {
    Clean.erase(0, 2);
  }
  std::vector<uint8_t> Bytes;
  for (size_t I = 0; I + 1 < Clean.size(); I += 2) {
    Bytes.push_back(static_cast<uint8_t>(
        std::stoul(Clean.substr(I, 2), nullptr, 16)));
  }
  return Bytes;
}

uint64_t fnv1a64(const uint8_t *Data, size_t Len) {
  uint64_t H = 0xcbf29ce484222325ull;
  for (size_t I = 0; I < Len; ++I) {
    H ^= Data[I];
    H *= 0x100000001b3ull;
  }
  return H;
}

} // namespace

int main(int Argc, char **Argv) {
  using namespace zen;
  using namespace zen::runtime;
  if (Argc < 2) {
    std::fprintf(stderr, "usage: %s <hex-file> [...]\n", Argv[0]);
    return 2;
  }

  RuntimeConfig Config;
  Config.Mode = common::RunMode::MultipassMode;
  Config.EnableEvmGasMetering = true;

  auto MockedHost = std::make_unique<zen::evm::ZenMockedEVMHost>();
  auto RT = Runtime::newEVMRuntime(Config, MockedHost.get());
  if (!RT) {
    std::fprintf(stderr, "runtime creation failed\n");
    return 1;
  }
  MockedHost->setRuntime(RT.get());

  for (int I = 1; I < Argc; ++I) {
    const std::string Path = Argv[I];
    auto Bytecode = readHexFile(Path);
    if (Bytecode.empty()) {
      std::printf("%s,ERROR,empty\n", Path.c_str());
      continue;
    }
    auto ModRet = RT->loadEVMModule(Path, Bytecode.data(), Bytecode.size());
    if (!ModRet) {
      std::printf("%s,ERROR,load\n", Path.c_str());
      continue;
    }
    EVMModule *Mod = *ModRet;
    if (Mod->getJITCode() == nullptr) {
      zen::action::performEVMJITCompile(*Mod);
    }
    if (Mod->getJITCode() == nullptr || Mod->getJITCodeSize() == 0) {
      std::printf("%s,ERROR,nojit\n", Path.c_str());
      continue;
    }
    std::printf("%s,%zu,%016llx\n", Path.c_str(),
                static_cast<size_t>(Mod->getJITCodeSize()),
                static_cast<unsigned long long>(fnv1a64(
                    static_cast<const uint8_t *>(Mod->getJITCode()),
                    Mod->getJITCodeSize())));
  }
  return 0;
}
