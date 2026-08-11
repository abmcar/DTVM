// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef COMPILER_EVM_FRONTEND_EVM_RUNTIME_HELPER_EFFECTS_H
#define COMPILER_EVM_FRONTEND_EVM_RUNTIME_HELPER_EFFECTS_H

#include "compiler/evm_frontend/evm_memory_facts.h"

#include <cstdint>

namespace COMPILER {

enum class RuntimeMemoryHelperId : uint8_t {
  Unknown,
  CodeCopyNoExpand,
  CallDataCopyNoExpand,
  KeccakNoExpand,
  TwoWordKeccakNoExpand,
  ReturnNoExpand,
  RevertNoExpand,
  ExpandMemoryNoGas,
  CallNoExpand
};

enum class RuntimeProofRequirementFlag : uint16_t {
  LogicalSize = 1U << 0,
  AccessRange = 1U << 1,
  DynamicGasCharged = 1U << 2,
  BoundsValidated = 1U << 3,
  OrderToken = 1U << 4,
  CallArgumentsRange = 1U << 5,
  CallReturnRange = 1U << 6
};

// Lowering accumulates obligations as it emits the corresponding operations.
// Prepared-memory helpers accept this typed token instead of a preassembled
// bit mask, making the discharge order explicit at each call site.
class RuntimeProofToken {
public:
  void establish(RuntimeProofRequirementFlag Requirement) {
    Provided |= static_cast<uint16_t>(Requirement);
  }

  bool provides(RuntimeProofRequirementFlag Requirement) const {
    return (Provided & static_cast<uint16_t>(Requirement)) != 0;
  }

  uint16_t bits() const { return Provided; }

private:
  uint16_t Provided = 0;
};

// Contract for prepared-memory runtime helpers. Requirements are obligations
// discharged by lowering before the call; effects describe the helper's
// normal-success transition. Exceptional behavior remains fail closed through
// MayTrapOrHalt and OrderToken.
struct RuntimeMemoryHelperContract {
  uint16_t Requirements = 0;
  MemoryEffectSummary Effects;
  bool EstablishesLogicalSize = false;
  bool Valid = false;

  void require(RuntimeProofRequirementFlag Requirement) {
    Requirements |= static_cast<uint16_t>(Requirement);
  }

  bool hasRequirement(RuntimeProofRequirementFlag Requirement) const {
    return (Requirements & static_cast<uint16_t>(Requirement)) != 0;
  }
};

inline bool satisfiesRuntimeMemoryHelperContract(
    const RuntimeMemoryHelperContract &Contract,
    const RuntimeProofToken &Proofs) {
  return Contract.Valid &&
         (Contract.Requirements & Proofs.bits()) == Contract.Requirements;
}

inline RuntimeMemoryHelperContract
getRuntimeMemoryHelperContract(RuntimeMemoryHelperId Helper) {
  RuntimeMemoryHelperContract Contract;
  auto RequirePreparedRange = [&Contract]() {
    Contract.require(RuntimeProofRequirementFlag::LogicalSize);
    Contract.require(RuntimeProofRequirementFlag::AccessRange);
  };
  auto RequireObservableOrder = [&Contract]() {
    Contract.require(RuntimeProofRequirementFlag::OrderToken);
    Contract.Effects.add(MemoryEffectFlag::RequiresOrderToken);
  };

  switch (Helper) {
  case RuntimeMemoryHelperId::Unknown:
    return Contract;

  case RuntimeMemoryHelperId::CodeCopyNoExpand:
  case RuntimeMemoryHelperId::CallDataCopyNoExpand:
    Contract.Valid = true;
    RequirePreparedRange();
    Contract.require(RuntimeProofRequirementFlag::DynamicGasCharged);
    Contract.Effects.add(MemoryEffectFlag::WritesMemory);
    break;

  case RuntimeMemoryHelperId::KeccakNoExpand:
    Contract.Valid = true;
    RequirePreparedRange();
    Contract.require(RuntimeProofRequirementFlag::DynamicGasCharged);
    Contract.Effects.add(MemoryEffectFlag::ReadsMemory);
    break;

  case RuntimeMemoryHelperId::TwoWordKeccakNoExpand:
    Contract.Valid = true;
    RequirePreparedRange();
    Contract.Effects.add(MemoryEffectFlag::ReadsMemory);
    Contract.Effects.add(MemoryEffectFlag::WritesMemory);
    Contract.Effects.add(MemoryEffectFlag::ObservesGas);
    Contract.Effects.add(MemoryEffectFlag::ChargesDynamicGas);
    Contract.Effects.add(MemoryEffectFlag::MayExhaustGas);
    Contract.Effects.add(MemoryEffectFlag::MayTrapOrHalt);
    RequireObservableOrder();
    break;

  case RuntimeMemoryHelperId::ReturnNoExpand:
  case RuntimeMemoryHelperId::RevertNoExpand:
    Contract.Valid = true;
    RequirePreparedRange();
    Contract.Effects.add(MemoryEffectFlag::ReadsMemory);
    Contract.Effects.add(MemoryEffectFlag::ExternalizesMemory);
    Contract.Effects.add(MemoryEffectFlag::TerminatesFrame);
    RequireObservableOrder();
    break;

  case RuntimeMemoryHelperId::ExpandMemoryNoGas:
    Contract.Valid = true;
    Contract.require(RuntimeProofRequirementFlag::DynamicGasCharged);
    Contract.require(RuntimeProofRequirementFlag::BoundsValidated);
    Contract.Effects.add(MemoryEffectFlag::MayGrowMemory);
    Contract.Effects.add(MemoryEffectFlag::MayRebaseMemory);
    Contract.Effects.add(MemoryEffectFlag::MayTrapOrHalt);
    RequireObservableOrder();
    Contract.EstablishesLogicalSize = true;
    break;

  case RuntimeMemoryHelperId::CallNoExpand:
    Contract.Valid = true;
    Contract.require(RuntimeProofRequirementFlag::LogicalSize);
    Contract.require(RuntimeProofRequirementFlag::CallArgumentsRange);
    Contract.require(RuntimeProofRequirementFlag::CallReturnRange);
    Contract.Effects.add(MemoryEffectFlag::ReadsMemory);
    Contract.Effects.add(MemoryEffectFlag::WritesMemory);
    Contract.Effects.add(MemoryEffectFlag::ObservesGas);
    Contract.Effects.add(MemoryEffectFlag::ChargesDynamicGas);
    Contract.Effects.add(MemoryEffectFlag::MayExhaustGas);
    Contract.Effects.add(MemoryEffectFlag::MayRebaseMemory);
    Contract.Effects.add(MemoryEffectFlag::MayTrapOrHalt);
    Contract.Effects.add(MemoryEffectFlag::ExternalizesMemory);
    RequireObservableOrder();
    break;
  }

  return Contract;
}

} // namespace COMPILER

#endif // COMPILER_EVM_FRONTEND_EVM_RUNTIME_HELPER_EFFECTS_H
