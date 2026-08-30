// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "dt_evmc_vm.h"
#include "action/compiler.h"
#include "common/enums.h"
#include "common/errors.h"
#include "compiler/evm_frontend/evm_analyzer.h"
#include "evm/evm.h"
#include "evm/interpreter.h"
#include "evm/opcode_handlers.h"
#include "jit_profile.h"
#include "runtime/config.h"
#include "runtime/evm_instance.h"
#include "runtime/isolation.h"
#include "runtime/runtime.h"
#include "wrapped_host.h"
#include <algorithm>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>
#include <evmc/helpers.h>

#include <chrono>
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
#include <atomic>
#include <mutex>
#endif
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef ZEN_ENABLE_VIRTUAL_STACK
#include "utils/virtual_stack.h"
#endif // ZEN_ENABLE_VIRTUAL_STACK

static_assert(sizeof(dtvm_evmc_hot_metrics) == 192,
              "hot metrics schema size must remain versioned");

namespace {

using namespace zen::runtime;
using namespace zen::common;

using zen::vm::CallRecord;
using zen::vm::CallRingBuffer;
using zen::vm::ContractProfile;
using zen::vm::JITCompilePool;
namespace profile = zen::vm::profile;
// RAII helper for temporarily changing runtime configuration
class ScopedConfig {
public:
  ScopedConfig(Runtime *Runtime, const RuntimeConfig &NewConfig)
      : RT(Runtime), PreviousConfig(Runtime->getConfig()) {
    RT->setConfig(NewConfig);
  }

  ~ScopedConfig() { RT->setConfig(PreviousConfig); }

private:
  Runtime *RT;
  RuntimeConfig PreviousConfig;
};

// Forward declaration for InstanceGuard
struct DTVM;

// RAII guard for unloading transient modules that must not be persisted in the
// address-based cache. Destruction order matters: instance guards must run
// before unloading the module they execute.
struct ModuleGuard {
  DTVM *VM;
  EVMModule *Mod;
  bool ShouldUnload;

  ModuleGuard(DTVM *VM, EVMModule *Mod, bool ShouldUnload)
      : VM(VM), Mod(Mod), ShouldUnload(ShouldUnload) {}

  ModuleGuard(const ModuleGuard &) = delete;
  ModuleGuard &operator=(const ModuleGuard &) = delete;

  ~ModuleGuard();

  void release() { ShouldUnload = false; }
};

// RAII helper for host context save/restore (exception safety).
// Ensures host context is always restored on all exit paths, including
// exceptions.
struct HostContextScope {
  ::WrappedHost *ExecHost;
  const evmc_host_interface *PrevInterface;
  evmc_host_context *PrevContext;

  HostContextScope(::WrappedHost *Host, const evmc_host_interface *Interface,
                   evmc_host_context *Context)
      : ExecHost(Host), PrevInterface(Host->getInterface()),
        PrevContext(Host->getContext()) {
    ExecHost->reinitialize(Interface, Context);
  }

  ~HostContextScope() { ExecHost->reinitialize(PrevInterface, PrevContext); }

  // Non-copyable
  HostContextScope(const HostContextScope &) = delete;
  HostContextScope &operator=(const HostContextScope &) = delete;
};

// ---- Address-based module cache types ----

// Removed: MAX_MODULE_CACHE_SIZE is now configurable at runtime
static constexpr size_t CODE_PREFIX_SIZE = 8; // Store first 8 bytes of code

struct CodeAddrRevKey {
  evmc_address Addr;
  evmc_revision Rev;
  zen::runtime::EVMMemorySpecializationProfile MemoryProfile = {};
  uint8_t
      CodePrefix[CODE_PREFIX_SIZE]; // First 8 bytes of code (no hash collision)
  size_t CodeSize = 0;              // Code size for exact match

  CodeAddrRevKey() : Addr{}, Rev{}, MemoryProfile{}, CodeSize(0) {
    std::memset(CodePrefix, 0, CODE_PREFIX_SIZE);
  }
};

struct CodeAddrRevHash {
  size_t operator()(const CodeAddrRevKey &K) const {
    const auto CodegenKey =
        getEVMMemorySpecializationCodegenKey(K.MemoryProfile);
    uint64_t H;
    std::memcpy(&H, K.Addr.bytes + 12, sizeof(H));
    // Mix in code prefix (first 8 bytes) and size for better distribution
    uint64_t PrefixHash;
    std::memcpy(&PrefixHash, K.CodePrefix, sizeof(PrefixHash));
    H ^= PrefixHash;
    H ^= static_cast<uint64_t>(K.CodeSize) * 2654435761ULL;
    return H ^ (static_cast<size_t>(K.Rev) * 2654435761u) ^
           (static_cast<size_t>(CodegenKey.SkipLeadingZeroLimbStores) << 20);
  }
};

struct CodeAddrRevEqual {
  bool operator()(const CodeAddrRevKey &A, const CodeAddrRevKey &B) const {
    // Keep the cache-key dependency explicit: today only the codegen-relevant
    // specialization fields participate in module identity. If additional
    // EVMMemorySpecializationProfile fields start affecting lowering or JIT
    // codegen, update getEVMMemorySpecializationCodegenKey() accordingly.
    const auto ACodegenKey =
        getEVMMemorySpecializationCodegenKey(A.MemoryProfile);
    const auto BCodegenKey =
        getEVMMemorySpecializationCodegenKey(B.MemoryProfile);
    return A.Rev == B.Rev &&
           ACodegenKey.SkipLeadingZeroLimbStores ==
               BCodegenKey.SkipLeadingZeroLimbStores &&
           std::memcmp(A.Addr.bytes, B.Addr.bytes, sizeof(A.Addr.bytes)) == 0 &&
           std::memcmp(A.CodePrefix, B.CodePrefix, CODE_PREFIX_SIZE) == 0 &&
           A.CodeSize == B.CodeSize;
  }
};

zen::runtime::EVMMemorySpecializationProfile
deriveMemorySpecializationProfile(const evmc_message *Msg) {
  return deriveEVMMemorySpecializationProfileFromCallData(
      Msg ? Msg->input_data : nullptr, Msg ? Msg->input_size : 0);
}

/// Validate that the cached module's code matches the provided code.
/// Strict mode performs full-bytecode comparison for hostile/non-standard hosts
/// that may reuse the same code address for different bytecode.
/// Relaxed mode checks head/tail windows (fast path for trusted immutable-code
/// hosts). EIP-170 caps contract code size at 24 KiB.
bool validateCodeMatch(const uint8_t *Code, size_t CodeSize,
                       const EVMModule *Mod, bool StrictValidation) {
  if (CodeSize != Mod->CodeSize)
    return false;
  if (CodeSize == 0)
    return true;
  auto *ModCode = reinterpret_cast<const uint8_t *>(Mod->Code);
  if (StrictValidation) {
    return std::memcmp(Code, ModCode, CodeSize) == 0;
  }
  constexpr size_t kWindow = 256;
  const size_t HeadLen = std::min(CodeSize, kWindow);
  if (std::memcmp(Code, ModCode, HeadLen) != 0)
    return false;
  if (CodeSize > kWindow) {
    const size_t TailLen = std::min(CodeSize, kWindow);
    const size_t TailOffset = CodeSize - TailLen;
    if (std::memcmp(Code + TailOffset, ModCode + TailOffset, TailLen) != 0)
      return false;
  }
  return true;
}

bool parseBoolEnvValue(const char *Value, bool &ParsedValue) {
  if (std::strcmp(Value, "1") == 0 || std::strcmp(Value, "true") == 0 ||
      std::strcmp(Value, "TRUE") == 0) {
    ParsedValue = true;
    return true;
  }
  if (std::strcmp(Value, "0") == 0 || std::strcmp(Value, "false") == 0 ||
      std::strcmp(Value, "FALSE") == 0) {
    ParsedValue = false;
    return true;
  }
  return false;
}

#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
enum class EVMActiveMode : uint8_t { None, JIT, Interpreter };
#endif

// VM interface for DTVM
struct DTVM : evmc_vm {
  DTVM();
  ~DTVM() {
    // Drain the JIT compile thread pool first: wait for all in-flight
    // compilation tasks to finish before unloading modules they reference.
#ifdef ZEN_ENABLE_MULTIPASS_JIT
    CompilePool.reset();
#endif // ZEN_ENABLE_MULTIPASS_JIT
    if (CachedMainInst && Iso) {
      Iso->deleteEVMInstance(CachedMainInst);
      CachedMainInst = nullptr;
    }

    // Clean up cached instance for depth > 0
    if (CacheInsts.size() > 0 && Iso) {
      for (auto It : CacheInsts) {
        Iso->deleteEVMInstance(It);
      }
    }

    // Unload all address-cached modules
    if (RT) {
      for (auto &P : AddrCache) {
        if (!RT->unloadEVMModule(P.second.first)) {
          ZEN_LOG_ERROR("failed to unload EVM module");
        }
      }
    }
    if (Iso) {
      RT->deleteManagedIsolation(Iso);
    }
  }
  RuntimeConfig Config = {.Format = InputFormat::EVM,
                          .Mode = RunMode::MultipassMode,
                          .EnableEvmGasMetering = true};
  std::unique_ptr<Runtime> RT;
  std::unique_ptr<::WrappedHost> ExecHost;
  Isolation *Iso = nullptr;
  // Default to strict (full bytecode equality) for correctness in tests and
  // non-standard hosts. Trusted hosts can opt out via env.
  bool EnableStrictAddrCacheValidation = true;

  // Maximum number of modules in the address-based LRU cache (configurable)
  size_t MaxModuleCacheSize = 4096;

  // ---- Module & instance cache (shared by interpreter and multipass) ----
  // L0: pointer-based inline cache (fastest, 2 integer comparisons)
  const uint8_t *LastCodePtr = nullptr;
  size_t LastCodeSize = 0;
  EVMModule *L0Mod = nullptr;

  // L1: address-based LRU cache (code_address + rev -> module)
  // LRUOrder tracks access recency: front = most recent, back = least recent.
  // AddrCache maps key -> (module, iterator into LRUOrder) for O(1) lookup
  // and O(1) LRU maintenance via std::list::splice.
  using LRUList = std::list<CodeAddrRevKey>;
  LRUList LRUOrder;
  std::unordered_map<CodeAddrRevKey, std::pair<EVMModule *, LRUList::iterator>,
                     CodeAddrRevHash, CodeAddrRevEqual>
      AddrCache;

  uint64_t ModCounter = 0;
  // Cached EVMInstance to avoid alloc/free (~33KB) on every call
  EVMInstance *CachedMainInst = nullptr;
  // Cached InterpreterExecContext for the top-level call (depth == 0).
  std::unique_ptr<zen::evm::InterpreterExecContext> CachedCtx;
  // Per-depth InterpreterExecContext pool for nested calls (depth > 0).
  // Indexed by Msg->depth.  Each entry persists across calls so that the
  // 32 KB EVMFrame stack array inside InterpreterExecContext::FrameStack
  // is allocated *at most once per depth* and reused on subsequent
  // recursive calls.  Without this pool, every nested CALL/CREATE went
  // through std::make_unique<InterpreterExecContext>() + a fresh
  // FrameStack.emplace_back() (32 KB zero-init), which dominated runtime
  // in deeply recursive workloads (e.g. transStorageOK, ~24x slowdown
  // vs evmone baseline).  DTVM is single-threaded per VM instance, so a
  // depth-indexed pool is safe without locking.
  std::vector<std::unique_ptr<zen::evm::InterpreterExecContext>> NestedCtxPool;
  // Instance pool for depth > 0
  std::vector<EVMInstance *> CacheInsts;

  // ---- Profile-guided JIT state ----
#ifdef ZEN_ENABLE_MULTIPASS_JIT
  // Keyed by code_address (20 bytes) instead of a heap-allocated hex string;
  // evmc ships a std::hash<evmc::address> specialisation so the map works
  // out of the box and we save an allocation per call on the hot path.
  std::unordered_map<evmc::address, ContractProfile> ProfileStore;
  CallRingBuffer RingBuffer{Config.RingBufferCapacity};
  // Thread pool for background JIT compilation (lazily initialized).
  std::unique_ptr<JITCompilePool> CompilePool;
  // Statistics: number of background JIT compilations actually triggered.
  uint64_t BackgroundJITTriggerCount = 0;
#endif // ZEN_ENABLE_MULTIPASS_JIT
  // Statistics: total execute() call count.
  uint64_t ExecuteCallCount = 0;

#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
  zen::action::EVMJITCompileMetrics ForegroundCompileMetrics;
  uint64_t TopLevelExecuteCount = 0;
  uint64_t TopLevelExecuteWallNs = 0;
  uint64_t ProfileGuidedJITTriggerBaseline = 0;
  uint64_t ModuleCacheLookupCount = 0;
  uint64_t ModuleCacheHitCount = 0;
  uint64_t ModuleCacheMissCount = 0;
  uint64_t ModuleCacheValidationRejectCount = 0;
  uint64_t ModuleCacheEvictionCount = 0;
  uint64_t ModuleCachePeakEntryCount = 0;
  uint64_t TransientModuleLoadCount = 0;
  uint64_t JITFrameCount = 0;
  uint64_t JITActiveWallNs = 0;
  uint64_t InterpreterFrameCount = 0;
  uint64_t InterpreterActiveWallNs = 0;
  uint64_t CreateInterpreterFallbackCount = 0;
  uint64_t NewlyCreatedInterpreterFallbackCount = 0;
  uint64_t SmallCodeInterpreterFallbackCount = 0;
  uint64_t StickyInterpreterFallbackCount = 0;
  std::mutex PhaseMetricsMutex;
  std::atomic<uint32_t> ExecuteInFlight{0};
  std::atomic<uint32_t> BackgroundJITCompileInFlight{0};
  EVMActiveMode ActiveMode = EVMActiveMode::None;
  std::chrono::steady_clock::time_point ActiveModeStart;
  bool MeasureExecutionActive = false;
#endif

  // Track addresses created via CREATE/CREATE2 in the current transaction.
  // Used to skip JIT compilation for contracts that were just deployed
  // (unlikely to be called enough times to benefit from JIT).
  std::unordered_set<evmc::address> CreatedAddrsInTx;

  bool isModuleInUse(const EVMModule *Mod) const {
    if (CachedMainInst && CachedMainInst->getModule() == Mod)
      return true;
    for (const auto *Inst : CacheInsts) {
      if (Inst && Inst->getModule() == Mod)
        return true;
    }
    return false;
  }
};

#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
void accrueActiveWall(DTVM *VM, std::chrono::steady_clock::time_point Now) {
  if (VM->ActiveMode == EVMActiveMode::JIT) {
    VM->JITActiveWallNs += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Now - VM->ActiveModeStart)
            .count());
  } else if (VM->ActiveMode == EVMActiveMode::Interpreter) {
    VM->InterpreterActiveWallNs += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Now - VM->ActiveModeStart)
            .count());
  }
}

class EVMExecutionActiveScope {
public:
  EVMExecutionActiveScope(DTVM *VM, EVMActiveMode Mode) : VM(VM) {
    if (!VM->MeasureExecutionActive) {
      return;
    }
    if (Mode == EVMActiveMode::JIT) {
      ++VM->JITFrameCount;
    } else {
      ++VM->InterpreterFrameCount;
    }
    PreviousMode = VM->ActiveMode;
    if (PreviousMode == Mode) {
      return;
    }
    const auto Now = std::chrono::steady_clock::now();
    accrueActiveWall(VM, Now);
    VM->ActiveMode = Mode;
    VM->ActiveModeStart = Now;
    ChangedMode = true;
  }

  ~EVMExecutionActiveScope() {
    if (!ChangedMode) {
      return;
    }
    const auto Now = std::chrono::steady_clock::now();
    accrueActiveWall(VM, Now);
    VM->ActiveMode = PreviousMode;
    VM->ActiveModeStart = Now;
  }

private:
  DTVM *VM;
  EVMActiveMode PreviousMode = EVMActiveMode::None;
  bool ChangedMode = false;
};

class EVMCPhaseMetricsScope {
public:
  EVMCPhaseMetricsScope(DTVM *VM, const evmc_message *Msg) : VM(VM) {
    std::lock_guard<std::mutex> Guard(VM->PhaseMetricsMutex);
    const uint32_t Previous =
        VM->ExecuteInFlight.fetch_add(1, std::memory_order_relaxed);
    MeasureTopLevel = Previous == 0 && Msg != nullptr && Msg->depth == 0;
    if (!MeasureTopLevel) {
      return;
    }
    PreviousCompileMetrics = zen::action::exchangeEVMJITCompileMetrics(
        &VM->ForegroundCompileMetrics);
    VM->ActiveMode = EVMActiveMode::None;
    VM->MeasureExecutionActive = true;
    Start = std::chrono::steady_clock::now();
  }

  ~EVMCPhaseMetricsScope() {
    if (MeasureTopLevel) {
      VM->MeasureExecutionActive = false;
      const auto Elapsed = std::chrono::steady_clock::now() - Start;
      ++VM->TopLevelExecuteCount;
      VM->TopLevelExecuteWallNs += static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Elapsed)
              .count());
      zen::action::exchangeEVMJITCompileMetrics(PreviousCompileMetrics);
    }
    VM->ExecuteInFlight.fetch_sub(1, std::memory_order_release);
  }

private:
  DTVM *VM;
  zen::action::EVMJITCompileMetrics *PreviousCompileMetrics = nullptr;
  std::chrono::steady_clock::time_point Start;
  bool MeasureTopLevel = false;
};
#endif

ModuleGuard::~ModuleGuard() {
  if (ShouldUnload && Mod && VM && VM->RT) {
    if (!VM->RT->unloadEVMModule(Mod)) {
      ZEN_LOG_ERROR("failed to unload transient EVM module");
    }
  }
}

/// The implementation of the evmc_vm::destroy() method.
void destroy(evmc_vm *VMInstance) { delete static_cast<DTVM *>(VMInstance); }

/// The implementation of the evmc_vm::get_capabilities() method.
evmc_capabilities_flagset get_capabilities(evmc_vm * /*instance*/) {
  return EVMC_CAPABILITY_EVM1;
}

/// VM options.
///
/// The implementation of the evmc_vm::set_option() method.
/// VMs are allowed to omit this method implementation.
enum evmc_set_option_result set_option(evmc_vm *VMInstance, const char *Name,
                                       const char *Value) {
  auto *VM = static_cast<DTVM *>(VMInstance);
  if (std::strcmp(Name, "mode") == 0) {
    if (std::strcmp(Value, "interpreter") == 0) {
      VM->Config.Mode = RunMode::InterpMode;
      return EVMC_SET_OPTION_SUCCESS;
    } else if (std::strcmp(Value, "multipass") == 0) {
      VM->Config.Mode = RunMode::MultipassMode;
      return EVMC_SET_OPTION_SUCCESS;
    } else {
      return EVMC_SET_OPTION_INVALID_VALUE;
    }
  } else if (std::strcmp(Name, "enable_gas_metering") == 0) {
    if (std::strcmp(Value, "true") == 0) {
      VM->Config.EnableEvmGasMetering = true;
      return EVMC_SET_OPTION_SUCCESS;
    } else if (std::strcmp(Value, "false") == 0) {
      VM->Config.EnableEvmGasMetering = false;
      return EVMC_SET_OPTION_SUCCESS;
    } else {
      return EVMC_SET_OPTION_INVALID_VALUE;
    }
#ifdef ZEN_ENABLE_MULTIPASS_JIT
  } else if (std::strcmp(Name, "num_jit_compile_threads") == 0) {
    int Parsed = std::atoi(Value);
    if (Parsed > 0 && Parsed <= 256) {
      VM->Config.NumJITCompileThreads = static_cast<uint32_t>(Parsed);
      return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_VALUE;
  } else if (std::strcmp(Name, "profile_guided_jit") == 0) {
    bool Parsed = false;
    if (parseBoolEnvValue(Value, Parsed)) {
      VM->Config.EnableProfileGuidedJIT = Parsed;
      return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_VALUE;
  } else if (std::strcmp(Name, "jit_trigger_calls") == 0) {
    int Parsed = std::atoi(Value);
    if (Parsed > 0) {
      VM->Config.JITTriggerCallCount = static_cast<uint64_t>(Parsed);
      return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_VALUE;
  } else if (std::strcmp(Name, "jit_trigger_gas") == 0) {
    int Parsed = std::atoi(Value);
    if (Parsed > 0) {
      VM->Config.JITTriggerTotalGas = static_cast<uint64_t>(Parsed);
      return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_VALUE;
  } else if (std::strcmp(Name, "code_cache_dir") == 0) {
    if (Value != nullptr && Value[0] != '\0') {
      VM->Config.EVMCodeCacheDir = Value;
      return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_VALUE;
  } else if (std::strcmp(Name, "code_cache_mode") == 0) {
    if (std::strcmp(Value, "off") == 0) {
      VM->Config.EVMCodeCacheMode = 0;
      return EVMC_SET_OPTION_SUCCESS;
    }
    if (std::strcmp(Value, "ro") == 0) {
      VM->Config.EVMCodeCacheMode = 1;
      return EVMC_SET_OPTION_SUCCESS;
    }
    if (std::strcmp(Value, "rw") == 0) {
      VM->Config.EVMCodeCacheMode = 2;
      return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_VALUE;
  } else if (std::strcmp(Name, "ring_buffer_capacity") == 0) {
    int Parsed = std::atoi(Value);
    if (Parsed > 0 &&
        static_cast<size_t>(Parsed) <= profile::MAX_RING_BUFFER_CAPACITY) {
      VM->Config.RingBufferCapacity = static_cast<size_t>(Parsed);
      VM->RingBuffer = CallRingBuffer{VM->Config.RingBufferCapacity};
      return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_VALUE;
#endif
  }
  return EVMC_SET_OPTION_INVALID_NAME;
}

/// Ensure RT and Iso are initialized. Returns false on failure.
bool ensureRuntimeAndIsolation(DTVM *VM) {
  if (!VM->RT) {
    VM->RT = Runtime::newEVMRuntime(VM->Config, VM->ExecHost.get());
    if (!VM->RT)
      return false;
  }
  if (!VM->Iso) {
    VM->Iso = VM->RT->createManagedIsolation();
    if (!VM->Iso)
      return false;
  }
  return true;
}

#ifdef ZEN_ENABLE_MULTIPASS_JIT
/// Lazily initialize the JIT compile thread pool.
JITCompilePool &getOrCreateCompilePool(DTVM *VM) {
  if (!VM->CompilePool) {
    VM->CompilePool =
        std::make_unique<JITCompilePool>(VM->Config.NumJITCompileThreads);
  }
  return *VM->CompilePool;
}
#endif // ZEN_ENABLE_MULTIPASS_JIT

bool isNonCreateOperation(const evmc_message *Msg) {
  // Returns true if the EVM operation is not a CREATE or CREATE2.
  // CREATE/CREATE2 operations deploy new contracts with initcode, which is
  // one-shot and can differ across transactions for the same address.
  // All other operations (CALL/DELEGATECALL/STATICCALL/CALLCODE) are regular
  // contract calls where deployed code at a given address is immutable.
  return Msg != nullptr && Msg->kind != EVMC_CREATE &&
         Msg->kind != EVMC_CREATE2;
}

bool shouldRetryModuleLoadWithFastRA(const DTVM *VM, const Error &Err) {
  return VM->Config.Mode == RunMode::MultipassMode &&
#ifdef ZEN_ENABLE_MULTIPASS_JIT
         !VM->Config.DisableMultipassGreedyRA &&
#endif
         Err.getPhase() == ErrorPhase::Compilation &&
         Err.getSubphase() == ErrorSubphase::RegAlloc;
}

zen::common::MayBe<EVMModule *> loadEVMModuleWithRegAllocRetry(
    DTVM *VM, const std::string &ModName, const uint8_t *Code, size_t CodeSize,
    evmc_revision Rev, EVMMemorySpecializationProfile MemoryProfile = {}) {
  auto ModRet =
      VM->RT->loadEVMModule(ModName, Code, CodeSize, Rev, MemoryProfile);
  if (ModRet || !shouldRetryModuleLoadWithFastRA(VM, ModRet.getError())) {
    return ModRet;
  }

  RuntimeConfig RetryConfig = VM->Config;
#ifdef ZEN_ENABLE_MULTIPASS_JIT
  RetryConfig.DisableMultipassGreedyRA = true;
#endif
  ScopedConfig Retry(VM->RT.get(), RetryConfig);
  return VM->RT->loadEVMModule(ModName, Code, CodeSize, Rev, MemoryProfile);
}

EVMModule *loadTransientModule(DTVM *VM, const uint8_t *Code, size_t CodeSize,
                               evmc_revision Rev) {
  // Transient modules (CREATE init code, CreatedAddrsInTx contracts) are
  // short-lived and unlikely to benefit from JIT. Load them in interpreter
  // mode to skip EVMAnalyzer scans and JIT compilation entirely.
  RuntimeConfig TransientConfig = VM->Config;
  TransientConfig.Mode = RunMode::InterpMode;
  ScopedConfig InterpScope(VM->RT.get(), TransientConfig);

#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
  ++VM->TransientModuleLoadCount;
#endif
  std::string ModName = "tmp_mod_" + std::to_string(VM->ModCounter++);
  auto ModRet =
      loadEVMModuleWithRegAllocRetry(VM, ModName, Code, CodeSize, Rev);
  if (!ModRet)
    return nullptr;
  return *ModRet;
}

/// Find or load a cached EVMModule using two-level cache:
///   L0 (code pointer+size) -> L1 (address map) -> Cold load.
/// Shared by both interpreter and multipass paths.
/// Returns nullptr on failure.
EVMModule *findModuleCached(DTVM *VM, const uint8_t *Code, size_t CodeSize,
                            evmc_revision Rev, const evmc_message *Msg,
                            bool &IsTransient) {
  if (!isNonCreateOperation(Msg)) {
    IsTransient = true;
    return loadTransientModule(VM, Code, CodeSize, Rev);
  }

  IsTransient = false;
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
  ++VM->ModuleCacheLookupCount;
#endif
  const EVMMemorySpecializationProfile Profile =
      deriveMemorySpecializationProfile(Msg);

  // L0 disabled: pointer comparison is unsafe when callers reuse addresses
  // for different bytecode (e.g. test frameworks, repeated allocations).
  // Fall through to L1 address-based lookup with content validation.

  EVMModule *Mod = nullptr;

  // L1: Address-based LRU cache lookup
  // Copy first 8 bytes of code for cache key (no hash collision, fast memcmp)
  uint8_t CodePrefix[CODE_PREFIX_SIZE] = {};
  const size_t CopySize = std::min(CodeSize, CODE_PREFIX_SIZE);
  if (CopySize > 0 && Code != nullptr) {
    std::memcpy(CodePrefix, Code, CopySize);
  }
  CodeAddrRevKey AddrKey;
  AddrKey.Addr = Msg->code_address;
  AddrKey.Rev = Rev;
  AddrKey.MemoryProfile = Profile;
  std::memcpy(AddrKey.CodePrefix, CodePrefix, CODE_PREFIX_SIZE);
  AddrKey.CodeSize = CodeSize;
  auto It = VM->AddrCache.find(AddrKey);
  if (It != VM->AddrCache.end() &&
      validateCodeMatch(Code, CodeSize, It->second.first,
                        VM->EnableStrictAddrCacheValidation)) {
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
    ++VM->ModuleCacheHitCount;
#endif
    Mod = It->second.first;
    // LRU touch: move to front (most recently used)
    VM->LRUOrder.splice(VM->LRUOrder.begin(), VM->LRUOrder, It->second.second);
  } else {
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
    ++VM->ModuleCacheMissCount;
#endif
    // Cold path: full module load
    // If validation failed for an existing entry, evict the stale module
    if (It != VM->AddrCache.end()) {
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
      ++VM->ModuleCacheValidationRejectCount;
      ++VM->ModuleCacheEvictionCount;
#endif
      EVMModule *OldMod = It->second.first;
      if (VM->L0Mod == OldMod && Msg->depth == 0)
        VM->L0Mod = nullptr;
      VM->RT->unloadEVMModule(OldMod);
      VM->LRUOrder.erase(It->second.second);
      VM->AddrCache.erase(It);
    }

    // LRU eviction: if cache is at capacity, evict least recently used
    while (VM->AddrCache.size() >= VM->MaxModuleCacheSize &&
           !VM->LRUOrder.empty()) {
      auto &VictimKey = VM->LRUOrder.back();
      auto VictimIt = VM->AddrCache.find(VictimKey);
      if (VictimIt != VM->AddrCache.end()) {
        EVMModule *VictimMod = VictimIt->second.first;
        if (VM->isModuleInUse(VictimMod))
          break; // never evict a module referenced by an active instance
        if (VM->L0Mod == VictimMod)
          VM->L0Mod = nullptr;
        VM->RT->unloadEVMModule(VictimMod);
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
        ++VM->ModuleCacheEvictionCount;
#endif
        VM->AddrCache.erase(VictimIt);
      }
      VM->LRUOrder.pop_back();
    }

    std::string ModName = "mod_" + std::to_string(VM->ModCounter++);
    auto ModRet = loadEVMModuleWithRegAllocRetry(VM, ModName, Code, CodeSize,
                                                 Rev, Profile);
    if (!ModRet)
      return nullptr;
    Mod = *ModRet;

    VM->LRUOrder.push_front(AddrKey);
    VM->AddrCache[AddrKey] = {Mod, VM->LRUOrder.begin()};
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
    VM->ModuleCachePeakEntryCount =
        std::max<uint64_t>(VM->ModuleCachePeakEntryCount, VM->AddrCache.size());
#endif
  }

  // Update L0 cache members. Even though L0 lookup is disabled, we maintain
  // these state variables for two reasons:
  // 1. Eviction tracking: If a stale L1 entry is replaced, we need to
  // invalidate
  //    L0Mod if it pointed to the old module (done in the eviction path
  //    above).
  // 2. Future extensibility: It keeps the door open for re-enabling L0 later
  //    with a safer validation scheme (e.g., pointer + size + hash).
  VM->LastCodePtr = Code;
  VM->LastCodeSize = CodeSize;
  VM->L0Mod = Mod;
  return Mod;
}

/// Get or create an EVMInstance for the given module.
/// For top-level calls (Depth == 0), reuses a single cached main instance
/// when possible; for nested calls (Depth > 0), uses per-depth cached
/// instances stored in VM->CacheInsts, creating them lazily as needed.
/// The lifetime of all returned instances is managed by the DTVM object;
/// callers must not delete the returned pointer.
EVMInstance *getOrCreateInstance(DTVM *VM, EVMModule *Mod, evmc_revision Rev,
                                 int32_t Depth) {
  // if depth > 0, use cached instance
  if (Depth > 0) {
    EVMInstance *TempInst = nullptr;
    if (VM->CacheInsts.size() < static_cast<size_t>(Depth)) {
      auto InstRet = VM->Iso->createEVMInstance(*Mod, 0);
      if (!InstRet)
        return nullptr;
      TempInst = *InstRet;
      VM->CacheInsts.push_back(TempInst);
    } else {
      size_t Idx = Depth - 1;
      TempInst = VM->CacheInsts[Idx];
    }
    TempInst->resetForNewCall(Rev, *Mod);
    return TempInst;
  }

  // if depth == 0, reuse cached main instance
  EVMInstance *TheInst = VM->CachedMainInst;
  // Create new instance if cache is empty or module mismatch
  if (!TheInst || TheInst->getModule() != Mod) {
    if (TheInst) {
      // Reuse existing instance with new module
      TheInst->resetForNewCall(Rev, *Mod);
    } else {
      // Allocate new instance and cache it
      auto InstRet = VM->Iso->createEVMInstance(*Mod, 0);
      if (!InstRet)
        return nullptr;
      TheInst = *InstRet;
      VM->CachedMainInst = TheInst;
      // Ensure revision is initialized for the first use of cached main
      // instance; default is DEFAULT_REVISION (CANCUN), which can overcharge
      // pre-fork blocks if not reset.
      TheInst->resetForNewCall(Rev, *Mod);
    }
  } else {
    // Cache hit: same module, just reset with new revision
    TheInst->resetForNewCall(Rev, *Mod);
  }

  return TheInst;
}

/// Run the interpreter on an already-resolved (Mod, TheInst) pair. This is the
/// shared core used by both the public interpreter fast path and the
/// profile-guided JIT fall-back path inside the JIT-mode execute(): both
/// callers have just looked up the module and instance, and both want exactly
/// the same Ctx-pooling + interpret + result-extraction sequence. Keeping it
/// in one helper avoids the duplicated, drift-prone copy that lived in
/// execute() prior to PR #481 round-2 review.
evmc_result runInterpreterOnResolvedInstance(DTVM *VM, EVMModule *Mod,
                                             EVMInstance *TheInst,
                                             const evmc_message *Msg,
                                             bool IsTransientMod) {
  // Trigger bytecodeCache build if not yet done (lazy, cached on module)
  (void)Mod->getBytecodeCache();

  // Call interpreter directly, bypassing Runtime::callEVMMain overhead
  evmc_message MsgWithCode = *Msg;
  MsgWithCode.code = reinterpret_cast<uint8_t *>(Mod->Code);
  MsgWithCode.code_size = Mod->CodeSize;
  TheInst->setExeResult(evmc::Result{EVMC_SUCCESS, 0, 0});
  TheInst->pushMessage(&MsgWithCode);

  const bool ReuseTopLevel = !IsTransientMod && Msg->depth == 0;

  // Pick the InterpreterExecContext to run this call in:
  //  * Top-level (depth == 0, non-transient): reuse VM->CachedCtx.
  //  * Nested:    reuse VM->NestedCtxPool[depth] (grow on demand).
  // The previous implementation allocated a *fresh* InterpreterExecContext
  // for every nested call, which silently triggered a 32 KB EVMFrame
  // zero-init via FrameStack.emplace_back() on every recursion level.
  // Tests that recurse hundreds of times (e.g. transStorageOK) became
  // 24x slower than evmone baseline; the pool below makes the EVMFrame
  // allocation amortized O(1) per depth.
  zen::evm::InterpreterExecContext *CtxPtr = nullptr;
  if (ReuseTopLevel) {
    if (!VM->CachedCtx) {
      VM->CachedCtx =
          std::make_unique<zen::evm::InterpreterExecContext>(TheInst);
    } else {
      VM->CachedCtx->resetForNewCall(TheInst);
    }
    CtxPtr = VM->CachedCtx.get();
  } else {
    ZEN_ASSERT(Msg->depth >= 0 && Msg->depth <= 1024 &&
               "EVMC depth out of valid range [0, 1024]");
    const size_t Depth = static_cast<size_t>(Msg->depth);
    auto &Pool = VM->NestedCtxPool;
    if (Depth >= Pool.size()) {
      Pool.resize(Depth + 1);
    }
    auto &Slot = Pool[Depth];
    if (!Slot) {
      Slot = std::make_unique<zen::evm::InterpreterExecContext>(TheInst);
    } else {
      Slot->resetForNewCall(TheInst);
    }
    CtxPtr = Slot.get();
  }

  auto &Ctx = *CtxPtr;
  zen::evm::BaseInterpreter Interpreter(Ctx);
  Ctx.allocTopFrame(&MsgWithCode);
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
  {
    EVMExecutionActiveScope ActiveScope(VM, EVMActiveMode::Interpreter);
    Interpreter.interpret();
  }
#else
  Interpreter.interpret();
#endif

  evmc::Result Result =
      std::move(const_cast<evmc::Result &>(Ctx.getExeResult()));
  zen::evm::setFrameGasLeft(Result, TheInst->getGas());

  return Result.release_raw();
}

/// Fast path for interpreter mode: reuse cached instance, call interpreter
/// directly. This avoids per-call EVMInstance alloc/free and bypasses
/// Runtime::callEVMMain overhead.
evmc_result executeInterpreterFastPath(DTVM *VM,
                                       const evmc_host_interface *Host,
                                       evmc_host_context *Context,
                                       evmc_revision Rev,
                                       const evmc_message *Msg,
                                       const uint8_t *Code, size_t CodeSize) {
  // RAII guard for host context save/restore (exception safety)
  HostContextScope HostScope(VM->ExecHost.get(), Host, Context);

  // Ensure runtime and isolation exist
  if (!ensureRuntimeAndIsolation(VM)) {
    return evmc_make_result(EVMC_FAILURE, 0, 0, nullptr, 0);
  }

  // Module lookup: L1 address-based cache -> Cold load
  bool IsTransientMod = false;
  EVMModule *Mod =
      findModuleCached(VM, Code, CodeSize, Rev, Msg, IsTransientMod);
  if (!Mod) {
    return evmc_make_result(EVMC_FAILURE, 0, 0, nullptr, 0);
  }
  ModuleGuard ModGuard(VM, Mod, IsTransientMod);

  // Instance reuse (shared only for cacheable top-level calls)
  EVMInstance *TheInst = getOrCreateInstance(VM, Mod, Rev, Msg->depth);
  if (!TheInst) {
    return evmc_make_result(EVMC_FAILURE, 0, 0, nullptr, 0);
  }

  return runInterpreterOnResolvedInstance(VM, Mod, TheInst, Msg,
                                          IsTransientMod);
}

/// Profile-guided JIT: record call and check if JIT should be triggered.
/// Called after execution completes with the result available.
#ifdef ZEN_ENABLE_MULTIPASS_JIT
void updateProfileAndMaybeTriggerJIT(DTVM *VM, const evmc_message *Msg,
                                     const evmc_result &Result,
                                     EVMModule *Mod) {
  const evmc::address &ModAddr = Msg->code_address;
  // evmc gas fields are signed (int64_t); a faulting call can leave
  // gas_left == -1 or otherwise greater than Msg->gas, which would wrap
  // to a huge uint64_t when cast and corrupt WindowGasUsed. Clamp to 0
  // and saturate to Msg->gas to keep the sliding-window accounting sane.
  int64_t GasLeft = Result.gas_left;
  if (GasLeft < 0) {
    GasLeft = 0;
  } else if (GasLeft > Msg->gas) {
    GasLeft = Msg->gas;
  }
  uint64_t GasUsed = static_cast<uint64_t>(Msg->gas - GasLeft);

  // 1. Push new record; may evict the oldest record.
  auto Evicted = VM->RingBuffer.push(CallRecord{ModAddr, GasUsed});

  // 2. Process evicted record: decrement its profile counters.
  if (Evicted) {
    auto EvictIt = VM->ProfileStore.find(Evicted->ModAddr);
    if (EvictIt != VM->ProfileStore.end()) {
      auto &OldProfile = EvictIt->second;
      if (OldProfile.WindowCallCount > 0)
        OldProfile.WindowCallCount--;
      if (OldProfile.WindowGasUsed >= Evicted->GasUsed)
        OldProfile.WindowGasUsed -= Evicted->GasUsed;
      else
        OldProfile.WindowGasUsed = 0;

      // Clean up zero-count profiles to bound ProfileStore memory.
      if (OldProfile.WindowCallCount == 0) {
        VM->ProfileStore.erase(EvictIt);
      }
    }
  }

  // 3. Skip profile update for contracts already evaluated.
  auto ExistIt = VM->ProfileStore.find(ModAddr);
  if (ExistIt != VM->ProfileStore.end() &&
      (ExistIt->second.JITTriggered || ExistIt->second.JITRejected)) {
    return;
  }

  // 4. Update current contract profile (increment).
  auto &CurrentProfile = VM->ProfileStore[ModAddr];
  CurrentProfile.WindowCallCount++;
  CurrentProfile.WindowGasUsed += GasUsed;

  // 5. Check JIT trigger conditions (use runtime config if set, else defaults).
  const uint64_t TriggerCalls = VM->Config.JITTriggerCallCount
                                    ? VM->Config.JITTriggerCallCount
                                    : profile::JIT_TRIGGER_CALL_COUNT;
  const uint64_t TriggerGas = VM->Config.JITTriggerTotalGas
                                  ? VM->Config.JITTriggerTotalGas
                                  : profile::JIT_TRIGGER_TOTAL_GAS;
  if (CurrentProfile.WindowCallCount < TriggerCalls ||
      CurrentProfile.WindowGasUsed < TriggerGas) {
    return;
  }

  if (Mod->ShouldFallbackToInterp) {
    CurrentProfile.JITRejected = true;
    return;
  }

  // Avoid duplicate JIT triggers.
  if ((Mod->JITCompileFuture.valid() &&
       Mod->JITCompileFuture.wait_for(std::chrono::seconds(0)) !=
           std::future_status::ready) ||
      Mod->getJITCode()) {
    CurrentProfile.JITTriggered = true;
    return;
  }

  // Trigger background JIT compilation via thread pool.
  auto &Pool = getOrCreateCompilePool(VM);
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
  VM->BackgroundJITCompileInFlight.fetch_add(1, std::memory_order_relaxed);
  auto Future = Pool.submit([VM, Mod]() {
    struct CompletionGuard {
      DTVM *VM;
      ~CompletionGuard() {
        VM->BackgroundJITCompileInFlight.fetch_sub(1,
                                                   std::memory_order_release);
      }
    } Guard{VM};
    zen::action::performEVMJITCompile(*Mod);
  });
#else
  auto Future =
      Pool.submit([Mod]() { zen::action::performEVMJITCompile(*Mod); });
#endif
  if (!Future.valid()) {
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
    VM->BackgroundJITCompileInFlight.fetch_sub(1, std::memory_order_release);
#endif
    return;
  }
  CurrentProfile.JITTriggered = true;
  VM->BackgroundJITTriggerCount++;
  Mod->JITCompileFuture = std::move(Future);
}
#endif // ZEN_ENABLE_MULTIPASS_JIT

#ifdef ZEN_ENABLE_JIT

#ifdef ZEN_ENABLE_VIRTUAL_STACK
/// Virtual stack callback for JIT fast path: invoked with RSP on the virtual
/// stack. Follows the same pattern as callEVMFuncFromVirtualStack in
/// runtime.cpp.
static void callJITFromVirtualStack(zen::utils::VirtualStackInfo *StackInfo) {
  auto *Inst = static_cast<EVMInstance *>(StackInfo->SavedPtr1);
  auto *Msg = static_cast<evmc_message *>(StackInfo->SavedPtr2);
  auto *Result = static_cast<evmc::Result *>(StackInfo->SavedPtr3);
  Inst->getRuntime()->callEVMInJITMode(*Inst, *Msg, *Result);
}
#endif // ZEN_ENABLE_VIRTUAL_STACK
#endif // ZEN_ENABLE_JIT

/// The implementation of the evmc_vm::execute() method.
evmc_result execute(evmc_vm *EVMInstance, const evmc_host_interface *Host,
                    evmc_host_context *Context, enum evmc_revision Rev,
                    const evmc_message *Msg, const uint8_t *Code,
                    size_t CodeSize) {
  auto *VM = static_cast<DTVM *>(EVMInstance);
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
  EVMCPhaseMetricsScope MetricsScope(VM, Msg);
#endif
  VM->ExecuteCallCount++;

  // Clear created-address tracker at top-level call boundaries (new tx).
  if (Msg->depth == 0) {
    VM->CreatedAddrsInTx.clear();
  }

  // Interpreter mode: use optimized fast path (bypasses callEVMMain)
  if (VM->Config.Mode == RunMode::InterpMode || !isNonCreateOperation(Msg)) {
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
    if (!isNonCreateOperation(Msg)) {
      ++VM->CreateInterpreterFallbackCount;
    }
#endif
    evmc_result createResult =
        executeInterpreterFastPath(VM, Host, Context, Rev, Msg, Code, CodeSize);
    // Track newly created contract addresses so we can skip JIT for them.
    if (Msg->kind == EVMC_CREATE || Msg->kind == EVMC_CREATE2) {
      if (createResult.status_code == EVMC_SUCCESS) {
        VM->CreatedAddrsInTx.insert(
            static_cast<const evmc::address &>(createResult.create_address));
      }
    }
    return createResult;
  }

  // Skip JIT for contracts created in the current transaction.
  if (VM->CreatedAddrsInTx.count(
          static_cast<const evmc::address &>(Msg->code_address))) {
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
    ++VM->NewlyCreatedInterpreterFallbackCount;
#endif
    return executeInterpreterFastPath(VM, Host, Context, Rev, Msg, Code,
                                      CodeSize);
  }

  // Skip JIT for small contracts -- the per-call overhead of the JIT
  // execution path (TLS setup, callNativeGeneral, etc.) exceeds any
  // speedup for contracts with fewer than ~64 opcodes.
  static constexpr size_t kMinJITCodeSize = 64;
  if (CodeSize < kMinJITCodeSize) {
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
    ++VM->SmallCodeInterpreterFallbackCount;
#endif
    return executeInterpreterFastPath(VM, Host, Context, Rev, Msg, Code,
                                      CodeSize);
  }

#ifdef ZEN_ENABLE_JIT
  {
    // RAII guard for host context save/restore (exception safety)
    HostContextScope HostScope(VM->ExecHost.get(), Host, Context);

    if (!ensureRuntimeAndIsolation(VM)) {
      return evmc_make_result(EVMC_FAILURE, 0, 0, nullptr, 0);
    }

    // Module lookup: L1 address-based cache -> Cold load
    bool IsTransientMod = false;
    EVMModule *Mod =
        findModuleCached(VM, Code, CodeSize, Rev, Msg, IsTransientMod);
    if (!Mod) {
      return evmc_make_result(EVMC_FAILURE, 0, 0, nullptr, 0);
    }
    ModuleGuard ModGuard(VM, Mod, IsTransientMod);
    Mod->ModuleExecuteCount++;

    // O(1) flag check replaces per-call O(n) EVMAnalyzer scans and remains
    // sticky if a later JIT compilation attempt fails.
    if (Mod->ShouldFallbackToInterp) {
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
      ++VM->StickyInterpreterFallbackCount;
#endif
      return executeInterpreterFastPath(VM, Host, Context, Rev, Msg, Code,
                                        CodeSize);
    }

    // Instance reuse (shared only for cacheable top-level calls)
    auto *TheInst = getOrCreateInstance(VM, Mod, Rev, Msg->depth);
    if (!TheInst) {
      return evmc_make_result(EVMC_FAILURE, 0, 0, nullptr, 0);
    }

#ifdef ZEN_ENABLE_MULTIPASS_JIT
    // Profile-guided JIT: while the JIT is not yet ready for this module,
    // run the interpreter and (only for cacheable top-level invocations)
    // record the call into the sliding-window profile. Once the background
    // compile installs JITCode, subsequent calls naturally fall through to
    // the JIT fast path below. Reusing runInterpreterOnResolvedInstance
    // keeps the Ctx pooling and pre/post bookkeeping in lock-step with the
    // public interpreter fast path, so the two paths can never drift.
    if (!Mod->getJITCode()) {
      if (VM->Config.EnableProfileGuidedJIT) {
        // Profile-guided JIT: interpret first, trigger background JIT for hot
        // contracts.
        evmc_result RawResult = runInterpreterOnResolvedInstance(
            VM, Mod, TheInst, Msg, IsTransientMod);
        if (isNonCreateOperation(Msg)) {
          updateProfileAndMaybeTriggerJIT(VM, Msg, RawResult, Mod);
        }
        return RawResult;
      } else {
        // Default mode: synchronous JIT compilation on first call.
        // This is the default behavior when Profile-guided JIT is disabled.
        zen::action::performEVMJITCompile(*Mod);
        // If compilation failed, fall back to interpreter.
        if (!Mod->getJITCode()) {
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
          ++VM->StickyInterpreterFallbackCount;
#endif
          return executeInterpreterFastPath(VM, Host, Context, Rev, Msg, Code,
                                            CodeSize);
        }
        // Fall through to JIT fast path.
      }
    }
#endif // ZEN_ENABLE_MULTIPASS_JIT

    // JIT fast path: used by both non-PGJ mode and PGJ mode after JIT is
    // ready. Module and instance are already resolved above.
    //
    // Safety: EagerEVMJITCompiler::compile() now catches exceptions and only
    // logs on failure (see PR #481), so Mod->getJITCode() can legitimately be
    // nullptr here. Fall back to the interpreter fast path in that case
    // instead of jumping through a null function pointer.
    if (!Mod->getJITCode()) {
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
      ++VM->StickyInterpreterFallbackCount;
#endif
      return executeInterpreterFastPath(VM, Host, Context, Rev, Msg, Code,
                                        CodeSize);
    }

    evmc_message MsgWithCode = *Msg;
    MsgWithCode.code = reinterpret_cast<uint8_t *>(Mod->Code);
    MsgWithCode.code_size = Mod->CodeSize;
    TheInst->setExeResult(evmc::Result{EVMC_SUCCESS, 0, 0});
    TheInst->pushMessage(&MsgWithCode);

    evmc::Result Result;

#ifdef ZEN_ENABLE_VIRTUAL_STACK
    if (Msg->depth == 0) {
      // depth==0: set up virtual stack for stack overflow protection via guard
      // pages. The virtual stack switches RSP to a separate mmap'd region.
      zen::utils::VirtualStackInfo StackInfo;
      StackInfo.SavedPtr1 = TheInst;
      StackInfo.SavedPtr2 = &MsgWithCode;
      StackInfo.SavedPtr3 = &Result;
      TheInst->pushVirtualStack(&StackInfo);
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
      {
        EVMExecutionActiveScope ActiveScope(VM, EVMActiveMode::JIT);
        StackInfo.runInVirtualStack(&callJITFromVirtualStack);
      }
#else
      StackInfo.runInVirtualStack(&callJITFromVirtualStack);
#endif
      TheInst->popVirtualStack();
    } else {
      // depth>0: re-entered via EVMC host callback, already on physical stack
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
      {
        EVMExecutionActiveScope ActiveScope(VM, EVMActiveMode::JIT);
        VM->RT->callEVMInJITMode(*TheInst, MsgWithCode, Result);
      }
#else
      VM->RT->callEVMInJITMode(*TheInst, MsgWithCode, Result);
#endif
    }
#else
#ifdef ZEN_ENABLE_EVMC_PHASE_METRICS
    {
      EVMExecutionActiveScope ActiveScope(VM, EVMActiveMode::JIT);
      VM->RT->callEVMInJITMode(*TheInst, MsgWithCode, Result);
    }
#else
    VM->RT->callEVMInJITMode(*TheInst, MsgWithCode, Result);
#endif
#endif // ZEN_ENABLE_VIRTUAL_STACK

    zen::evm::setFrameGasLeft(Result, TheInst->getGas());
    return Result.release_raw();
  }
#else
  return evmc_make_result(EVMC_FAILURE, 0, 0, nullptr, 0);
#endif // ZEN_ENABLE_JIT
}

/// @cond internal
#if !defined(PROJECT_VERSION)
/// The dummy project version if not provided by the build system.
#define PROJECT_VERSION "0.0.0"
#endif
/// @endcond

DTVM::DTVM()
    : evmc_vm{EVMC_ABI_VERSION, "dtvm",    PROJECT_VERSION,
              ::destroy,        ::execute, ::get_capabilities,
              ::set_option},
      ExecHost(new WrappedHost) {
  if (const char *Mode = std::getenv("DTVM_EVM_MODE"); Mode != nullptr) {
    if (std::strcmp(Mode, "interpreter") == 0) {
      Config.Mode = RunMode::InterpMode;
    } else if (std::strcmp(Mode, "multipass") == 0) {
      Config.Mode = RunMode::MultipassMode;
    } else {
      ZEN_LOG_WARN("ignore invalid DTVM_EVM_MODE=%s", Mode);
    }
  }

  if (const char *EnableGas = std::getenv("DTVM_EVM_ENABLE_GAS_METERING");
      EnableGas != nullptr) {
    bool ParsedEnableGas = false;
    if (parseBoolEnvValue(EnableGas, ParsedEnableGas)) {
      Config.EnableEvmGasMetering = ParsedEnableGas;
    } else {
      ZEN_LOG_WARN("ignore invalid DTVM_EVM_ENABLE_GAS_METERING=%s", EnableGas);
    }
  }

  if (const char *StrictValidation =
          std::getenv("DTVM_EVM_STRICT_ADDR_CACHE_VALIDATION");
      StrictValidation != nullptr) {
    bool ParsedStrictValidation = true;
    if (parseBoolEnvValue(StrictValidation, ParsedStrictValidation)) {
      EnableStrictAddrCacheValidation = ParsedStrictValidation;
    } else {
      ZEN_LOG_WARN("ignore invalid DTVM_EVM_STRICT_ADDR_CACHE_VALIDATION=%s",
                   StrictValidation);
    }
  }

  // Benchmark affordance: the L1 module cache bound is a capacity knob, not a
  // correctness one. Long replay corpora hold more unique contracts than the
  // 4096 default, so every pass evicts and recompiles; raising the bound
  // isolates execution cost from that eviction traffic. Production keeps the
  // default -- this is opt-in and off unless the variable is set.
  if (const char *MaxCacheEnv = std::getenv("DTVM_EVM_MAX_MODULE_CACHE_SIZE");
      MaxCacheEnv != nullptr) {
    char *ParseEnd = nullptr;
    const unsigned long ParsedMax = std::strtoul(MaxCacheEnv, &ParseEnd, 10);
    if (ParseEnd != MaxCacheEnv && *ParseEnd == '\0' && ParsedMax > 0) {
      MaxModuleCacheSize = static_cast<size_t>(ParsedMax);
    } else {
      ZEN_LOG_WARN("ignore invalid DTVM_EVM_MAX_MODULE_CACHE_SIZE=%s",
                   MaxCacheEnv);
    }
  }
}

bool isDTVMInstance(const evmc_vm *VM) {
  return VM != nullptr && VM->abi_version == EVMC_ABI_VERSION &&
         VM->destroy == ::destroy && VM->execute == ::execute;
}
} // namespace

extern "C" evmc_vm *evmc_create_dtvmapi() { return new DTVM; }

extern "C" uint64_t dtvm_get_jit_trigger_count(evmc_vm *vm) {
#ifdef ZEN_ENABLE_MULTIPASS_JIT
  auto *VM = static_cast<DTVM *>(vm);
  return VM->BackgroundJITTriggerCount;
#else
  return 0;
#endif // ZEN_ENABLE_MULTIPASS_JIT
}

extern "C" dtvm_evmc_phase_metrics_status
dtvm_get_evmc_phase_metrics(evmc_vm *vm, dtvm_evmc_hot_metrics *metrics) {
  if (!isDTVMInstance(vm) || metrics == nullptr) {
    return DTVM_EVMC_PHASE_METRICS_INVALID_ARGUMENT;
  }
  if (metrics->version != DTVM_EVMC_PHASE_METRICS_VERSION ||
      metrics->struct_size != sizeof(dtvm_evmc_hot_metrics)) {
    return DTVM_EVMC_PHASE_METRICS_INCOMPATIBLE;
  }
#ifndef ZEN_ENABLE_EVMC_PHASE_METRICS
  return DTVM_EVMC_PHASE_METRICS_DISABLED;
#else
  auto *VM = static_cast<DTVM *>(vm);
  std::lock_guard<std::mutex> Guard(VM->PhaseMetricsMutex);
  if (VM->ExecuteInFlight.load(std::memory_order_acquire) != 0 ||
      VM->BackgroundJITCompileInFlight.load(std::memory_order_acquire) != 0 ||
      VM->ForegroundCompileMetrics.InFlightDepth != 0) {
    return DTVM_EVMC_PHASE_METRICS_BUSY;
  }
  const uint64_t ActiveWall = VM->JITActiveWallNs + VM->InterpreterActiveWallNs;
  if (VM->ForegroundCompileMetrics.SuccessfulInstallCount >
          VM->ForegroundCompileMetrics.AttemptCount ||
      VM->ForegroundCompileMetrics.WallTimeNs > VM->TopLevelExecuteWallNs ||
      ActiveWall > VM->TopLevelExecuteWallNs ||
      VM->AddrCache.size() > VM->ModuleCachePeakEntryCount) {
    return DTVM_EVMC_PHASE_METRICS_INCONSISTENT;
  }

  uint64_t ProfileGuidedJITTriggerCount = 0;
#ifdef ZEN_ENABLE_MULTIPASS_JIT
  if (VM->BackgroundJITTriggerCount < VM->ProfileGuidedJITTriggerBaseline) {
    return DTVM_EVMC_PHASE_METRICS_INCONSISTENT;
  }
  ProfileGuidedJITTriggerCount =
      VM->BackgroundJITTriggerCount - VM->ProfileGuidedJITTriggerBaseline;
#endif

  *metrics = dtvm_evmc_hot_metrics{
      DTVM_EVMC_PHASE_METRICS_VERSION,
      sizeof(dtvm_evmc_hot_metrics),
      VM->TopLevelExecuteCount,
      VM->TopLevelExecuteWallNs,
      VM->ForegroundCompileMetrics.AttemptCount,
      VM->ForegroundCompileMetrics.SuccessfulInstallCount,
      VM->ForegroundCompileMetrics.WallTimeNs,
      VM->TopLevelExecuteWallNs - VM->ForegroundCompileMetrics.WallTimeNs,
      ProfileGuidedJITTriggerCount,
      VM->ModuleCacheLookupCount,
      VM->ModuleCacheHitCount,
      VM->ModuleCacheMissCount,
      VM->ModuleCacheValidationRejectCount,
      VM->ModuleCacheEvictionCount,
      VM->AddrCache.size(),
      VM->ModuleCachePeakEntryCount,
      VM->TransientModuleLoadCount,
      VM->JITFrameCount,
      VM->JITActiveWallNs,
      VM->InterpreterFrameCount,
      VM->InterpreterActiveWallNs,
      VM->CreateInterpreterFallbackCount,
      VM->NewlyCreatedInterpreterFallbackCount,
      VM->SmallCodeInterpreterFallbackCount,
      VM->StickyInterpreterFallbackCount,
  };
  return DTVM_EVMC_PHASE_METRICS_SUCCESS;
#endif
}

extern "C" dtvm_evmc_phase_metrics_status
dtvm_reset_evmc_phase_metrics(evmc_vm *vm, uint32_t version,
                              uint32_t struct_size) {
  if (!isDTVMInstance(vm)) {
    return DTVM_EVMC_PHASE_METRICS_INVALID_ARGUMENT;
  }
  if (version != DTVM_EVMC_PHASE_METRICS_VERSION ||
      struct_size != sizeof(dtvm_evmc_hot_metrics)) {
    return DTVM_EVMC_PHASE_METRICS_INCOMPATIBLE;
  }
#ifndef ZEN_ENABLE_EVMC_PHASE_METRICS
  return DTVM_EVMC_PHASE_METRICS_DISABLED;
#else
  auto *VM = static_cast<DTVM *>(vm);
  std::lock_guard<std::mutex> Guard(VM->PhaseMetricsMutex);
  if (VM->ExecuteInFlight.load(std::memory_order_acquire) != 0 ||
      VM->BackgroundJITCompileInFlight.load(std::memory_order_acquire) != 0 ||
      VM->ForegroundCompileMetrics.InFlightDepth != 0) {
    return DTVM_EVMC_PHASE_METRICS_BUSY;
  }

  VM->TopLevelExecuteCount = 0;
  VM->TopLevelExecuteWallNs = 0;
  VM->ForegroundCompileMetrics = {};
  VM->ModuleCacheLookupCount = 0;
  VM->ModuleCacheHitCount = 0;
  VM->ModuleCacheMissCount = 0;
  VM->ModuleCacheValidationRejectCount = 0;
  VM->ModuleCacheEvictionCount = 0;
  VM->ModuleCachePeakEntryCount = VM->AddrCache.size();
  VM->TransientModuleLoadCount = 0;
  VM->JITFrameCount = 0;
  VM->JITActiveWallNs = 0;
  VM->InterpreterFrameCount = 0;
  VM->InterpreterActiveWallNs = 0;
  VM->CreateInterpreterFallbackCount = 0;
  VM->NewlyCreatedInterpreterFallbackCount = 0;
  VM->SmallCodeInterpreterFallbackCount = 0;
  VM->StickyInterpreterFallbackCount = 0;
  VM->ActiveMode = EVMActiveMode::None;
  VM->MeasureExecutionActive = false;
#ifdef ZEN_ENABLE_MULTIPASS_JIT
  VM->ProfileGuidedJITTriggerBaseline = VM->BackgroundJITTriggerCount;
#else
  VM->ProfileGuidedJITTriggerBaseline = 0;
#endif
  return DTVM_EVMC_PHASE_METRICS_SUCCESS;
#endif
}
