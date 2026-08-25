// Copyright (C) 2026 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef ZEN_COMPILER_EVM_CODE_CACHE_H
#define ZEN_COMPILER_EVM_CODE_CACHE_H

// Persistent EVM code cache (Phase 2 of the position-independent host-calls
// change): stores the relocatable ELF object a multipass EVM compilation
// emits, keyed by every codegen-affecting input, and reloads it through the
// same parse/copy path a fresh compilation uses.
//
// Failure is never an error: any miss, corruption, or version mismatch falls
// back to compilation and (in read-write mode) overwrites the entry.

#include "common/defines.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace COMPILER {

/// Target feature string as codegen itself computes it (context.cpp). Part of
/// the cache key: the same library emits different code on different hosts.
std::string getTargetFeatureString();

/// Inputs that affect emitted code. Extend together with
/// EVM_CODE_CACHE_FORMAT_VERSION.
struct EVMCodeCacheKeyInputs {
  const uint8_t *Bytecode = nullptr;
  size_t BytecodeSize = 0;
  int32_t Revision = 0;
  bool GasMetering = false;
  bool DisableGreedyRA = false;
  uint8_t MemoryStrideSkipLeadingZeroLimbStores = 0;
};

/// Hex key string (also the cache file basename). Covers the explicit inputs
/// plus the target feature string and a build tag that changes on every
/// library rebuild (conservative stand-in for a git-hash build id).
std::string deriveEVMCodeCacheKey(const EVMCodeCacheKeyInputs &Inputs);

/// Reads and integrity-checks one entry. Returns the ELF object bytes, or
/// std::nullopt on miss/corruption/mismatch (never throws).
std::optional<std::vector<char>> loadEVMObjectFromCache(const std::string &Dir,
                                                        const std::string &Key);

/// Atomically (tmp + rename) writes one entry. Best-effort: failures are
/// logged and swallowed.
void storeEVMObjectToCache(const std::string &Dir, const std::string &Key,
                           const char *Data, size_t Size);

} // namespace COMPILER

#endif // ZEN_COMPILER_EVM_CODE_CACHE_H
