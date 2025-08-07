// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "evm_test_utils.h"
#include "evm/merkle_patricia_trie.h"
#include "host/evm/crypto.h"
#include "runtime/runtime.h"
#include "utils/others.h"

#include <algorithm>
#include <evmc/hex.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <stdexcept>

namespace zen {
namespace test_utils {

namespace {
static std::string stripHexPrefix(const std::string &HexStr) {
  if (HexStr.size() >= 2 &&
      (HexStr.substr(0, 2) == "0x" || HexStr.substr(0, 2) == "0X")) {
    return HexStr.substr(2);
  }
  return HexStr;
}
} // namespace

evmc::address parseAddress(const std::string &HexAddr) {
  auto Data = zen::utils::fromHex(HexAddr);
  if (!Data || Data->size() != 20) {
    throw std::invalid_argument("Invalid address: " + HexAddr);
  }

  evmc::address Addr{};
  std::memcpy(Addr.bytes, Data->data(), 20);
  return Addr;
}

evmc::bytes32 parseBytes32(const std::string &HexStr) {
  auto Data = zen::utils::fromHex(HexStr);
  if (!Data) {
    throw std::invalid_argument("Invalid hex string: " + HexStr);
  }

  if (Data->size() > 32) {
    throw std::invalid_argument("Bytes32 hex string too long");
  }

  evmc::bytes32 Result{};
  std::memcpy(Result.bytes + (32 - Data->size()), Data->data(), Data->size());
  return Result;
}

evmc::uint256be parseUint256(const std::string &HexStr) {
  auto Data = zen::utils::fromHex(HexStr);
  if (!Data) {
    throw std::invalid_argument("Invalid hex string: " + HexStr);
  }

  if (Data->size() > 32) {
    throw std::invalid_argument("Uint256 hex string too long");
  }

  evmc::uint256be Result{};
  std::memcpy(Result.bytes + (32 - Data->size()), Data->data(), Data->size());
  return Result;
}

std::vector<uint8_t> parseHexData(const std::string &HexStr) {
  if (HexStr.empty()) {
    return {};
  }

  auto Result = zen::utils::fromHex(HexStr);
  if (!Result) {
    throw std::invalid_argument("Invalid hex string: " + HexStr);
  }
  return *Result;
}

std::vector<ParsedAccount> parsePreAccounts(const rapidjson::Value &Pre) {
  std::vector<ParsedAccount> Accounts;

  if (!Pre.IsObject()) {
    throw std::invalid_argument("Pre must be an object");
  }

  for (auto It = Pre.MemberBegin(); It != Pre.MemberEnd(); ++It) {
    const std::string AddrStr = It->name.GetString();
    const rapidjson::Value &AccountData = It->value;

    ParsedAccount PA;
    PA.Address = parseAddress(AddrStr);

    if (AccountData.HasMember("nonce") && AccountData["nonce"].IsString()) {
      std::string NonceStr = stripHexPrefix(AccountData["nonce"].GetString());
      PA.Account.nonce = static_cast<int>(std::stoull(NonceStr, nullptr, 16));
    }

    if (AccountData.HasMember("balance") && AccountData["balance"].IsString()) {
      PA.Account.balance = parseUint256(AccountData["balance"].GetString());
    }

    if (AccountData.HasMember("code") && AccountData["code"].IsString()) {
      auto CodeData = parseHexData(AccountData["code"].GetString());
      PA.Account.code.assign(CodeData.begin(), CodeData.end());

      // Calculate and set code hash
      auto CodeHashBytes = zen::host::evm::crypto::keccak256(CodeData);
      std::memcpy(PA.Account.codehash.bytes, CodeHashBytes.data(), 32);
    } else {
      // For accounts without code, set the empty code hash
      // EMPTY_CODE_HASH = keccak256("") =
      // 0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470
      std::vector<uint8_t> EmptyCode;
      auto EmptyCodeHash = zen::host::evm::crypto::keccak256(EmptyCode);
      std::memcpy(PA.Account.codehash.bytes, EmptyCodeHash.data(), 32);
    }

    if (AccountData.HasMember("storage") && AccountData["storage"].IsObject()) {
      const rapidjson::Value &Storage = AccountData["storage"];
      for (auto StorageIt = Storage.MemberBegin();
           StorageIt != Storage.MemberEnd(); ++StorageIt) {
        evmc::bytes32 Key = parseBytes32(StorageIt->name.GetString());
        evmc::bytes32 Value = parseBytes32(StorageIt->value.GetString());
        PA.Account.storage[Key] = evmc::StorageValue{Value};
      }
    }

    Accounts.push_back(std::move(PA));
  }

  return Accounts;
}

void addAccountToMockedHost(evmc::MockedHost &Host, const evmc::address &Addr,
                            const evmc::MockedAccount &Account) {
  Host.accounts[Addr] = Account;
}

std::vector<std::string> findJsonFiles(const std::string &RootPath) {
  std::vector<std::string> JsonFiles;

  if (!std::filesystem::exists(RootPath)) {
    return JsonFiles;
  }

  try {
    for (const auto &Entry :
         std::filesystem::recursive_directory_iterator(RootPath)) {
      if (Entry.is_regular_file() && Entry.path().extension() == ".json") {
        JsonFiles.push_back(Entry.path().string());
      }
    }
  } catch (const std::filesystem::filesystem_error &E) {
    throw std::runtime_error("Failed to traverse directory: " +
                             std::string(E.what()));
  }

  std::sort(JsonFiles.begin(), JsonFiles.end());
  return JsonFiles;
}

std::vector<StateTestFixture> parseStateTestFile(const std::string &FilePath) {
  std::vector<StateTestFixture> Fixtures;

  std::ifstream File(FilePath);
  if (!File.is_open()) {
    throw std::runtime_error("Failed to open file: " + FilePath);
  }

  rapidjson::IStreamWrapper ISW(File);
  rapidjson::Document Doc;
  Doc.ParseStream(ISW);

  if (Doc.HasParseError()) {
    throw std::runtime_error("Failed to parse JSON file: " + FilePath);
  }

  if (!Doc.IsObject()) {
    throw std::runtime_error("JSON root must be an object");
  }

  for (auto It = Doc.MemberBegin(); It != Doc.MemberEnd(); ++It) {
    StateTestFixture Fixture;
    Fixture.TestName = It->name.GetString();

    const rapidjson::Value &TestCase = It->value;

    if (TestCase.HasMember("pre")) {
      Fixture.PreState = parsePreAccounts(TestCase["pre"]);
    }

    if (TestCase.HasMember("env")) {
      const rapidjson::Value &Env = TestCase["env"];
      Fixture.Environment = {};

      if (Env.HasMember("currentCoinbase") &&
          Env["currentCoinbase"].IsString()) {
        Fixture.Environment.block_coinbase =
            parseAddress(Env["currentCoinbase"].GetString());
      }

      if (Env.HasMember("currentNumber") && Env["currentNumber"].IsString()) {
        std::string NumStr = stripHexPrefix(Env["currentNumber"].GetString());
        Fixture.Environment.block_number =
            static_cast<int64_t>(std::stoull(NumStr, nullptr, 16));
      }

      if (Env.HasMember("currentTimestamp") &&
          Env["currentTimestamp"].IsString()) {
        std::string TimestampStr =
            stripHexPrefix(Env["currentTimestamp"].GetString());
        Fixture.Environment.block_timestamp =
            static_cast<int64_t>(std::stoull(TimestampStr, nullptr, 16));
      }

      if (Env.HasMember("currentGasLimit") &&
          Env["currentGasLimit"].IsString()) {
        std::string GasLimitStr =
            stripHexPrefix(Env["currentGasLimit"].GetString());
        Fixture.Environment.block_gas_limit =
            static_cast<int64_t>(std::stoull(GasLimitStr, nullptr, 16));
      }

      if (Env.HasMember("currentBaseFee") && Env["currentBaseFee"].IsString()) {
        Fixture.Environment.block_base_fee =
            parseUint256(Env["currentBaseFee"].GetString());
      }

      if (Env.HasMember("currentRandom") && Env["currentRandom"].IsString()) {
        Fixture.Environment.block_prev_randao =
            parseBytes32(Env["currentRandom"].GetString());
      }
    }

    // Store transaction and post data as-is for later processing
    if (TestCase.HasMember("transaction")) {
      Fixture.Transaction = std::make_unique<rapidjson::Document>();
      Fixture.Transaction->CopyFrom(TestCase["transaction"],
                                    Fixture.Transaction->GetAllocator());

      // Parse gasPrice from transaction and set it in tx_context
      const rapidjson::Value &Transaction = TestCase["transaction"];
      if (Transaction.HasMember("gasPrice") &&
          Transaction["gasPrice"].IsString()) {
        Fixture.Environment.tx_gas_price =
            parseUint256(Transaction["gasPrice"].GetString());
      }
    }

    if (TestCase.HasMember("post")) {
      Fixture.Post = std::make_unique<rapidjson::Document>();
      Fixture.Post->CopyFrom(TestCase["post"], Fixture.Post->GetAllocator());
    }

    Fixtures.push_back(std::move(Fixture));
  }

  return Fixtures;
}

ForkPostResult parseForkPostResult(const rapidjson::Value &PostResult) {
  ForkPostResult Result;

  if (PostResult.HasMember("hash") && PostResult["hash"].IsString()) {
    Result.ExpectedHash = PostResult["hash"].GetString();
  }

  if (PostResult.HasMember("logs") && PostResult["logs"].IsString()) {
    Result.ExpectedLogs = PostResult["logs"].GetString();
  }

  if (PostResult.HasMember("expectException") &&
      PostResult["expectException"].IsString()) {
    Result.ExpectedException = PostResult["expectException"].GetString();
  }

  if (PostResult.HasMember("txbytes") && PostResult["txbytes"].IsString()) {
    Result.ExpectedTxBytes = parseHexData(PostResult["txbytes"].GetString());
  }

  if (PostResult.HasMember("indexes") && PostResult["indexes"].IsObject()) {
    const rapidjson::Value &Indexes = PostResult["indexes"];

    if (Indexes.HasMember("data") && Indexes["data"].IsNumber()) {
      Result.Indexes.Data = Indexes["data"].GetUint();
    }

    if (Indexes.HasMember("gas") && Indexes["gas"].IsNumber()) {
      Result.Indexes.Gas = Indexes["gas"].GetUint();
    }

    if (Indexes.HasMember("value") && Indexes["value"].IsNumber()) {
      Result.Indexes.Value = Indexes["value"].GetUint();
    }
  }

  return Result;
}

ParsedTransaction
createTransactionFromIndex(const rapidjson::Document &Transaction,
                           const ForkPostResult &Result) {
  ParsedTransaction PT;
  PT.TxContext = {};
  PT.Message = std::make_unique<evmc_message>();
  PT.Message->kind = EVMC_CALL;
  PT.Message->flags = 0;
  PT.Message->depth = 0;

  if (Transaction.HasMember("sender") && Transaction["sender"].IsString()) {
    PT.Message->sender = parseAddress(Transaction["sender"].GetString());
  }

  if (Transaction.HasMember("to") && Transaction["to"].IsString()) {
    PT.Message->recipient = parseAddress(Transaction["to"].GetString());
  }

  // Use indexed values from arrays
  if (Transaction.HasMember("gasLimit") && Transaction["gasLimit"].IsArray()) {
    const rapidjson::Value &GasArray = Transaction["gasLimit"];
    if (Result.Indexes.Gas < GasArray.Size()) {
      std::string GasStr =
          stripHexPrefix(GasArray[Result.Indexes.Gas].GetString());
      PT.Message->gas = static_cast<int64_t>(std::stoull(GasStr, nullptr, 16));
    }
  }

  if (Transaction.HasMember("value") && Transaction["value"].IsArray()) {
    const rapidjson::Value &ValueArray = Transaction["value"];
    if (Result.Indexes.Value < ValueArray.Size()) {
      PT.Message->value =
          parseUint256(ValueArray[Result.Indexes.Value].GetString());
    }
  }

  if (Transaction.HasMember("data") && Transaction["data"].IsArray()) {
    const rapidjson::Value &DataArray = Transaction["data"];
    if (Result.Indexes.Data < DataArray.Size()) {
      PT.CallData = parseHexData(DataArray[Result.Indexes.Data].GetString());
    }
  }

  PT.Message->input_data = PT.CallData.data();
  PT.Message->input_size = PT.CallData.size();

  return PT;
}

namespace {

std::vector<uint8_t> rlpEncodeLength(size_t Length, uint8_t Offset) {
  std::vector<uint8_t> Result;
  if (Length < 56) {
    Result.push_back(static_cast<uint8_t>(Offset + Length));
  } else {
    std::vector<uint8_t> LengthBytes;
    size_t TempLength = Length;
    while (TempLength > 0) {
      LengthBytes.push_back(static_cast<uint8_t>(TempLength & 0xFF));
      TempLength >>= 8;
    }
    std::reverse(LengthBytes.begin(), LengthBytes.end());
    Result.push_back(static_cast<uint8_t>(Offset + 55 + LengthBytes.size()));
    Result.insert(Result.end(), LengthBytes.begin(), LengthBytes.end());
  }
  return Result;
}

std::vector<uint8_t> rlpEncodeBytes(const std::vector<uint8_t> &Data) {
  if (Data.size() == 1 && Data[0] < 0x80) {
    return Data;
  }
  auto Header = rlpEncodeLength(Data.size(), 0x80);
  Header.insert(Header.end(), Data.begin(), Data.end());
  return Header;
}

std::vector<uint8_t>
rlpEncodeList(const std::vector<std::vector<uint8_t>> &Items) {
  std::vector<uint8_t> EncodedData;
  for (const auto &Item : Items) {
    EncodedData.insert(EncodedData.end(), Item.begin(), Item.end());
  }
  auto Header = rlpEncodeLength(EncodedData.size(), 0xc0);
  Header.insert(Header.end(), EncodedData.begin(), EncodedData.end());
  return Header;
}

std::string
calculateLogsHashImpl(const std::vector<evmc::MockedHost::log_record> &Logs) {
  std::vector<std::vector<uint8_t>> EncodedLogs;

  for (const auto &Log : Logs) {
    std::vector<std::vector<uint8_t>> LogComponents;

    // Address (20 bytes)
    std::vector<uint8_t> AddressBytes(Log.creator.bytes,
                                      Log.creator.bytes + 20);
    LogComponents.push_back(rlpEncodeBytes(AddressBytes));

    // Topics
    std::vector<std::vector<uint8_t>> TopicsEncoded;
    for (const auto &Topic : Log.topics) {
      std::vector<uint8_t> TopicBytes(Topic.bytes, Topic.bytes + 32);
      TopicsEncoded.push_back(rlpEncodeBytes(TopicBytes));
    }
    LogComponents.push_back(rlpEncodeList(TopicsEncoded));

    // Data
    std::vector<uint8_t> DataBytes(Log.data.begin(), Log.data.end());
    LogComponents.push_back(rlpEncodeBytes(DataBytes));

    // Encode the complete log entry
    EncodedLogs.push_back(rlpEncodeList(LogComponents));
  }

  // Encode the list of logs
  auto RlpEncodedLogs = rlpEncodeList(EncodedLogs);

  // Calculate Keccak-256 hash
  auto Hash = zen::host::evm::crypto::keccak256(RlpEncodedLogs);

  // Convert to hex string
  evmc::bytes_view HashView(
      reinterpret_cast<const unsigned char *>(Hash.data()), Hash.size());
  return evmc::hex(HashView);
}

} // anonymous namespace

std::string
calculateLogsHash(const std::vector<evmc::MockedHost::log_record> &Logs) {
  return calculateLogsHashImpl(Logs);
}

bool verifyLogsHash(const std::vector<evmc::MockedHost::log_record> &Logs,
                    const std::string &ExpectedHash) {
  std::string CalculatedHash = "0x" + calculateLogsHash(Logs);
  if (CalculatedHash != ExpectedHash) {
    std::cout << "CalculatedLogsHash: " << CalculatedHash << std::endl;
    std::cout << "ExpectedLogsHash: " << ExpectedHash << std::endl;
  }
  return CalculatedHash == ExpectedHash;
}

namespace {

// RLP encoding helper functions for state root calculation
std::vector<uint8_t> encodeRLPLength(size_t Length, uint8_t Offset) {
  std::vector<uint8_t> Result;

  if (Length < 56) {
    Result.push_back(static_cast<uint8_t>(Length + Offset));
  } else {
    std::vector<uint8_t> LengthBytes;
    size_t Temp = Length;
    while (Temp > 0) {
      LengthBytes.insert(LengthBytes.begin(),
                         static_cast<uint8_t>(Temp & 0xFF));
      Temp >>= 8;
    }
    Result.push_back(static_cast<uint8_t>(LengthBytes.size() + Offset + 55));
    Result.insert(Result.end(), LengthBytes.begin(), LengthBytes.end());
  }

  return Result;
}

std::vector<uint8_t> encodeRLPString(const std::vector<uint8_t> &Input) {
  if (Input.empty()) {
    return {0x80}; // RLP encoding for empty string
  }

  if (Input.size() == 1 && Input[0] < 0x80) {
    return Input;
  }

  auto LengthBytes = encodeRLPLength(Input.size(), 0x80);
  LengthBytes.insert(LengthBytes.end(), Input.begin(), Input.end());
  return LengthBytes;
}

std::vector<uint8_t>
encodeRLPList(const std::vector<std::vector<uint8_t>> &Items) {
  std::vector<uint8_t> Payload;
  for (const auto &Item : Items) {
    auto Encoded = encodeRLPString(Item);
    Payload.insert(Payload.end(), Encoded.begin(), Encoded.end());
  }

  auto LengthBytes = encodeRLPLength(Payload.size(), 0xc0);
  LengthBytes.insert(LengthBytes.end(), Payload.begin(), Payload.end());
  return LengthBytes;
}

// Convert uint256be to minimal byte representation (remove leading zeros)
std::vector<uint8_t> uint256beToBytes(const evmc::uint256be &Value) {
  const auto *Data = Value.bytes;
  size_t Start = 0;

  // Find first non-zero byte
  while (Start < sizeof(Value.bytes) && Data[Start] == 0) {
    Start++;
  }

  if (Start == sizeof(Value.bytes)) {
    return {}; // All zeros, return empty
  }

  return std::vector<uint8_t>(Data + Start, Data + sizeof(Value.bytes));
}

// Calculate storage root for an account
std::vector<uint8_t> calculateStorageRoot(
    const std::unordered_map<evmc::bytes32, evmc::StorageValue> &Storage) {
  zen::evm::MerklePatriciaTrie StorageTrie;

  for (const auto &[Key, StorageValue] : Storage) {
    // Skip empty values (deleted storage slots)
    bool IsEmpty = true;
    for (int I = 0; I < 32; I++) {
      if (StorageValue.current.bytes[I] != 0) {
        IsEmpty = false;
        break;
      }
    }
    if (IsEmpty)
      continue;

    // Use key hash as trie key
    auto KeyHash = zen::host::evm::crypto::keccak256(
        std::vector<uint8_t>(Key.bytes, Key.bytes + sizeof(Key.bytes)));

    // Convert storage value to minimal byte representation
    auto ValueBytes = uint256beToBytes(StorageValue.current);
    auto EncodedValue = encodeRLPString(ValueBytes);

    StorageTrie.put(KeyHash, EncodedValue);
  }

  return StorageTrie.rootHash();
}

// Encode account for state trie
std::vector<uint8_t> encodeAccount(const evmc::MockedAccount &Account) {
  std::vector<std::vector<uint8_t>> AccountFields;

  // 1. Nonce (as minimal bytes)
  if (Account.nonce == 0) {
    AccountFields.push_back({});
  } else {
    std::vector<uint8_t> NonceBytes;
    int Nonce = Account.nonce;
    while (Nonce > 0) {
      NonceBytes.insert(NonceBytes.begin(), static_cast<uint8_t>(Nonce & 0xFF));
      Nonce >>= 8;
    }
    AccountFields.push_back(NonceBytes);
  }

  // 2. Balance (as minimal bytes)
  auto BalanceBytes = uint256beToBytes(Account.balance);
  AccountFields.push_back(BalanceBytes);

  // 3. Storage root
  auto StorageRoot = calculateStorageRoot(Account.storage);
  AccountFields.push_back(StorageRoot);

  // 4. Code hash
  std::vector<uint8_t> CodeHash(Account.codehash.bytes,
                                Account.codehash.bytes +
                                    sizeof(Account.codehash.bytes));
  AccountFields.push_back(CodeHash);

  return encodeRLPList(AccountFields);
}

} // anonymous namespace

bool verifyStateRoot(evmc::MockedHost &Host, const std::string &ExpectedHash) {
  zen::evm::MerklePatriciaTrie StateTrie;

  // Build state trie from all accounts
  for (const auto &[Address, Account] : Host.accounts) {
    // Calculate address hash (used as key in state trie)
    auto AddressHash = zen::host::evm::crypto::keccak256(std::vector<uint8_t>(
        Address.bytes, Address.bytes + sizeof(Address.bytes)));

    // Encode account data
    auto EncodedAccount = encodeAccount(Account);

    // Add to state trie
    StateTrie.put(AddressHash, EncodedAccount);
  }

  // Calculate state root hash
  auto StateRoot = StateTrie.rootHash();

  // Convert to hex string for comparison
  evmc::bytes_view HashView(StateRoot.data(), StateRoot.size());
  std::string CalculatedHash = "0x" + evmc::hex(HashView);

  if (CalculatedHash != ExpectedHash) {
    std::cout << "CalculatedRootHash: " << CalculatedHash << std::endl;
    std::cout << "ExpectedRootHash: " << ExpectedHash << std::endl;
  }

  // Compare with expected hash
  return CalculatedHash == ExpectedHash;
}

} // namespace test_utils
} // namespace zen
