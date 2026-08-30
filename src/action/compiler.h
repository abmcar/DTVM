// Copyright (C) 2021-2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef ZEN_ACTION_COMPILER_H
#define ZEN_ACTION_COMPILER_H

#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
#include <cstdint>
#endif

#ifdef ZEN_ENABLE_EVM
#include "runtime/evm_module.h"
#endif // ZEN_ENABLE_EVM
#include "runtime/module.h"

namespace zen::action {

#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
struct EVMJITCompileMetrics {
  uint64_t AttemptCount = 0;
  uint64_t SuccessfulInstallCount = 0;
  uint64_t WallTimeNs = 0;
  uint32_t InFlightDepth = 0;
};

EVMJITCompileMetrics *
exchangeEVMJITCompileMetrics(EVMJITCompileMetrics *Metrics);
#endif

void performJITCompile(runtime::Module &Mod);
#ifdef ZEN_ENABLE_EVM
void performEVMJITCompile(runtime::EVMModule &Mod);
#endif // ZEN_ENABLE_EVM

} // namespace zen::action

#endif // ZEN_ACTION_COMPILER_H
