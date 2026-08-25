// Copyright (C) 2026 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Persistent EVM code cache: store/load round-trip through independent
// Runtime instances, byte-equality of the installed JIT code, and behavioral
// gates (ro-mode miss compiles; corrupt entries are rejected and recompiled).

#include "action/compiler.h"
#include "compiler/evm_code_cache.h"
#include "evm_test_host.hpp"
#include "runtime/evm_module.h"
#include "zetaengine.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

using namespace zen;
using namespace zen::runtime;

namespace {

// PUSH1 7 PUSH1 2 MUL PUSH1 3 ADD PUSH1 0 MSTORE PUSH1 32 PUSH1 0 RETURN
const std::vector<uint8_t> kContract = {0x60, 0x07, 0x60, 0x02, 0x02, 0x60,
                                        0x03, 0x01, 0x60, 0x00, 0x52, 0x60,
                                        0x20, 0x60, 0x00, 0xf3};

struct CompiledModule {
  std::unique_ptr<zen::evm::ZenMockedEVMHost> Host;
  std::unique_ptr<Runtime> RT;
  EVMModule *Mod = nullptr;
};

// Loads and eagerly compiles kContract under the given cache config.
CompiledModule compileWithCache(const std::string &Dir, uint8_t Mode,
                                const char *Name) {
  CompiledModule Out;
  RuntimeConfig Config;
  Config.Mode = common::RunMode::MultipassMode;
  Config.EnableEvmGasMetering = true;
  Config.EVMCodeCacheDir = Dir;
  Config.EVMCodeCacheMode = Mode;
  Out.Host = std::make_unique<zen::evm::ZenMockedEVMHost>();
  Out.RT = Runtime::newEVMRuntime(Config, Out.Host.get());
  EXPECT_TRUE(Out.RT != nullptr);
  Out.Host->setRuntime(Out.RT.get());
  auto ModRet =
      Out.RT->loadEVMModule(Name, kContract.data(), kContract.size());
  EXPECT_TRUE(ModRet);
  Out.Mod = *ModRet;
  if (Out.Mod->getJITCode() == nullptr) {
    zen::action::performEVMJITCompile(*Out.Mod);
  }
  EXPECT_NE(Out.Mod->getJITCode(), nullptr);
  return Out;
}

std::filesystem::path freshCacheDir(const char *Tag) {
  auto Dir = std::filesystem::temp_directory_path() /
             (std::string("dtvm-code-cache-test-") + Tag + "-" +
              std::to_string(::getpid()));
  std::filesystem::remove_all(Dir);
  return Dir;
}

} // namespace

TEST(EVMCodeCachePersist, StoreLoadRoundTripUnit) {
  auto Dir = freshCacheDir("unit");
  const char Payload[] = "not a real object, just bytes for the unit layer";
  COMPILER::EVMCodeCacheKeyInputs Inputs;
  Inputs.Bytecode = kContract.data();
  Inputs.BytecodeSize = kContract.size();
  Inputs.Revision = 1;
  std::string Key = COMPILER::deriveEVMCodeCacheKey(Inputs);
  EXPECT_EQ(Key.size(), 64u);
  EXPECT_FALSE(COMPILER::loadEVMObjectFromCache(Dir.string(), Key));
  COMPILER::storeEVMObjectToCache(Dir.string(), Key, Payload, sizeof(Payload));
  auto Loaded = COMPILER::loadEVMObjectFromCache(Dir.string(), Key);
  ASSERT_TRUE(Loaded);
  ASSERT_EQ(Loaded->size(), sizeof(Payload));
  EXPECT_EQ(std::memcmp(Loaded->data(), Payload, sizeof(Payload)), 0);

  // Key sensitivity: any input change must change the key.
  Inputs.GasMetering = true;
  EXPECT_NE(COMPILER::deriveEVMCodeCacheKey(Inputs), Key);

  // Corruption: flip one payload byte -> reject.
  auto Entry = Dir / (Key + ".dcc");
  {
    std::fstream F(Entry, std::ios::in | std::ios::out | std::ios::binary);
    F.seekp(-1, std::ios::end);
    F.put('X');
  }
  EXPECT_FALSE(COMPILER::loadEVMObjectFromCache(Dir.string(), Key));
  std::filesystem::remove_all(Dir);
}

TEST(EVMCodeCachePersist, CompileWritesEntryAndReloadIsByteIdentical) {
  auto Dir = freshCacheDir("roundtrip");

  // First runtime: read-write cache. Compiles and writes one entry.
  auto A = compileWithCache(Dir.string(), 2, "cache_mod_a");
  size_t Entries = 0;
  for (auto &E : std::filesystem::directory_iterator(Dir)) {
    (void)E;
    ++Entries;
  }
  ASSERT_EQ(Entries, 1u);
  std::vector<uint8_t> CodeA(
      static_cast<const uint8_t *>(A.Mod->getJITCode()),
      static_cast<const uint8_t *>(A.Mod->getJITCode()) +
          A.Mod->getJITCodeSize());

  // Second, independent runtime: read-only cache. Must produce byte-identical
  // installed code without writing anything.
  auto B = compileWithCache(Dir.string(), 1, "cache_mod_b");
  ASSERT_EQ(B.Mod->getJITCodeSize(), A.Mod->getJITCodeSize());
  EXPECT_EQ(std::memcmp(B.Mod->getJITCode(), CodeA.data(), CodeA.size()), 0);
  size_t EntriesAfter = 0;
  for (auto &E : std::filesystem::directory_iterator(Dir)) {
    (void)E;
    ++EntriesAfter;
  }
  EXPECT_EQ(EntriesAfter, 1u);

  // Read-only mode with an empty dir must still compile (miss = fallback).
  auto EmptyDir = freshCacheDir("empty");
  auto C = compileWithCache(EmptyDir.string(), 1, "cache_mod_c");
  EXPECT_NE(C.Mod->getJITCode(), nullptr);
  EXPECT_FALSE(std::filesystem::exists(EmptyDir) &&
               !std::filesystem::is_empty(EmptyDir));

  std::filesystem::remove_all(Dir);
  std::filesystem::remove_all(EmptyDir);
}

// Byte-identity alone cannot distinguish a cache hit from a deterministic
// recompile. Decisive proof: swap contract1's entry payload for contract2's
// object; if the loader is live, a fresh runtime compiling contract1 installs
// contract2's code.
TEST(EVMCodeCachePersist, LoadPathInstallsCacheContentNotRecompile) {
  auto Dir = freshCacheDir("swap");

  // Compile both contracts read-write: two distinct entries.
  auto A1 = compileWithCache(Dir.string(), 2, "swap_mod_1");
  std::vector<uint8_t> Contract2(kContract);
  Contract2.insert(Contract2.begin(), {0x60, 0x01, 0x50}); // PUSH1 1 POP prefix
  {
    RuntimeConfig Config;
    Config.Mode = common::RunMode::MultipassMode;
    Config.EnableEvmGasMetering = true;
    Config.EVMCodeCacheDir = Dir.string();
    Config.EVMCodeCacheMode = 2;
    auto Host = std::make_unique<zen::evm::ZenMockedEVMHost>();
    auto RT = Runtime::newEVMRuntime(Config, Host.get());
    Host->setRuntime(RT.get());
    auto ModRet =
        RT->loadEVMModule("swap_mod_2", Contract2.data(), Contract2.size());
    ASSERT_TRUE(ModRet);
    if ((*ModRet)->getJITCode() == nullptr) {
      zen::action::performEVMJITCompile(**ModRet);
    }
    ASSERT_NE((*ModRet)->getJITCode(), nullptr);
  }

  auto keyFor = [&](const std::vector<uint8_t> &Code) {
    COMPILER::EVMCodeCacheKeyInputs Inputs;
    Inputs.Bytecode = Code.data();
    Inputs.BytecodeSize = Code.size();
    Inputs.Revision = static_cast<int32_t>(A1.Mod->getRevision());
    Inputs.GasMetering = true;
    return COMPILER::deriveEVMCodeCacheKey(Inputs);
  };
  std::string Key1 = keyFor(kContract);
  std::string Key2 = keyFor(Contract2);
  ASSERT_NE(Key1, Key2);
  auto Payload2 = COMPILER::loadEVMObjectFromCache(Dir.string(), Key2);
  ASSERT_TRUE(Payload2);
  COMPILER::storeEVMObjectToCache(Dir.string(), Key1, Payload2->data(),
                                  Payload2->size());

  // Fresh read-only runtime asks for contract1; the loader must hand back
  // contract2's code -- proving bytes came from the cache, not a recompile.
  auto B = compileWithCache(Dir.string(), 1, "swap_mod_1b");
  std::vector<uint8_t> CodeA1(
      static_cast<const uint8_t *>(A1.Mod->getJITCode()),
      static_cast<const uint8_t *>(A1.Mod->getJITCode()) +
          A1.Mod->getJITCodeSize());
  ASSERT_TRUE(B.Mod->getJITCodeSize() != CodeA1.size() ||
              std::memcmp(B.Mod->getJITCode(), CodeA1.data(), CodeA1.size()) !=
                  0);
  std::filesystem::remove_all(Dir);
}
