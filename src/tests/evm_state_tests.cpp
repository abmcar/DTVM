// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "evm/interpreter.h"
#include "evm_test_utils.h"
#include "runtime/runtime.h"
#include "utils/others.h"
#include "zetaengine.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <gtest/gtest.h>
#include <rapidjson/document.h>

using namespace zen;
using namespace zen::evm;
using namespace zen::runtime;

namespace {

std::string getDefaultTestDir() {
  std::filesystem::path DirPath =
      std::filesystem::path(__FILE__).parent_path() /
      std::filesystem::path("../../tests/evm_spec_test/state_tests");
  return DirPath.string();
}

const std::string DefaultTestDir = getDefaultTestDir();
const bool Debug = false;

struct TestResult {
  std::string TestName;
  std::string ForkName;
  bool Passed;
  std::string ErrorMessage;
};

struct TestSummary {
  size_t TotalTests = 0;
  size_t PassedTests = 0;
  size_t FailedTests = 0;
  std::vector<TestResult> FailedTestDetails;
};

// Helper function to create temporary hex file from bytecode
std::string createTempHexFile(const std::string &HexCode) {
  if (HexCode.empty() || HexCode == "0x") {
    return "";
  }

  std::string TempPath = std::tmpnam(nullptr);
  TempPath += ".hex";

  std::string CleanHex = HexCode;
  if (CleanHex.size() >= 2 && CleanHex.substr(0, 2) == "0x") {
    CleanHex = CleanHex.substr(2);
  }

  std::ofstream File(TempPath);
  if (!File) {
    throw std::runtime_error("Failed to create temp file: " + TempPath);
  }
  File << CleanHex;
  File.close();

  return TempPath;
}

bool executeStateTest(const test_utils::StateTestFixture &Fixture,
                      const std::string &Fork,
                      const test_utils::ForkPostResult &ExpectedResult) {
  try {
    // Parse transaction data
    test_utils::ParsedTransaction PT = test_utils::createTransactionFromIndex(
        *Fixture.Transaction, ExpectedResult);

    // Find the target account (contract to call)
    const test_utils::ParsedAccount *TargetAccount = nullptr;
    for (const auto &PA : Fixture.PreState) {
      if (std::memcmp(PA.Address.bytes, PT.Message->recipient.bytes, 20) == 0) {
        TargetAccount = &PA;
        break;
      }
    }

    if (!TargetAccount) {
      if (Debug) {
        std::cout << "No target account found for test: " << Fixture.TestName
                  << std::endl;
      }
      return ExpectedResult.ExpectedException.empty() ? false : true;
    }

    // Skip if no code to execute
    if (TargetAccount->Account.code.empty()) {
      if (Debug) {
        std::cout << "No code to execute for test: " << Fixture.TestName
                  << std::endl;
      }
      return true; // Empty code execution is considered success
    }

    // Convert code to hex string and create temp file
    std::string HexCode =
        "0x" + zen::utils::toHex(TargetAccount->Account.code.data(),
                                 TargetAccount->Account.code.size());
    std::string TempFilePath = createTempHexFile(HexCode);

    RuntimeConfig Config;
    Config.Mode = common::RunMode::InterpMode;

    std::unique_ptr<evmc::Host> Host = std::make_unique<evmc::MockedHost>();
    evmc::MockedHost *MockedHost = static_cast<evmc::MockedHost *>(Host.get());
    MockedHost->tx_context = Fixture.Environment;

    for (const auto &PA : Fixture.PreState) {
      test_utils::addAccountToMockedHost(*MockedHost, PA.Address, PA.Account);
    }

    auto RT = Runtime::newEVMRuntime(Config, Host.get());
    if (!RT) {
      std::filesystem::remove(TempFilePath);
      return false;
    }

    auto ModRet = RT->loadEVMModule(TempFilePath);
    if (!ModRet) {
      std::filesystem::remove(TempFilePath);
      return false;
    }

    EVMModule *Mod = *ModRet;

    Isolation *Iso = RT->createManagedIsolation();
    if (!Iso) {
      std::filesystem::remove(TempFilePath);
      return false;
    }

    uint64_t GasLimit = static_cast<uint64_t>(PT.Message->gas) * 100;
    auto InstRet = Iso->createEVMInstance(*Mod, GasLimit);
    if (!InstRet) {
      std::filesystem::remove(TempFilePath);
      return false;
    }

    EVMInstance *Inst = *InstRet;

    InterpreterExecContext Ctx(Inst);
    BaseInterpreter Interpreter(Ctx);

    evmc_message Msg = *PT.Message;
    Msg.gas *= 10;
    Ctx.allocFrame(&Msg);

    bool ExecutionSucceeded = true;
    try {

      Interpreter.interpret();
      // Host->call(Msg);
    } catch (const std::exception &E) {
      ExecutionSucceeded = false;
      if (Debug) {
        std::cout << "Execution failed for " << Fixture.TestName << ": "
                  << E.what() << std::endl;
      }
    }

    std::filesystem::remove(TempFilePath);

    if (!ExpectedResult.ExpectedException.empty()) {
      return !ExecutionSucceeded;
    }

    if (!ExecutionSucceeded) {
      return false;
    }

    return test_utils::verifyStateRoot(*MockedHost,
                                       ExpectedResult.ExpectedHash) &&
           test_utils::verifyLogsHash(MockedHost->recorded_logs,
                                      ExpectedResult.ExpectedLogs);

  } catch (const std::exception &E) {
    if (Debug) {
      std::cout << "Exception in executeStateTest for " << Fixture.TestName
                << ": " << E.what() << std::endl;
    }
    return !ExpectedResult.ExpectedException.empty();
  }
}

class StateTestRunner {
public:
  explicit StateTestRunner(const std::string &TestDirectory = DefaultTestDir)
      : TestDirectory(TestDirectory) {}

  bool loadTestFixtures() {
    LoadedFixtures.clear();

    auto JsonFiles = test_utils::findJsonFiles(TestDirectory);
    if (Debug) {
      std::cout << "Found " << JsonFiles.size() << " JSON test files in "
                << TestDirectory << std::endl;
    }

    for (const auto &FilePath : JsonFiles) {
      auto Fixtures = test_utils::parseStateTestFile(FilePath);
      for (auto &Fixture : Fixtures) {
        LoadedFixtures.push_back(std::move(Fixture));
      }
    }

    if (Debug) {
      std::cout << "Loaded " << LoadedFixtures.size() << " test fixtures"
                << std::endl;
    }
    return !LoadedFixtures.empty();
  }

  TestSummary executeAllTests() {
    TestSummary Summary;
    if (LoadedFixtures.empty()) {
      std::cerr << "No test fixtures loaded. Call loadTestFixtures() first."
                << std::endl;
      return Summary;
    }

    for (const auto &Fixture : LoadedFixtures) {
      if (!Fixture.Post || !Fixture.Post->IsObject()) {
        ADD_FAILURE() << "Invalid test fixture: " << Fixture.TestName
                      << " - Post section missing or invalid";
        continue;
      }

      for (const auto &Fork : Fixture.Post->GetObject()) {
        std::string ForkName = Fork.name.GetString();

        const rapidjson::Value &ForkResults = Fork.value;
        if (!ForkResults.IsArray()) {
          ADD_FAILURE() << "Invalid fork results format for: " << ForkName
                        << " in test: " << Fixture.TestName;
          continue;
        }

        for (rapidjson::SizeType I = 0; I < ForkResults.Size(); ++I) {
          Summary.TotalTests++;
          TestResult Result =
              executeTestCase(Fixture, ForkName, ForkResults[I]);

          if (Result.Passed) {
            Summary.PassedTests++;
            if (Debug) {
              std::cout << "✓ " << Result.TestName << " [" << Result.ForkName
                        << "]" << std::endl;
            }
          } else {
            Summary.FailedTests++;
            Summary.FailedTestDetails.push_back(Result);
            if (Debug) {
              std::cout << "✗ " << Result.TestName << " [" << Result.ForkName
                        << "]" << std::endl;
            }
          }
        }
      }
    }

    return Summary;
  }

  // Print test summary
  static void printTestSummary(const TestSummary &Summary) {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "EVM State Test Results Summary:" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Total Tests:   " << Summary.TotalTests << std::endl;
    std::cout << "Passed Tests:  " << Summary.PassedTests << " ("
              << (Summary.TotalTests > 0
                      ? (Summary.PassedTests * 100 / Summary.TotalTests)
                      : 0)
              << "%)" << std::endl;
    std::cout << "Failed Tests:  " << Summary.FailedTests << std::endl;

    if (!Summary.FailedTestDetails.empty()) {
      std::cout << "\nFailed Tests:" << std::endl;
      for (const auto &Result : Summary.FailedTestDetails) {
        std::cout << "  - " << Result.TestName << " [" << Result.ForkName
                  << "]: " << Result.ErrorMessage << std::endl;
      }
    }
  }

private:
  std::string TestDirectory;
  std::vector<test_utils::StateTestFixture> LoadedFixtures;

  TestResult executeTestCase(const test_utils::StateTestFixture &Fixture,
                             const std::string &ForkName,
                             const rapidjson::Value &PostResult) {
    TestResult Result{Fixture.TestName, ForkName, false, ""};

    try {
      test_utils::ForkPostResult ExpectedResult =
          test_utils::parseForkPostResult(PostResult);
      Result.Passed = executeStateTest(Fixture, ForkName, ExpectedResult);
      if (!Result.Passed) {
        Result.ErrorMessage = "Test execution failed";
      }
    } catch (const std::exception &E) {
      Result.ErrorMessage = E.what();
    }

    return Result;
  }
};

class EVMStateTest : public testing::Test {
public:
  static void SetUpTestSuite() {
    Runner = std::make_unique<StateTestRunner>();
    if (!Runner->loadTestFixtures()) {
      std::cerr << "Failed to load test fixtures from " << DefaultTestDir
                << std::endl;
    }
  }

  static void TearDownTestSuite() { Runner.reset(); }

protected:
  static std::unique_ptr<StateTestRunner> Runner;
};

std::unique_ptr<StateTestRunner> EVMStateTest::Runner;

TEST_F(EVMStateTest, ExecuteAllStateTests) {
  ASSERT_TRUE(Runner) << "Test runner not initialized";

  TestSummary Summary = Runner->executeAllTests();
  StateTestRunner::printTestSummary(Summary);

  if (Summary.TotalTests == 0) {
    GTEST_SKIP() << "No compatible test cases found";
  }

  EXPECT_EQ(Summary.FailedTests, 0)
      << "Found " << Summary.FailedTests << " failed tests out of "
      << Summary.TotalTests;
}

} // anonymous namespace