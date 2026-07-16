// Copyright (C) 2026 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "evm_test_host.hpp"
#include "runtime/evm_memory_specialization.h"
#include "runtime/evm_module.h"
#include "utils/evm.h"
#include "utils/logging.h"
#include "zetaengine.h"

#include <CLI/CLI.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace zen::common;
using namespace zen::runtime;
using namespace zen::utils;

namespace {

struct BenchOptions {
  std::string InputFile;
  std::string BuildLabel = "unknown";
  std::string SampleName;
  std::string TimingCsv;
  std::string CalldataHex;
  std::string ExpectedOutputHex;
  std::string SenderAddress = "1000000000000000000000000000000000000000";
  std::string ContractAddress = "0000000000000000000000000000000000000001";
  LoggerLevel LogLevel = LoggerLevel::Off;
  RunMode Mode = RunMode::MultipassMode;
  evmc_revision EvmRevision = EVMC_OSAKA;
  uint64_t GasLimit = 1000000000;
  uint32_t WarmupCalls = 100;
  uint32_t RepeatCalls = 1000;
  uint32_t BatchCalls = 1;
  bool VerifyOutput = false;
};

std::string inferSampleName(const std::string &InputFile) {
  std::filesystem::path Path(InputFile);
  std::string Stem = Path.stem().string();
  if (Stem.size() >= 4 && Stem.substr(Stem.size() - 4) == ".evm") {
    Stem.resize(Stem.size() - 4);
  }
  return Stem;
}

std::string normalizeHex(std::string Hex) {
  if (Hex.size() >= 2 && Hex[0] == '0' && (Hex[1] == 'x' || Hex[1] == 'X')) {
    Hex.erase(0, 2);
  }
  return Hex;
}

evmc_message createEvmCallMessage(const BenchOptions &Options,
                                  const std::vector<uint8_t> &Calldata) {
  evmc_message Msg{
      .kind = EVMC_CALL,
      .flags = 0u,
      .depth = 0,
      .gas = static_cast<int64_t>(Options.GasLimit),
      .recipient = zen::utils::parseAddress(Options.ContractAddress),
      .sender = zen::utils::parseAddress(Options.SenderAddress),
      .input_data = Calldata.empty() ? nullptr : Calldata.data(),
      .input_size = Calldata.size(),
      .value = {},
      .create2_salt = {},
      .code_address = zen::utils::parseAddress(Options.ContractAddress),
      .code = nullptr,
      .code_size = 0,
  };

  const int64_t IntrinsicGas = zen::utils::computeIntrinsicGas(
      Options.EvmRevision, EVMC_CALL, Msg.input_data, Msg.input_size);
  if (Msg.gas < IntrinsicGas) {
    throw std::runtime_error("intrinsic gas exceeds gas limit");
  }
  Msg.gas -= IntrinsicGas;
  return Msg;
}

int parseArgs(int argc, char **argv, BenchOptions &Options) {
  const std::unordered_map<std::string, RunMode> ModeMap = {
      {"interpreter", RunMode::InterpMode},
      {"multipass", RunMode::MultipassMode},
  };
  const std::unordered_map<std::string, LoggerLevel> LogMap = {
      {"trace", LoggerLevel::Trace}, {"debug", LoggerLevel::Debug},
      {"info", LoggerLevel::Info},   {"warn", LoggerLevel::Warn},
      {"error", LoggerLevel::Error}, {"fatal", LoggerLevel::Fatal},
      {"off", LoggerLevel::Off},
  };
  const std::unordered_map<std::string, evmc_revision> EvmRevisionMap = {
      {"frontier", EVMC_FRONTIER},
      {"homestead", EVMC_HOMESTEAD},
      {"tangerine_whistle", EVMC_TANGERINE_WHISTLE},
      {"spurious_dragon", EVMC_SPURIOUS_DRAGON},
      {"byzantium", EVMC_BYZANTIUM},
      {"constantinople", EVMC_CONSTANTINOPLE},
      {"petersburg", EVMC_PETERSBURG},
      {"istanbul", EVMC_ISTANBUL},
      {"berlin", EVMC_BERLIN},
      {"london", EVMC_LONDON},
      {"paris", EVMC_PARIS},
      {"shanghai", EVMC_SHANGHAI},
      {"cancun", EVMC_CANCUN},
      {"osaka", EVMC_OSAKA},
  };

  CLI::App App{"EVM execute-only memory benchmark harness"};
  App.add_option("INPUT_FILE", Options.InputFile, "EVM .hex input file")
      ->required();
  App.add_option("--build-label", Options.BuildLabel,
                 "Label written to the CSV build column");
  App.add_option("--sample", Options.SampleName,
                 "Label written to the CSV sample column");
  App.add_option("--timing-csv", Options.TimingCsv,
                 "Output CSV path for per-call timings")
      ->required();
  App.add_option("--calldata", Options.CalldataHex, "Call data hex");
  App.add_option("--expected-output", Options.ExpectedOutputHex,
                 "Expected output hex used with --verify-output");
  App.add_flag("--verify-output", Options.VerifyOutput,
               "Check every call output after the measured timing boundary");
  App.add_option("--sender", Options.SenderAddress, "EVM sender address");
  App.add_option("--contract-address", Options.ContractAddress,
                 "EVM contract address");
  App.add_option("--gas-limit", Options.GasLimit, "Gas limit");
  App.add_option("--warmup-call", Options.WarmupCalls, "Warmup calls");
  App.add_option("--repeat-call", Options.RepeatCalls, "Measured calls");
  App.add_option("--batch-call", Options.BatchCalls,
                 "Calls measured inside each formal timing row");
  App.add_option("-m,--mode", Options.Mode, "Execution mode")
      ->transform(CLI::CheckedTransformer(ModeMap, CLI::ignore_case));
  App.add_option("--log-level", Options.LogLevel, "Log level")
      ->transform(CLI::CheckedTransformer(LogMap, CLI::ignore_case));
  App.add_option("--evm-revision", Options.EvmRevision, "EVM revision")
      ->transform(CLI::CheckedTransformer(EvmRevisionMap, CLI::ignore_case));

  try {
    App.parse(argc, argv);
  } catch (const CLI::ParseError &E) {
    return App.exit(E);
  }
  return EXIT_SUCCESS;
}

bool runCall(Runtime &RT, EVMInstance &Inst, evmc_message &Msg,
             evmc::Result &Result) {
  RT.callEVMMain(Inst, Msg, Result);
  return Result.status_code == EVMC_SUCCESS;
}

bool verifyOutput(const BenchOptions &Options, const evmc::Result &Result,
                  const char *Phase, uint32_t Index) {
  if (!Options.VerifyOutput) {
    return true;
  }
  const std::string Actual =
      zen::utils::toHex(Result.output_data, Result.output_size);
  if (Actual == Options.ExpectedOutputHex) {
    return true;
  }
  std::cerr << "output mismatch in " << Phase << " call " << Index
            << "\nexpected: " << Options.ExpectedOutputHex
            << "\nactual:   " << Actual << "\n";
  return false;
}

} // namespace

int main(int argc, char **argv) {
  BenchOptions Options;
  int ParseRet = parseArgs(argc, argv, Options);
  if (ParseRet != EXIT_SUCCESS) {
    return ParseRet;
  }
  if (Options.SampleName.empty()) {
    Options.SampleName = inferSampleName(Options.InputFile);
  }
  Options.ExpectedOutputHex = normalizeHex(Options.ExpectedOutputHex);

  try {
    zen::setGlobalLogger(
        createConsoleLogger("evm_memory_execute_only_bench", Options.LogLevel));
  } catch (const std::exception &E) {
    std::cerr << "failed to create logger: " << E.what() << "\n";
    return EXIT_FAILURE;
  }

  auto CalldataBytes = zen::utils::fromHex(Options.CalldataHex);
  if (!CalldataBytes.has_value()) {
    std::cerr << "invalid calldata hex\n";
    return EXIT_FAILURE;
  }

  RuntimeConfig Config;
  Config.Format = InputFormat::EVM;
  Config.Mode = Options.Mode;

  auto MockedHost = std::make_unique<zen::evm::ZenMockedEVMHost>();
  MockedHost->tx_context.tx_origin =
      zen::utils::parseAddress(Options.SenderAddress);

  auto RT = Runtime::newEVMRuntime(Config, MockedHost.get());
  if (!RT) {
    std::cerr << "failed to create runtime\n";
    return EXIT_FAILURE;
  }
  MockedHost->setRuntime(RT.get());

  const EVMMemorySpecializationProfile MemoryProfile =
      deriveEVMMemorySpecializationProfileFromCallData(CalldataBytes->data(),
                                                       CalldataBytes->size());
  auto ModRet =
      RT->loadEVMModule(Options.InputFile, Options.EvmRevision, MemoryProfile);
  if (!ModRet) {
    const auto &ErrMsg = ModRet.getError().getFormattedMessage(false);
    std::cerr << "failed to load EVM module: " << ErrMsg << "\n";
    return EXIT_FAILURE;
  }
  EVMModule *Mod = *ModRet;

  Isolation *Iso = RT->createManagedIsolation();
  if (!Iso) {
    std::cerr << "failed to create EVM isolation\n";
    return EXIT_FAILURE;
  }

  auto InstRet = Iso->createEVMInstance(*Mod, Options.GasLimit);
  if (!InstRet) {
    const auto &ErrMsg = InstRet.getError().getFormattedMessage(false);
    std::cerr << "failed to create EVM instance: " << ErrMsg << "\n";
    return EXIT_FAILURE;
  }
  EVMInstance *Inst = *InstRet;
  Inst->setRevision(Options.EvmRevision);

  evmc_message Msg = createEvmCallMessage(Options, *CalldataBytes);
  zen::utils::prewarmTransactionAccounts(*MockedHost, Options.EvmRevision,
                                         Msg.sender, Msg.recipient,
                                         MockedHost->tx_context.block_coinbase);

  std::filesystem::path CsvPath(Options.TimingCsv);
  if (CsvPath.has_parent_path()) {
    std::filesystem::create_directories(CsvPath.parent_path());
  }
  std::ofstream Csv(Options.TimingCsv);
  if (!Csv) {
    std::cerr << "failed to open timing csv: " << Options.TimingCsv << "\n";
    return EXIT_FAILURE;
  }
  if (Options.BatchCalls > 1) {
    Csv << "build,sample,row_idx,batch_calls,total_ns,avg_ns_per_call,"
           "exit_code\n";
  } else {
    Csv << "build,sample,run_idx,elapsed_ns,exit_code\n";
  }

  evmc::Result ProbeResult;
  Inst->resetForNewCall(Options.EvmRevision);
  if (!runCall(*RT, *Inst, Msg, ProbeResult)) {
    Csv << Options.BuildLabel << ',' << Options.SampleName << ",-1,0,"
        << static_cast<int>(ProbeResult.status_code) << "\n";
    return static_cast<int>(ProbeResult.status_code);
  }
  if (!verifyOutput(Options, ProbeResult, "probe", 0)) {
    return EXIT_FAILURE;
  }

  for (uint32_t I = 0; I < Options.WarmupCalls; ++I) {
    for (uint32_t J = 0; J < Options.BatchCalls; ++J) {
      evmc::Result WarmupResult;
      Inst->resetForNewCall(Options.EvmRevision);
      if (!runCall(*RT, *Inst, Msg, WarmupResult)) {
        return static_cast<int>(WarmupResult.status_code);
      }
      if (!verifyOutput(Options, WarmupResult, "warmup", I)) {
        return EXIT_FAILURE;
      }
    }
  }

  bool AllSucceeded = true;
  for (uint32_t I = 0; I < Options.RepeatCalls; ++I) {
    if (Options.BatchCalls <= 1) {
      evmc::Result Result;
      Inst->resetForNewCall(Options.EvmRevision);
      const auto Start = std::chrono::steady_clock::now();
      const bool Success = runCall(*RT, *Inst, Msg, Result);
      const auto End = std::chrono::steady_clock::now();
      const auto ElapsedNs =
          std::chrono::duration_cast<std::chrono::nanoseconds>(End - Start)
              .count();
      const int ExitCode = static_cast<int>(Result.status_code);
      Csv << Options.BuildLabel << ',' << Options.SampleName << ',' << I << ','
          << ElapsedNs << ',' << ExitCode << '\n';
      AllSucceeded = AllSucceeded && Success;
      AllSucceeded = AllSucceeded && verifyOutput(Options, Result, "formal", I);
      continue;
    }

    int BatchExitCode = 0;
    const auto Start = std::chrono::steady_clock::now();
    for (uint32_t J = 0; J < Options.BatchCalls; ++J) {
      evmc::Result Result;
      Inst->resetForNewCall(Options.EvmRevision);
      const bool Success = runCall(*RT, *Inst, Msg, Result);
      const int ExitCode = static_cast<int>(Result.status_code);
      if (!Success && BatchExitCode == 0) {
        BatchExitCode = ExitCode;
      }
      AllSucceeded = AllSucceeded && Success;
      AllSucceeded = AllSucceeded && verifyOutput(Options, Result, "formal", I);
    }
    const auto End = std::chrono::steady_clock::now();
    const auto TotalNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(End - Start)
            .count();
    const double AvgNsPerCall =
        static_cast<double>(TotalNs) / static_cast<double>(Options.BatchCalls);
    Csv << Options.BuildLabel << ',' << Options.SampleName << ',' << I << ','
        << Options.BatchCalls << ',' << TotalNs << ',' << AvgNsPerCall << ','
        << BatchExitCode << '\n';
  }

  if (!RT->unloadEVMModule(Mod)) {
    std::cerr << "failed to unload EVM module\n";
    return EXIT_FAILURE;
  }
  if (!Iso->deleteEVMInstance(Inst)) {
    std::cerr << "failed to delete EVM instance\n";
    return EXIT_FAILURE;
  }
  RT->deleteManagedIsolation(Iso);

  return AllSucceeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
