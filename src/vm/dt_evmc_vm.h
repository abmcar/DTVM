// dt_evmc_vm.h
#ifndef DT_EVMC_VM_H
#define DT_EVMC_VM_H

#include <evmc/evmc.h>
#include <evmc/utils.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Creates DT VM.
 */
EVMC_EXPORT struct evmc_vm *evmc_create_dtvmapi(void);

/**
 * Returns the number of background JIT compilations triggered so far.
 * Used by tests to verify profile-guided JIT is working.
 */
EVMC_EXPORT uint64_t dtvm_get_jit_trigger_count(struct evmc_vm *vm);

#define DTVM_EVMC_PHASE_METRICS_VERSION 2u

enum dtvm_evmc_phase_metrics_status {
  DTVM_EVMC_PHASE_METRICS_SUCCESS = 0,
  DTVM_EVMC_PHASE_METRICS_INVALID_ARGUMENT = 1,
  DTVM_EVMC_PHASE_METRICS_DISABLED = 2,
  DTVM_EVMC_PHASE_METRICS_BUSY = 3,
  DTVM_EVMC_PHASE_METRICS_INCOMPATIBLE = 4,
  DTVM_EVMC_PHASE_METRICS_INCONSISTENT = 5,
};

struct dtvm_evmc_hot_metrics {
  uint32_t version;
  uint32_t struct_size;
  uint64_t top_level_execute_count;
  uint64_t top_level_execute_wall_ns;
  uint64_t synchronous_jit_compile_attempt_count;
  uint64_t synchronous_jit_compile_success_count;
  uint64_t synchronous_jit_compile_wall_ns;
  uint64_t non_compile_residual_ns;
  uint64_t profile_guided_jit_trigger_count;
  uint64_t module_cache_lookup_count;
  uint64_t module_cache_hit_count;
  uint64_t module_cache_miss_count;
  uint64_t module_cache_validation_reject_count;
  uint64_t module_cache_eviction_count;
  uint64_t module_cache_entry_count;
  uint64_t module_cache_peak_entry_count;
  uint64_t transient_module_load_count;
  uint64_t jit_frame_count;
  uint64_t jit_active_wall_ns;
  uint64_t interpreter_frame_count;
  uint64_t interpreter_active_wall_ns;
  uint64_t create_interpreter_fallback_count;
  uint64_t newly_created_interpreter_fallback_count;
  uint64_t small_code_interpreter_fallback_count;
  uint64_t sticky_interpreter_fallback_count;
};

/** Copies a quiescent, non-destructive diagnostic metrics snapshot. */
EVMC_EXPORT enum dtvm_evmc_phase_metrics_status
dtvm_get_evmc_phase_metrics(struct evmc_vm *vm,
                            struct dtvm_evmc_hot_metrics *metrics);

/** Resets diagnostic metrics at a quiescent replay boundary. */
EVMC_EXPORT enum dtvm_evmc_phase_metrics_status
dtvm_reset_evmc_phase_metrics(struct evmc_vm *vm, uint32_t version,
                              uint32_t struct_size);

#ifdef __cplusplus
}
#endif

#endif // DT_EVMC_VM_H
