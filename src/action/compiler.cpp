// Copyright (C) 2021-2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "action/compiler.h"
#include "common/enums.h"

#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
#include <chrono>
#endif

#ifdef ZEN_ENABLE_SINGLEPASS_JIT
#include "singlepass/singlepass.h"
#endif
#ifdef ZEN_ENABLE_MULTIPASS_JIT
#include "compiler/compiler.h"
#ifdef ZEN_ENABLE_EVM
#include "compiler/evm_compiler.h"
#endif // ZEN_ENABLE_EVM
#endif

namespace zen::action {

#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
namespace {
thread_local EVMJITCompileMetrics *ActiveEVMJITCompileMetrics = nullptr;

class EVMJITCompileMetricsScope {
public:
  explicit EVMJITCompileMetricsScope(runtime::EVMModule &Mod)
      : Metrics(ActiveEVMJITCompileMetrics), Mod(Mod) {
    if (!Metrics) {
      return;
    }
    ++Metrics->AttemptCount;
    MeasureWall = Metrics->InFlightDepth++ == 0;
    HadJITCode = Mod.getJITCode() != nullptr;
    if (MeasureWall) {
      Start = std::chrono::steady_clock::now();
    }
  }

  ~EVMJITCompileMetricsScope() {
    if (!Metrics) {
      return;
    }
    if (!HadJITCode && Mod.getJITCode() != nullptr) {
      ++Metrics->SuccessfulInstallCount;
    }
    --Metrics->InFlightDepth;
    if (MeasureWall) {
      const auto Elapsed = std::chrono::steady_clock::now() - Start;
      Metrics->WallTimeNs += static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Elapsed)
              .count());
    }
  }

private:
  EVMJITCompileMetrics *Metrics;
  runtime::EVMModule &Mod;
  std::chrono::steady_clock::time_point Start;
  bool MeasureWall = false;
  bool HadJITCode = false;
};
} // namespace

EVMJITCompileMetrics *
exchangeEVMJITCompileMetrics(EVMJITCompileMetrics *Metrics) {
  auto *Previous = ActiveEVMJITCompileMetrics;
  ActiveEVMJITCompileMetrics = Metrics;
  return Previous;
}
#endif

void performJITCompile(runtime::Module &Mod) {
  switch (Mod.getRuntime()->getConfig().Mode) {
#ifdef ZEN_ENABLE_SINGLEPASS_JIT
  case common::RunMode::SinglepassMode: {
    singlepass::JITCompiler::compile(&Mod);
    break;
  }
#endif
#ifdef ZEN_ENABLE_MULTIPASS_JIT
  case common::RunMode::MultipassMode: {
    if (Mod.getRuntime()->getConfig().EnableMultipassLazy) {
      auto *LCompiler = Mod.newLazyJITCompiler();
      LCompiler->precompile();
    } else {
      COMPILER::EagerJITCompiler ECompiler(&Mod);
      ECompiler.compile();
    }
    break;
  }
#endif
  default:
    break;
  }
}

#ifdef ZEN_ENABLE_EVM
void performEVMJITCompile(runtime::EVMModule &Mod) {
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
  EVMJITCompileMetricsScope MetricsScope(Mod);
#endif
  switch (Mod.getRuntime()->getConfig().Mode) {
#ifdef ZEN_ENABLE_MULTIPASS_JIT
  case common::RunMode::MultipassMode: {
    COMPILER::EagerEVMJITCompiler ECompiler(&Mod);
    ECompiler.compile();
    break;
  }
#endif
  default:
    ZEN_LOG_ERROR("EVMJIT does not support singlepass mode");
    break;
  }
}
#endif // ZEN_ENABLE_EVM

} // namespace zen::action
