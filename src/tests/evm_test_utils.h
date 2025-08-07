// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef ZEN_TESTS_EVM_TEST_UTILS_H
#define ZEN_TESTS_EVM_TEST_UTILS_H

#include "evm/interpreter.h"
#include "evmc/mocked_host.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <rapidjson/document.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace zen {
namespace test_utils {

// RAII class for temporary hex files
class TempHexFile {
private:
  std::string FilePath;
  bool Valid = false;

public:
  explicit TempHexFile(const std::string &HexCode) {
    if (HexCode.empty() || HexCode == "0x") {
      return;
    }

    auto TempDir = std::filesystem::temp_directory_path();
    auto TempPath = TempDir / "dtvm_XXXXXX.hex";
    FilePath = TempPath.string();

    // Create unique filename
    auto BaseStr = TempPath.stem().string();
    auto Extension = TempPath.extension();
    for (int I = 0; I < 1000; ++I) {
      auto UniquePath =
          TempDir / (BaseStr + std::to_string(I) + Extension.string());
      if (!std::filesystem::exists(UniquePath)) {
        FilePath = UniquePath.string();
        break;
      }
    }

    std::string CleanHex = HexCode;
    if (CleanHex.size() >= 2 && CleanHex.substr(0, 2) == "0x") {
      CleanHex = CleanHex.substr(2);
    }

    std::ofstream File(FilePath);
    if (!File) {
      throw std::runtime_error("Failed to create temp file: " + FilePath);
    }
    File << CleanHex;
    File.close();
    Valid = true;
  }

  // Constructor for custom path and suffix
  TempHexFile(const std::string &BasePath, const std::string &Suffix,
              const std::string &Content) {
    if (Content.empty()) {
      return;
    }

    FilePath = BasePath + "/" + Suffix + ".hex";

    std::ofstream File(FilePath);
    if (!File) {
      throw std::runtime_error("Failed to create temp file: " + FilePath);
    }
    File << Content;
    File.close();
    Valid = true;
  }

  ~TempHexFile() {
    if (Valid && !FilePath.empty()) {
      std::filesystem::remove(FilePath);
    }
  }

  // Delete copy constructor and assignment operator
  TempHexFile(const TempHexFile &) = delete;
  TempHexFile &operator=(const TempHexFile &) = delete;

  // Allow move semantics
  TempHexFile(TempHexFile &&Other) noexcept
      : FilePath(std::move(Other.FilePath)), Valid(Other.Valid) {
    Other.Valid = false;
  }

  TempHexFile &operator=(TempHexFile &&Other) noexcept {
    if (this != &Other) {
      if (Valid && !FilePath.empty()) {
        std::filesystem::remove(FilePath);
      }
      FilePath = std::move(Other.FilePath);
      Valid = Other.Valid;
      Other.Valid = false;
    }
    return *this;
  }

  bool isValid() const { return Valid; }
  const std::string &getPath() const { return FilePath; }
};

struct ParsedAccount {
  evmc::address Address;
  evmc::MockedAccount Account;
};

struct ParsedTransaction {
  evmc_tx_context TxContext;
  std::unique_ptr<evmc_message> Message;
  std::vector<uint8_t> CallData;
};

struct StateTestFixture {
  std::string TestName;
  std::vector<ParsedAccount> PreState;
  evmc_tx_context Environment;
  std::unique_ptr<rapidjson::Document> Transaction;
  std::unique_ptr<rapidjson::Document> Post;

  // Make it movable but not copyable
  StateTestFixture() = default;
  StateTestFixture(const StateTestFixture &) = delete;
  StateTestFixture &operator=(const StateTestFixture &) = delete;
  StateTestFixture(StateTestFixture &&) = default;
  StateTestFixture &operator=(StateTestFixture &&) = default;
};

struct ForkPostResult {
  std::string ExpectedHash;
  std::string ExpectedLogs;
  std::string ExpectedException;
  std::vector<uint8_t> ExpectedTxBytes;
  struct {
    size_t Data = 0;
    size_t Gas = 0;
    size_t Value = 0;
  } Indexes;
};

std::vector<ParsedAccount> parsePreAccounts(const rapidjson::Value &Pre);

void addAccountToMockedHost(evmc::MockedHost &Host, const evmc::address &Addr,
                            const evmc::MockedAccount &Account);

evmc::address parseAddress(const std::string &HexAddr);
evmc::bytes32 parseBytes32(const std::string &HexStr);
evmc::uint256be parseUint256(const std::string &HexStr);
std::vector<uint8_t> parseHexData(const std::string &HexStr);

// State test specific functions
std::vector<std::string> findJsonFiles(const std::string &RootPath);
std::vector<StateTestFixture> parseStateTestFile(const std::string &FilePath);
ForkPostResult parseForkPostResult(const rapidjson::Value &PostResult);
ParsedTransaction
createTransactionFromIndex(const rapidjson::Document &Transaction,
                           const ForkPostResult &Result);

// State verification functions
std::string
calculateLogsHash(const std::vector<evmc::MockedHost::log_record> &Logs);
bool verifyStateRoot(evmc::MockedHost &Host, const std::string &ExpectedHash);
bool verifyLogsHash(const std::vector<evmc::MockedHost::log_record> &Logs,
                    const std::string &ExpectedHash);

} // namespace test_utils
} // namespace zen

#endif // ZEN_TESTS_EVM_TEST_UTILS_H
