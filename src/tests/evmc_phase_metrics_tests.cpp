// Copyright (C) 2026 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vm/dt_evmc_vm.h"

#include <evmc/evmc.hpp>
#include <evmc/mocked_host.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

using namespace evmc::literals;

namespace {

static_assert(sizeof(dtvm_evmc_hot_metrics) == 192);
static_assert(DTVM_EVMC_PHASE_METRICS_VERSION == 2u);

dtvm_evmc_hot_metrics makeMetricsRequest() {
  dtvm_evmc_hot_metrics Metrics{};
  Metrics.version = DTVM_EVMC_PHASE_METRICS_VERSION;
  Metrics.struct_size = sizeof(Metrics);
  return Metrics;
}

#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
std::vector<uint8_t> buildReturnContract(uint8_t Value, size_t Size = 64) {
  std::vector<uint8_t> Code = {
      0x60, Value, 0x60, 0x00, 0x52, // MSTORE(Value, 0)
      0x60, 0x20,  0x60, 0x00, 0xf3, // RETURN(0, 32)
  };
  Code.resize(Size, 0x5b);
  return Code;
}

std::vector<uint8_t> buildCallContract() {
  std::vector<uint8_t> Code = {
      0x5f, // return size
      0x5f, // return offset
      0x5f, // input size
      0x5f, // input offset
      0x5f, // value
      0x73, // PUSH20 callee
  };
  Code.insert(Code.end(), 19, 0x00);
  Code.push_back(0x01);
  Code.insert(Code.end(), {0x61, 0xff, 0xff, 0xf1, 0x50, 0x00});
  Code.resize(64, 0x5b);
  return Code;
}

class ReentrantHost final : public evmc::MockedHost {
public:
  evmc::Result call(const evmc_message &Msg) noexcept override {
    auto Metrics = makeMetricsRequest();
    SnapshotStatusDuringCall = dtvm_get_evmc_phase_metrics(VM, &Metrics);
    ResetStatusDuringCall = dtvm_reset_evmc_phase_metrics(
        VM, DTVM_EVMC_PHASE_METRICS_VERSION, sizeof(Metrics));

    evmc_message Nested = Msg;
    Nested.kind = EVMC_CALL;
    Nested.depth = std::max(Msg.depth, 1);
    Nested.recipient = InnerAddress;
    Nested.code_address = InnerAddress;
    return evmc::Result{VM->execute(VM, &evmc::Host::get_interface(),
                                    to_context(), EVMC_LATEST_STABLE_REVISION,
                                    &Nested, InnerCode.data(),
                                    InnerCode.size())};
  }

  evmc_vm *VM = nullptr;
  std::vector<uint8_t> InnerCode = buildReturnContract(0x2a);
  evmc::address InnerAddress =
      0x0000000000000000000000000000000000000001_address;
  dtvm_evmc_phase_metrics_status SnapshotStatusDuringCall =
      DTVM_EVMC_PHASE_METRICS_SUCCESS;
  dtvm_evmc_phase_metrics_status ResetStatusDuringCall =
      DTVM_EVMC_PHASE_METRICS_SUCCESS;
};

class BlockingHost final : public evmc::MockedHost {
public:
  evmc::Result call(const evmc_message &) noexcept override {
    {
      std::lock_guard<std::mutex> Guard(Mutex);
      Entered = true;
    }
    Condition.notify_all();
    std::unique_lock<std::mutex> Lock(Mutex);
    Condition.wait(Lock, [this] { return Released; });
    return evmc::Result{call_result};
  }

  void waitUntilEntered() {
    std::unique_lock<std::mutex> Lock(Mutex);
    Condition.wait(Lock, [this] { return Entered; });
  }

  void releaseCall() {
    {
      std::lock_guard<std::mutex> Guard(Mutex);
      Released = true;
    }
    Condition.notify_all();
  }

private:
  std::mutex Mutex;
  std::condition_variable Condition;
  bool Entered = false;
  bool Released = false;
};

evmc_result execute(evmc_vm *VM, evmc::Host &Host,
                    const std::vector<uint8_t> &Code,
                    const evmc::address &Address, int32_t Depth = 0,
                    evmc_call_kind Kind = EVMC_CALL) {
  evmc_message Msg{};
  Msg.kind = Kind;
  Msg.depth = Depth;
  Msg.gas = 100'000'000;
  Msg.recipient = Address;
  Msg.code_address = Address;
  return VM->execute(VM, &evmc::Host::get_interface(), Host.to_context(),
                     EVMC_LATEST_STABLE_REVISION, &Msg, Code.data(),
                     Code.size());
}

void release(evmc_result &Result) {
  if (Result.release) {
    Result.release(&Result);
  }
}

void expectActiveTimeConsistent(const dtvm_evmc_hot_metrics &Metrics) {
  EXPECT_LE(Metrics.jit_active_wall_ns + Metrics.interpreter_active_wall_ns,
            Metrics.top_level_execute_wall_ns);
}
#endif

TEST(EVMCPhaseMetricsTest, EnforcesBuildStateAndSchema) {
  evmc_vm *VM = evmc_create_dtvmapi();
  ASSERT_NE(VM, nullptr);

  auto Metrics = makeMetricsRequest();
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
  EXPECT_EQ(dtvm_reset_evmc_phase_metrics(VM, DTVM_EVMC_PHASE_METRICS_VERSION,
                                          sizeof(Metrics)),
            DTVM_EVMC_PHASE_METRICS_SUCCESS);
  EXPECT_EQ(dtvm_get_evmc_phase_metrics(VM, &Metrics),
            DTVM_EVMC_PHASE_METRICS_SUCCESS);
  EXPECT_EQ(Metrics.top_level_execute_count, 0u);
#else
  EXPECT_EQ(dtvm_reset_evmc_phase_metrics(VM, DTVM_EVMC_PHASE_METRICS_VERSION,
                                          sizeof(Metrics)),
            DTVM_EVMC_PHASE_METRICS_DISABLED);
  EXPECT_EQ(dtvm_get_evmc_phase_metrics(VM, &Metrics),
            DTVM_EVMC_PHASE_METRICS_DISABLED);
#endif

  Metrics.struct_size = 0;
  EXPECT_EQ(dtvm_get_evmc_phase_metrics(VM, &Metrics),
            DTVM_EVMC_PHASE_METRICS_INCOMPATIBLE);
  EXPECT_EQ(dtvm_get_evmc_phase_metrics(nullptr, &Metrics),
            DTVM_EVMC_PHASE_METRICS_INVALID_ARGUMENT);
  VM->destroy(VM);
}

#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
TEST(EVMCPhaseMetricsTest, SnapshotsCacheCompileAndJITMonotonically) {
  evmc_vm *VM = evmc_create_dtvmapi();
  ASSERT_NE(VM, nullptr);
  ASSERT_EQ(VM->set_option(VM, "mode", "multipass"), EVMC_SET_OPTION_SUCCESS);
  ASSERT_EQ(dtvm_reset_evmc_phase_metrics(VM, DTVM_EVMC_PHASE_METRICS_VERSION,
                                          sizeof(dtvm_evmc_hot_metrics)),
            DTVM_EVMC_PHASE_METRICS_SUCCESS);

  evmc::MockedHost Host;
  const auto Code = buildReturnContract(0x2a);
  const auto Address = 0x0000000000000000000000000000000000000010_address;
  for (int Repeat = 0; Repeat < 2; ++Repeat) {
    evmc_result Result = execute(VM, Host, Code, Address);
    ASSERT_EQ(Result.status_code, EVMC_SUCCESS);
    release(Result);
  }

  auto Metrics = makeMetricsRequest();
  ASSERT_EQ(dtvm_get_evmc_phase_metrics(VM, &Metrics),
            DTVM_EVMC_PHASE_METRICS_SUCCESS);
  EXPECT_EQ(Metrics.top_level_execute_count, 2u);
  EXPECT_EQ(Metrics.module_cache_lookup_count, 2u);
  EXPECT_EQ(Metrics.module_cache_hit_count, 1u);
  EXPECT_EQ(Metrics.module_cache_miss_count, 1u);
  EXPECT_EQ(Metrics.module_cache_entry_count, 1u);
  EXPECT_EQ(Metrics.module_cache_peak_entry_count, 1u);
  EXPECT_GE(Metrics.synchronous_jit_compile_attempt_count, 1u);
  EXPECT_GE(Metrics.synchronous_jit_compile_success_count, 1u);
  EXPECT_GT(Metrics.jit_frame_count, 0u);
  EXPECT_GT(Metrics.jit_active_wall_ns, 0u);
  EXPECT_EQ(Metrics.interpreter_frame_count, 0u);
  EXPECT_EQ(Metrics.non_compile_residual_ns,
            Metrics.top_level_execute_wall_ns -
                Metrics.synchronous_jit_compile_wall_ns);
  expectActiveTimeConsistent(Metrics);

  auto Repeated = makeMetricsRequest();
  ASSERT_EQ(dtvm_get_evmc_phase_metrics(VM, &Repeated),
            DTVM_EVMC_PHASE_METRICS_SUCCESS);
  EXPECT_EQ(Repeated.top_level_execute_count, Metrics.top_level_execute_count);
  EXPECT_EQ(Repeated.module_cache_hit_count, Metrics.module_cache_hit_count);
  EXPECT_EQ(Repeated.jit_active_wall_ns, Metrics.jit_active_wall_ns);

  ASSERT_EQ(dtvm_reset_evmc_phase_metrics(VM, DTVM_EVMC_PHASE_METRICS_VERSION,
                                          sizeof(Metrics)),
            DTVM_EVMC_PHASE_METRICS_SUCCESS);
  Metrics = makeMetricsRequest();
  ASSERT_EQ(dtvm_get_evmc_phase_metrics(VM, &Metrics),
            DTVM_EVMC_PHASE_METRICS_SUCCESS);
  EXPECT_EQ(Metrics.top_level_execute_count, 0u);
  EXPECT_EQ(Metrics.module_cache_lookup_count, 0u);
  EXPECT_EQ(Metrics.module_cache_entry_count, 1u);
  EXPECT_EQ(Metrics.module_cache_peak_entry_count, 1u);
  VM->destroy(VM);
}

TEST(EVMCPhaseMetricsTest, ReportsValidationReplacement) {
  evmc_vm *VM = evmc_create_dtvmapi();
  ASSERT_NE(VM, nullptr);
  ASSERT_EQ(dtvm_reset_evmc_phase_metrics(VM, DTVM_EVMC_PHASE_METRICS_VERSION,
                                          sizeof(dtvm_evmc_hot_metrics)),
            DTVM_EVMC_PHASE_METRICS_SUCCESS);

  evmc::MockedHost Host;
  auto First = buildReturnContract(0x2a);
  auto Second = First;
  Second[20] = 0x00;
  const auto Address = 0x0000000000000000000000000000000000000020_address;
  for (const auto *Code : {&First, &Second}) {
    evmc_result Result = execute(VM, Host, *Code, Address);
    ASSERT_EQ(Result.status_code, EVMC_SUCCESS);
    release(Result);
  }

  auto Metrics = makeMetricsRequest();
  ASSERT_EQ(dtvm_get_evmc_phase_metrics(VM, &Metrics),
            DTVM_EVMC_PHASE_METRICS_SUCCESS);
  EXPECT_EQ(Metrics.module_cache_lookup_count, 2u);
  EXPECT_EQ(Metrics.module_cache_hit_count, 0u);
  EXPECT_EQ(Metrics.module_cache_miss_count, 2u);
  EXPECT_EQ(Metrics.module_cache_validation_reject_count, 1u);
  EXPECT_EQ(Metrics.module_cache_eviction_count, 1u);
  EXPECT_EQ(Metrics.module_cache_entry_count, 1u);
  VM->destroy(VM);
}

TEST(EVMCPhaseMetricsTest, RejectsSnapshotAndResetWhileExecuting) {
  evmc_vm *VM = evmc_create_dtvmapi();
  ASSERT_NE(VM, nullptr);
  ReentrantHost Host;
  Host.VM = VM;

  const auto Code = buildCallContract();
  const auto Address = 0x0000000000000000000000000000000000000030_address;
  evmc_result Result = execute(VM, Host, Code, Address);
  ASSERT_EQ(Result.status_code, EVMC_SUCCESS);
  release(Result);

  EXPECT_EQ(Host.SnapshotStatusDuringCall, DTVM_EVMC_PHASE_METRICS_BUSY);
  EXPECT_EQ(Host.ResetStatusDuringCall, DTVM_EVMC_PHASE_METRICS_BUSY);
  VM->destroy(VM);
}

TEST(EVMCPhaseMetricsTest, SerializesSnapshotAndResetAgainstExecuteStart) {
  evmc_vm *VM = evmc_create_dtvmapi();
  ASSERT_NE(VM, nullptr);
  BlockingHost Host;

  const auto Code = buildCallContract();
  const auto Address = 0x0000000000000000000000000000000000000031_address;
  std::atomic<int> ExecuteStatus{EVMC_INTERNAL_ERROR};
  std::thread Worker([&] {
    evmc_result Result = execute(VM, Host, Code, Address);
    ExecuteStatus.store(Result.status_code, std::memory_order_release);
    release(Result);
  });

  Host.waitUntilEntered();
  auto Metrics = makeMetricsRequest();
  EXPECT_EQ(dtvm_get_evmc_phase_metrics(VM, &Metrics),
            DTVM_EVMC_PHASE_METRICS_BUSY);
  EXPECT_EQ(dtvm_reset_evmc_phase_metrics(VM, DTVM_EVMC_PHASE_METRICS_VERSION,
                                          sizeof(Metrics)),
            DTVM_EVMC_PHASE_METRICS_BUSY);
  Host.releaseCall();
  Worker.join();
  EXPECT_EQ(ExecuteStatus.load(std::memory_order_acquire), EVMC_SUCCESS);

  Metrics = makeMetricsRequest();
  EXPECT_EQ(dtvm_get_evmc_phase_metrics(VM, &Metrics),
            DTVM_EVMC_PHASE_METRICS_SUCCESS);
  EXPECT_EQ(Metrics.top_level_execute_count, 1u);
  VM->destroy(VM);
}

TEST(EVMCPhaseMetricsTest, SeparatesFallbackReasonsAndActiveTime) {
  evmc_vm *VM = evmc_create_dtvmapi();
  ASSERT_NE(VM, nullptr);
  evmc::MockedHost Host;

  const auto SmallCode = buildReturnContract(0x2a, 10);
  auto Result = execute(VM, Host, SmallCode,
                        0x0000000000000000000000000000000000000040_address);
  ASSERT_EQ(Result.status_code, EVMC_SUCCESS);
  release(Result);

  Result = execute(VM, Host, SmallCode, evmc::address{}, 0, EVMC_CREATE);
  ASSERT_EQ(Result.status_code, EVMC_SUCCESS);
  const evmc::address CreatedAddress = Result.create_address;
  release(Result);

  Result = execute(VM, Host, SmallCode, CreatedAddress, 1);
  ASSERT_EQ(Result.status_code, EVMC_SUCCESS);
  release(Result);

  std::vector<uint8_t> StickyCode(0x6001, 0x00);
  Result = execute(VM, Host, StickyCode,
                   0x0000000000000000000000000000000000000050_address);
  ASSERT_EQ(Result.status_code, EVMC_SUCCESS);
  release(Result);

  auto Metrics = makeMetricsRequest();
  ASSERT_EQ(dtvm_get_evmc_phase_metrics(VM, &Metrics),
            DTVM_EVMC_PHASE_METRICS_SUCCESS);
  EXPECT_EQ(Metrics.small_code_interpreter_fallback_count, 1u);
  EXPECT_EQ(Metrics.create_interpreter_fallback_count, 1u);
  EXPECT_EQ(Metrics.newly_created_interpreter_fallback_count, 1u);
  EXPECT_EQ(Metrics.sticky_interpreter_fallback_count, 1u);
  EXPECT_EQ(Metrics.transient_module_load_count, 1u);
  EXPECT_GT(Metrics.interpreter_frame_count, 0u);
  EXPECT_GT(Metrics.interpreter_active_wall_ns, 0u);
  expectActiveTimeConsistent(Metrics);
  VM->destroy(VM);
}
#endif

} // namespace
