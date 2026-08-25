// Copyright (C) 2026 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Hot-execution A/B micro-bench for the host-call emission change.
//
// Compiles each hex-file contract once (multipass JIT), then times REPS
// individual executions after WARMUP, printing
//   <label>,<jit_size>,<p10_ns>,<median_ns>,<p90_ns>
// Run the same binary built from baseline and candidate trees on identical
// inputs and compare medians.
//
// Usage: evmExecBenchTool <hex-file> [...]

#include "action/compiler.h"
#include "evm_test_host.hpp"
#include "runtime/evm_module.h"
#include "zetaengine.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int WARMUP = 20;
constexpr int REPS = 200;

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
  std::vector<uint8_t> Bytes;
  for (size_t I = 0; I + 1 < Clean.size(); I += 2) {
    Bytes.push_back(
        static_cast<uint8_t>(std::stoul(Clean.substr(I, 2), nullptr, 16)));
  }
  return Bytes;
}

} // namespace

int main(int Argc, char **Argv) {
  using namespace zen;
  using namespace zen::runtime;
  using Clock = std::chrono::steady_clock;
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
    auto ModRet = RT->loadEVMModule(Path, Bytecode.data(), Bytecode.size());
    if (!ModRet) {
      std::printf("%s,ERROR,load,,\n", Path.c_str());
      continue;
    }
    EVMModule *Mod = *ModRet;
    if (Mod->getJITCode() == nullptr) {
      zen::action::performEVMJITCompile(*Mod);
    }
    if (Mod->getJITCode() == nullptr) {
      std::printf("%s,ERROR,nojit,,\n", Path.c_str());
      continue;
    }
    Isolation *Iso = RT->createManagedIsolation();
    if (!Iso) {
      std::printf("%s,ERROR,iso,,\n", Path.c_str());
      continue;
    }
    const uint64_t GasLimit = 0xFFFF'FFFF'FFFFull;

    auto RunOnce = [&]() -> int64_t {
      auto InstRet = Iso->createEVMInstance(*Mod, GasLimit);
      if (!InstRet) {
        return -1;
      }
      EVMInstance *Inst = *InstRet;
      Inst->setRevision(evmc_revision::EVMC_OSAKA);
      evmc_message Msg = {
          .kind = EVMC_CALL,
          .flags = 0u,
          .depth = 0,
          .gas = static_cast<int64_t>(GasLimit),
          .recipient = {},
          .sender = zen::evm::DEFAULT_DEPLOYER_ADDRESS,
          .input_data = nullptr,
          .input_size = 0,
          .value = {},
          .create2_salt = {},
          .code = reinterpret_cast<const uint8_t *>(Mod->Code),
          .code_size = Mod->CodeSize,
      };
      evmc::Result RawResult;
      auto T0 = Clock::now();
      RT->callEVMMain(*Inst, Msg, RawResult);
      auto T1 = Clock::now();
      return std::chrono::duration_cast<std::chrono::nanoseconds>(T1 - T0)
          .count();
    };

    for (int W = 0; W < WARMUP; ++W) {
      (void)RunOnce();
    }
    std::vector<int64_t> Ns;
    Ns.reserve(REPS);
    for (int R = 0; R < REPS; ++R) {
      int64_t V = RunOnce();
      if (V >= 0) {
        Ns.push_back(V);
      }
    }
    std::sort(Ns.begin(), Ns.end());
    if (Ns.empty()) {
      std::printf("%s,ERROR,exec,,\n", Path.c_str());
      continue;
    }
    std::printf("%s,%zu,%lld,%lld,%lld\n", Path.c_str(),
                static_cast<size_t>(Mod->getJITCodeSize()),
                static_cast<long long>(Ns[Ns.size() / 10]),
                static_cast<long long>(Ns[Ns.size() / 2]),
                static_cast<long long>(Ns[(Ns.size() * 9) / 10]));
  }
  return 0;
}
