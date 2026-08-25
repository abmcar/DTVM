// Copyright (C) 2026 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "compiler/evm_code_cache.h"

#include "utils/logging.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/SHA256.h>

#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace COMPILER {

namespace {

// Bump on any change to the entry layout or to the key derivation.
constexpr uint32_t EVM_CODE_CACHE_FORMAT_VERSION = 1;
constexpr uint32_t EVM_CODE_CACHE_MAGIC = 0x44434331; // "DCC1"

// Conservative build identity: distinct per library build, so a rebuilt
// libdtvmapi.so never consumes entries written by a previous build. Phase 3
// replaces this with a build-system-provided source hash so caches survive
// rebuilds of identical source.
const char *buildTag() {
  return __DATE__ " " __TIME__
#ifdef ZEN_ENABLE_EVM_GAS_REGISTER
         " gasreg=1"
#else
         " gasreg=0"
#endif
#ifdef ZEN_ENABLE_VIRTUAL_STACK
         " vstack=1"
#else
         " vstack=0"
#endif
#ifdef ZEN_ENABLE_CPU_EXCEPTION
         " cpuex=1"
#else
         " cpuex=0"
#endif
      ;
}

std::string toHex(const llvm::StringRef Bytes) {
  static const char *Digits = "0123456789abcdef";
  std::string Out;
  Out.reserve(Bytes.size() * 2);
  for (unsigned char C : Bytes) {
    Out.push_back(Digits[C >> 4]);
    Out.push_back(Digits[C & 0xf]);
  }
  return Out;
}

struct EntryHeader {
  uint32_t Magic = 0;
  uint32_t Format = 0;
  uint64_t PayloadSize = 0;
  uint8_t PayloadSha[32] = {};
};

void sha256Of(const char *Data, size_t Size, uint8_t Out[32]) {
  llvm::SHA256 H;
  H.update(llvm::StringRef(Data, Size));
  auto Digest = H.final();
  std::memcpy(Out, Digest.data(), 32);
}

} // namespace

std::string deriveEVMCodeCacheKey(const EVMCodeCacheKeyInputs &Inputs) {
  llvm::SHA256 H;
  auto Add = [&H](const void *P, size_t N) {
    H.update(llvm::StringRef(static_cast<const char *>(P), N));
  };
  const char *Tag = buildTag();
  Add(Tag, std::strlen(Tag));
  std::string Features = getTargetFeatureString();
  Add(Features.data(), Features.size());
  Add(&Inputs.Revision, sizeof(Inputs.Revision));
  uint8_t Flags = static_cast<uint8_t>(Inputs.GasMetering) |
                  (static_cast<uint8_t>(Inputs.DisableGreedyRA) << 1);
  Add(&Flags, sizeof(Flags));
  Add(&Inputs.MemoryStrideSkipLeadingZeroLimbStores,
      sizeof(Inputs.MemoryStrideSkipLeadingZeroLimbStores));
  uint64_t Size64 = Inputs.BytecodeSize;
  Add(&Size64, sizeof(Size64));
  Add(Inputs.Bytecode, Inputs.BytecodeSize);
  auto Digest = H.final();
  return toHex(llvm::StringRef(reinterpret_cast<const char *>(Digest.data()),
                               Digest.size()));
}

std::optional<std::vector<char>>
loadEVMObjectFromCache(const std::string &Dir, const std::string &Key) {
  const std::string Path = Dir + "/" + Key + ".dcc";
  std::ifstream In(Path, std::ios::binary);
  if (!In) {
    return std::nullopt;
  }
  EntryHeader Header;
  if (!In.read(reinterpret_cast<char *>(&Header), sizeof(Header))) {
    return std::nullopt;
  }
  if (Header.Magic != EVM_CODE_CACHE_MAGIC ||
      Header.Format != EVM_CODE_CACHE_FORMAT_VERSION ||
      Header.PayloadSize == 0 || Header.PayloadSize > (256u << 20)) {
    ZEN_LOG_WARN("evm code cache: rejecting %s (bad header)", Path.c_str());
    return std::nullopt;
  }
  std::vector<char> Payload(Header.PayloadSize);
  if (!In.read(Payload.data(), static_cast<std::streamsize>(Payload.size()))) {
    ZEN_LOG_WARN("evm code cache: rejecting %s (truncated)", Path.c_str());
    return std::nullopt;
  }
  uint8_t Sha[32];
  sha256Of(Payload.data(), Payload.size(), Sha);
  if (std::memcmp(Sha, Header.PayloadSha, sizeof(Sha)) != 0) {
    ZEN_LOG_WARN("evm code cache: rejecting %s (digest mismatch)",
                 Path.c_str());
    return std::nullopt;
  }
  return Payload;
}

void storeEVMObjectToCache(const std::string &Dir, const std::string &Key,
                           const char *Data, size_t Size) {
  std::error_code Ec;
  std::filesystem::create_directories(Dir, Ec);
  const std::string Path = Dir + "/" + Key + ".dcc";
  const std::string Tmp = Path + ".tmp." + std::to_string(::getpid());
  {
    std::ofstream Out(Tmp, std::ios::binary | std::ios::trunc);
    if (!Out) {
      ZEN_LOG_WARN("evm code cache: cannot write %s", Tmp.c_str());
      return;
    }
    EntryHeader Header;
    Header.Magic = EVM_CODE_CACHE_MAGIC;
    Header.Format = EVM_CODE_CACHE_FORMAT_VERSION;
    Header.PayloadSize = Size;
    sha256Of(Data, Size, Header.PayloadSha);
    Out.write(reinterpret_cast<const char *>(&Header), sizeof(Header));
    Out.write(Data, static_cast<std::streamsize>(Size));
    if (!Out) {
      ZEN_LOG_WARN("evm code cache: short write %s", Tmp.c_str());
      Out.close();
      std::filesystem::remove(Tmp, Ec);
      return;
    }
  }
  std::filesystem::rename(Tmp, Path, Ec);
  if (Ec) {
    ZEN_LOG_WARN("evm code cache: rename failed for %s: %s", Path.c_str(),
                 Ec.message().c_str());
    std::filesystem::remove(Tmp, Ec);
  }
}

} // namespace COMPILER
