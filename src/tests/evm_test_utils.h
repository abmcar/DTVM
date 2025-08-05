// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef ZEN_TESTS_EVM_TEST_UTILS_H
#define ZEN_TESTS_EVM_TEST_UTILS_H

#include "evm/interpreter.h"
#include "evmc/mocked_host.hpp"
#include <filesystem>
#include <memory>
#include <rapidjson/document.h>
#include <string>
#include <vector>

namespace zen {
namespace test_utils {

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

ParsedTransaction parseEnvAndTransaction(const rapidjson::Value &Env,
                                         const rapidjson::Value &Transaction);

void addAccountToMockedHost(evmc::MockedHost &Host, const evmc::address &Addr,
                            const evmc::MockedAccount &Account);

void populateFrameWithMockedHost(evm::EVMFrame &Frame, evmc::MockedHost &Host,
                                 const std::vector<ParsedAccount> &Accounts);

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
