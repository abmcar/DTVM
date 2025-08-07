// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "evm/merkle_patricia_trie.h"
#include "host/evm/crypto.h"
#include <algorithm>
#include <cassert>
#include <cstring>

namespace zen::evm {

// RLP encoding constants
const uint8_t RLP_OFFSET_SHORT_STRING = 0x80;
const uint8_t RLP_OFFSET_SHORT_LIST = 0xc0;

// Empty node hash (Keccak256 of empty string)
static const std::vector<uint8_t> EMPTY_NODE_HASH = {
    0x56, 0xe8, 0x1f, 0x17, 0x1b, 0xcc, 0x55, 0xa6, 0xff, 0x83, 0x45,
    0xe6, 0x92, 0xc0, 0xf8, 0x6e, 0x5b, 0x48, 0xe0, 0x1b, 0x99, 0x6c,
    0xad, 0xc0, 0x01, 0x62, 0x2f, 0xb5, 0xe3, 0x63, 0xb4, 0x21};

namespace {
// Safe type casting helper functions to replace dynamic_cast
template <typename T>
std::shared_ptr<T> safeCast(std::shared_ptr<Node> Node, NodeType ExpectedType) {
  if (Node && Node->getType() == ExpectedType) {
    return std::static_pointer_cast<T>(Node);
  }
  return nullptr;
}

// Convenience functions
std::shared_ptr<LeafNode> asLeafNode(std::shared_ptr<Node> Node) {
  return safeCast<LeafNode>(Node, NodeType::Leaf);
}

std::shared_ptr<BranchNode> asBranchNode(std::shared_ptr<Node> Node) {
  return safeCast<BranchNode>(Node, NodeType::Branch);
}

std::shared_ptr<ExtensionNode> asExtensionNode(std::shared_ptr<Node> Node) {
  return safeCast<ExtensionNode>(Node, NodeType::Extension);
}

// RLP encoding helper functions
std::vector<uint8_t> encodeLength(size_t Length, uint8_t Offset) {
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

std::vector<uint8_t> encodeString(const std::vector<uint8_t> &Input) {
  if (Input.empty()) {
    return {RLP_OFFSET_SHORT_STRING};
  }

  if (Input.size() == 1 && Input[0] < RLP_OFFSET_SHORT_STRING) {
    return Input;
  }

  auto LengthBytes = encodeLength(Input.size(), RLP_OFFSET_SHORT_STRING);
  LengthBytes.insert(LengthBytes.end(), Input.begin(), Input.end());
  return LengthBytes;
}

std::vector<uint8_t>
encodeList(const std::vector<std::vector<uint8_t>> &Items) {
  std::vector<uint8_t> Payload;
  for (const auto &Item : Items) {
    auto Encoded = encodeString(Item);
    Payload.insert(Payload.end(), Encoded.begin(), Encoded.end());
  }

  auto LengthBytes = encodeLength(Payload.size(), RLP_OFFSET_SHORT_LIST);
  LengthBytes.insert(LengthBytes.end(), Payload.begin(), Payload.end());
  return LengthBytes;
}
} // anonymous namespace

// Nibbles utility functions implementation
namespace nibbles {

std::pair<Nibble, Nibble> fromByte(uint8_t Byte) {
  return {(Byte >> 4) & 0x0F, Byte & 0x0F};
}

Nibbles fromBytes(const std::vector<uint8_t> &Bytes) {
  Nibbles Result;
  Result.reserve(Bytes.size() * 2);

  for (uint8_t Byte : Bytes) {
    auto [High, Low] = fromByte(Byte);
    Result.push_back(High);
    Result.push_back(Low);
  }

  return Result;
}

Nibbles fromString(const std::string &Str) {
  return fromBytes(std::vector<uint8_t>(Str.begin(), Str.end()));
}

std::vector<uint8_t> toPrefixed(const Nibbles &Nibbles, bool IsLeaf) {
  std::vector<uint8_t> Result;

  uint8_t Prefix = 0;
  if (IsLeaf) {
    Prefix += 2; // Set leaf flag
  }

  if (Nibbles.size() % 2 == 1) {
    Prefix += 1; // Set odd flag
    Result.push_back((Prefix << 4) | Nibbles[0]);

    // Process remaining nibbles in pairs
    for (size_t I = 1; I < Nibbles.size(); I += 2) {
      uint8_t Byte = (Nibbles[I] << 4);
      if (I + 1 < Nibbles.size()) {
        Byte |= Nibbles[I + 1];
      }
      Result.push_back(Byte);
    }
  } else {
    Result.push_back(Prefix << 4);

    // Process nibbles in pairs
    for (size_t I = 0; I < Nibbles.size(); I += 2) {
      uint8_t Byte = (Nibbles[I] << 4);
      if (I + 1 < Nibbles.size()) {
        Byte |= Nibbles[I + 1];
      }
      Result.push_back(Byte);
    }
  }

  return Result;
}

std::vector<uint8_t> toBytes(const Nibbles &Nibbles) {
  assert(Nibbles.size() % 2 == 0 && "Nibbles length must be even");

  std::vector<uint8_t> Result;
  Result.reserve(Nibbles.size() / 2);

  for (size_t I = 0; I < Nibbles.size(); I += 2) {
    uint8_t Byte = (Nibbles[I] << 4) | Nibbles[I + 1];
    Result.push_back(Byte);
  }

  return Result;
}

size_t commonPrefixLength(const Nibbles &A, const Nibbles &B) {
  size_t MinLen = std::min(A.size(), B.size());
  size_t I = 0;

  while (I < MinLen && A[I] == B[I]) {
    I++;
  }

  return I;
}

Nibbles subslice(const Nibbles &NibblesData, size_t Start, size_t End) {
  if (End == SIZE_MAX) {
    End = NibblesData.size();
  }

  assert(Start <= End && End <= NibblesData.size());

  return Nibbles(NibblesData.begin() + Start, NibblesData.begin() + End);
}

} // namespace nibbles

// EmptyNode implementation
std::shared_ptr<EmptyNode> EmptyNode::Instance = nullptr;

std::shared_ptr<EmptyNode> EmptyNode::getInstance() {
  if (!Instance) {
    Instance = std::shared_ptr<EmptyNode>(new EmptyNode());
  }
  return Instance;
}

std::vector<uint8_t> EmptyNode::hash() const { return EMPTY_NODE_HASH; }

std::vector<uint8_t> EmptyNode::serialize() const {
  return {RLP_OFFSET_SHORT_STRING}; // Empty string in RLP
}

// LeafNode implementation
LeafNode::LeafNode(const Nibbles &Path, const std::vector<uint8_t> &Value)
    : Path(Path), Value(Value) {}

std::shared_ptr<LeafNode>
LeafNode::fromKeyValue(const std::vector<uint8_t> &Key,
                       const std::vector<uint8_t> &Value) {
  return std::make_shared<LeafNode>(nibbles::fromBytes(Key), Value);
}

std::vector<uint8_t> LeafNode::hash() const {
  auto Serialized = serialize();
  return zen::host::evm::crypto::keccak256(Serialized);
}

std::vector<uint8_t> LeafNode::serialize() const {
  std::vector<std::vector<uint8_t>> Items;
  Items.push_back(nibbles::toPrefixed(Path, true)); // isLeaf = true
  Items.push_back(Value);

  return encodeList(Items);
}

// BranchNode implementation
BranchNode::BranchNode() {
  // Initialize all branches to empty nodes
  for (auto &Branch : Branches) {
    Branch = EmptyNode::getInstance();
  }
}

void BranchNode::setBranch(Nibble Index, std::shared_ptr<Node> Node) {
  assert(Index < 16);
  Branches[Index] = Node ? Node : EmptyNode::getInstance();
}

void BranchNode::removeBranch(Nibble Index) {
  assert(Index < 16);
  Branches[Index] = EmptyNode::getInstance();
}

void BranchNode::setValue(const std::vector<uint8_t> &Val) { Value = Val; }

void BranchNode::removeValue() { Value.reset(); }

bool BranchNode::hasContent() const {
  if (Value.has_value()) {
    return true;
  }

  for (const auto &Branch : Branches) {
    if (!Branch->isEmpty()) {
      return true;
    }
  }

  return false;
}

size_t BranchNode::branchCount() const {
  size_t Count = 0;
  for (const auto &Branch : Branches) {
    if (!Branch->isEmpty()) {
      Count++;
    }
  }
  return Count;
}

std::optional<Nibble> BranchNode::getSingleBranch() const {
  std::optional<Nibble> Result;

  for (Nibble I = 0; I < 16; I++) {
    if (!Branches[I]->isEmpty()) {
      if (Result.has_value()) {
        return std::nullopt; // More than one branch
      }
      Result = I;
    }
  }

  return Result;
}

std::vector<uint8_t> BranchNode::hash() const {
  auto Serialized = serialize();
  return zen::host::evm::crypto::keccak256(Serialized);
}

std::vector<uint8_t> BranchNode::serialize() const {
  std::vector<std::vector<uint8_t>> Items;

  // Add all 16 branches
  for (const auto &Branch : Branches) {
    if (Branch->isEmpty()) {
      Items.push_back({}); // Empty string for empty nodes
    } else {
      auto BranchHash = Branch->hash();
      if (BranchHash.size() < 32) {
        // Small node, embed directly
        Items.push_back(Branch->serialize());
      } else {
        // Large node, reference by hash
        Items.push_back(BranchHash);
      }
    }
  }

  // Add value (empty if no value)
  Items.push_back(Value.value_or(std::vector<uint8_t>{}));

  return encodeList(Items);
}

// ExtensionNode implementation
ExtensionNode::ExtensionNode(const Nibbles &Path, std::shared_ptr<Node> Next)
    : Path(Path), Next(Next) {}

std::vector<uint8_t> ExtensionNode::hash() const {
  auto Serialized = serialize();
  return zen::host::evm::crypto::keccak256(Serialized);
}

std::vector<uint8_t> ExtensionNode::serialize() const {
  std::vector<std::vector<uint8_t>> Items;
  Items.push_back(nibbles::toPrefixed(Path, false)); // isLeaf = false

  if (Next->isEmpty()) {
    Items.push_back({}); // Empty string for empty nodes
  } else {
    auto NextHash = Next->hash();
    if (NextHash.size() < 32) {
      // Small node, embed directly
      Items.push_back(Next->serialize());
    } else {
      // Large node, reference by hash
      Items.push_back(NextHash);
    }
  }

  return encodeList(Items);
}

// MerklePatriciaTrie implementation
MerklePatriciaTrie::MerklePatriciaTrie() { Root = EmptyNode::getInstance(); }

std::optional<std::vector<uint8_t>>
MerklePatriciaTrie::get(const std::vector<uint8_t> &Key) const {
  Nibbles KeyNibbles = nibbles::fromBytes(Key);
  return getWithPath(Root, KeyNibbles);
}

std::optional<std::vector<uint8_t>>
MerklePatriciaTrie::getWithPath(std::shared_ptr<Node> Node,
                                const Nibbles &Key) const {
  if (Node->isEmpty()) {
    return std::nullopt;
  }

  if (auto LeafNode = asLeafNode(Node)) {
    if (LeafNode->Path == Key) {
      return LeafNode->Value;
    }
    return std::nullopt;
  }

  if (auto BranchNode = asBranchNode(Node)) {
    if (Key.empty()) {
      return BranchNode->Value;
    }
    Nibble Index = Key[0];
    Nibbles RemainingKey = nibbles::subslice(Key, 1);
    return getWithPath(BranchNode->Branches[Index], RemainingKey);
  }

  if (auto ExtensionNode = asExtensionNode(Node)) {
    size_t MatchedLen = nibbles::commonPrefixLength(Key, ExtensionNode->Path);

    if (MatchedLen == ExtensionNode->Path.size()) {
      // Full match with extension path
      Nibbles RemainingKey = nibbles::subslice(Key, MatchedLen);
      return getWithPath(ExtensionNode->Next, RemainingKey);
    }
    // Partial match, key doesn't exist
    return std::nullopt;
  }

  return std::nullopt;
}

void MerklePatriciaTrie::put(const std::vector<uint8_t> &Key,
                             const std::vector<uint8_t> &Value) {
  Nibbles KeyNibbles = nibbles::fromBytes(Key);
  Root = put(Root, KeyNibbles, Value);
}

bool MerklePatriciaTrie::remove(const std::vector<uint8_t> &Key) {
  Nibbles KeyNibbles = nibbles::fromBytes(Key);
  auto OldRoot = Root;
  Root = remove(Root, KeyNibbles);
  return Root != OldRoot;
}

std::vector<uint8_t> MerklePatriciaTrie::rootHash() const {
  return Root->hash();
}

bool MerklePatriciaTrie::empty() const { return Root->isEmpty(); }

// Internal get implementation
std::shared_ptr<Node> MerklePatriciaTrie::get(std::shared_ptr<Node> Node,
                                              const Nibbles &Key) const {
  if (Node->isEmpty() || Key.empty()) {
    return Node;
  }

  if (auto LeafNode = asLeafNode(Node)) {
    return Node; // Return leaf node, caller checks if path matches
  }

  if (auto BranchNode = asBranchNode(Node)) {
    Nibble Index = Key[0];
    Nibbles RemainingKey = nibbles::subslice(Key, 1);
    return get(BranchNode->Branches[Index], RemainingKey);
  }

  if (auto ExtensionNode = asExtensionNode(Node)) {
    size_t MatchedLen = nibbles::commonPrefixLength(Key, ExtensionNode->Path);

    if (MatchedLen == ExtensionNode->Path.size()) {
      // Full match with extension path
      Nibbles RemainingKey = nibbles::subslice(Key, MatchedLen);
      return get(ExtensionNode->Next, RemainingKey);
    }
    // Partial match, key doesn't exist
    return EmptyNode::getInstance();
  }

  return EmptyNode::getInstance();
}

// Internal put implementation
std::shared_ptr<Node>
MerklePatriciaTrie::put(std::shared_ptr<Node> Node, const Nibbles &Key,
                        const std::vector<uint8_t> &Value) {
  if (Node->isEmpty()) {
    return std::make_shared<LeafNode>(Key, Value);
  }

  if (auto LeafNode = asLeafNode(Node)) {
    return putInLeaf(LeafNode, Key, Value);
  }

  if (auto BranchNode = asBranchNode(Node)) {
    return putInBranch(BranchNode, Key, Value);
  }

  if (auto ExtensionNode = asExtensionNode(Node)) {
    return putInExtension(ExtensionNode, Key, Value);
  }

  return Node;
}

std::shared_ptr<Node>
MerklePatriciaTrie::putInLeaf(std::shared_ptr<LeafNode> Leaf,
                              const Nibbles &Key,
                              const std::vector<uint8_t> &Value) {
  size_t MatchedLen = nibbles::commonPrefixLength(Key, Leaf->Path);

  if (MatchedLen == Leaf->Path.size() && MatchedLen == Key.size()) {
    // Exact match, update value
    return std::make_shared<LeafNode>(Key, Value);
  }

  // Create branch node to split paths
  auto Branch = std::make_shared<BranchNode>();

  if (MatchedLen == Leaf->Path.size()) {
    // Leaf path is prefix of key
    Branch->setValue(Leaf->Value);
    Nibbles RemainingKey = nibbles::subslice(Key, MatchedLen);
    if (!RemainingKey.empty()) {
      Nibble Index = RemainingKey[0];
      Nibbles NewKey = nibbles::subslice(RemainingKey, 1);
      Branch->setBranch(Index, std::make_shared<LeafNode>(NewKey, Value));
    }
  } else if (MatchedLen == Key.size()) {
    // Key is prefix of leaf path
    Branch->setValue(Value);
    Nibbles RemainingPath = nibbles::subslice(Leaf->Path, MatchedLen);
    Nibble Index = RemainingPath[0];
    Nibbles NewPath = nibbles::subslice(RemainingPath, 1);
    Branch->setBranch(Index, std::make_shared<LeafNode>(NewPath, Leaf->Value));
  } else {
    // Both have remaining parts after common prefix
    Nibbles LeafRemaining = nibbles::subslice(Leaf->Path, MatchedLen);
    Nibbles KeyRemaining = nibbles::subslice(Key, MatchedLen);

    Nibble LeafIndex = LeafRemaining[0];
    Nibble KeyIndex = KeyRemaining[0];

    Nibbles NewLeafPath = nibbles::subslice(LeafRemaining, 1);
    Nibbles NewKeyPath = nibbles::subslice(KeyRemaining, 1);

    Branch->setBranch(LeafIndex,
                      std::make_shared<LeafNode>(NewLeafPath, Leaf->Value));
    Branch->setBranch(KeyIndex, std::make_shared<LeafNode>(NewKeyPath, Value));
  }

  if (MatchedLen > 0) {
    // Create extension node for common prefix
    Nibbles CommonPrefix = nibbles::subslice(Key, 0, MatchedLen);
    return std::make_shared<ExtensionNode>(CommonPrefix, Branch);
  }

  return Branch;
}

std::shared_ptr<Node>
MerklePatriciaTrie::putInBranch(std::shared_ptr<BranchNode> Branch,
                                const Nibbles &Key,
                                const std::vector<uint8_t> &Value) {
  if (Key.empty()) {
    // Set value at this branch node
    auto NewBranch = std::make_shared<BranchNode>(*Branch);
    NewBranch->setValue(Value);
    return NewBranch;
  }

  Nibble Index = Key[0];
  Nibbles RemainingKey = nibbles::subslice(Key, 1);

  auto NewBranch = std::make_shared<BranchNode>(*Branch);
  auto NewChild = put(Branch->Branches[Index], RemainingKey, Value);
  NewBranch->setBranch(Index, NewChild);

  return NewBranch;
}

std::shared_ptr<Node>
MerklePatriciaTrie::putInExtension(std::shared_ptr<ExtensionNode> Ext,
                                   const Nibbles &Key,
                                   const std::vector<uint8_t> &Value) {
  size_t MatchedLen = nibbles::commonPrefixLength(Key, Ext->Path);

  if (MatchedLen == Ext->Path.size()) {
    // Full match with extension path
    Nibbles RemainingKey = nibbles::subslice(Key, MatchedLen);
    auto NewNext = put(Ext->Next, RemainingKey, Value);
    return std::make_shared<ExtensionNode>(Ext->Path, NewNext);
  }

  // Partial match, need to split extension
  auto Branch = std::make_shared<BranchNode>();

  Nibbles CommonPrefix = nibbles::subslice(Key, 0, MatchedLen);
  Nibbles ExtRemaining = nibbles::subslice(Ext->Path, MatchedLen);
  Nibbles KeyRemaining = nibbles::subslice(Key, MatchedLen);

  if (ExtRemaining.size() == 1) {
    // Extension remainder is single nibble, put next directly in branch
    Nibble ExtIndex = ExtRemaining[0];
    Branch->setBranch(ExtIndex, Ext->Next);
  } else {
    // Extension remainder is multiple nibbles, create new extension
    Nibble ExtIndex = ExtRemaining[0];
    Nibbles NewExtPath = nibbles::subslice(ExtRemaining, 1);
    Branch->setBranch(ExtIndex,
                      std::make_shared<ExtensionNode>(NewExtPath, Ext->Next));
  }

  if (KeyRemaining.empty()) {
    // Key ends at branch
    Branch->setValue(Value);
  } else {
    // Key continues past branch
    Nibble KeyIndex = KeyRemaining[0];
    Nibbles NewKeyPath = nibbles::subslice(KeyRemaining, 1);
    Branch->setBranch(KeyIndex, std::make_shared<LeafNode>(NewKeyPath, Value));
  }

  if (MatchedLen > 0) {
    return std::make_shared<ExtensionNode>(CommonPrefix, Branch);
  }

  return Branch;
}

// Internal remove implementation
std::shared_ptr<Node> MerklePatriciaTrie::remove(std::shared_ptr<Node> Node,
                                                 const Nibbles &Key) {
  if (Node->isEmpty()) {
    return Node;
  }

  if (auto LeafNode = asLeafNode(Node)) {
    if (LeafNode->Path == Key) {
      return EmptyNode::getInstance();
    }
    return Node; // Key not found
  }

  if (auto BranchNodePtr = asBranchNode(Node)) {
    if (Key.empty()) {
      // Remove value from branch node
      auto NewBranch = std::make_shared<BranchNode>(*BranchNodePtr);
      NewBranch->removeValue();

      // Check if branch can be simplified
      size_t BranchCount = NewBranch->branchCount();
      if (BranchCount == 0) {
        return EmptyNode::getInstance();
      }
      if (BranchCount == 1 && !NewBranch->Value.has_value()) {
        // Convert to extension or leaf
        auto SingleBranch = NewBranch->getSingleBranch();
        if (SingleBranch.has_value()) {
          auto Child = NewBranch->Branches[*SingleBranch];
          if (auto LeafChild = asLeafNode(Child)) {
            Nibbles NewPath = {*SingleBranch};
            NewPath.insert(NewPath.end(), LeafChild->Path.begin(),
                           LeafChild->Path.end());
            return std::make_shared<LeafNode>(NewPath, LeafChild->Value);
          }
          if (auto ExtChild = asExtensionNode(Child)) {
            Nibbles NewPath = {*SingleBranch};
            NewPath.insert(NewPath.end(), ExtChild->Path.begin(),
                           ExtChild->Path.end());
            return std::make_shared<ExtensionNode>(NewPath, ExtChild->Next);
          }
        }
      }

      return NewBranch;
    }

    Nibble Index = Key[0];
    Nibbles RemainingKey = nibbles::subslice(Key, 1);

    auto NewChild = remove(BranchNodePtr->Branches[Index], RemainingKey);
    if (NewChild == BranchNodePtr->Branches[Index]) {
      return Node; // Nothing changed
    }

    auto NewBranch = std::make_shared<BranchNode>(*BranchNodePtr);
    NewBranch->setBranch(Index, NewChild);

    // Check if branch can be simplified after removal
    size_t BranchCount = NewBranch->branchCount();
    if (BranchCount == 0 && !NewBranch->Value.has_value()) {
      return EmptyNode::getInstance();
    }
    if (BranchCount == 1 && !NewBranch->Value.has_value()) {
      auto SingleBranch = NewBranch->getSingleBranch();
      if (SingleBranch.has_value()) {
        auto Child = NewBranch->Branches[*SingleBranch];
        if (auto LeafChild = asLeafNode(Child)) {
          Nibbles NewPath = {*SingleBranch};
          NewPath.insert(NewPath.end(), LeafChild->Path.begin(),
                         LeafChild->Path.end());
          return std::make_shared<LeafNode>(NewPath, LeafChild->Value);
        }
        if (auto ExtChild = asExtensionNode(Child)) {
          Nibbles NewPath = {*SingleBranch};
          NewPath.insert(NewPath.end(), ExtChild->Path.begin(),
                         ExtChild->Path.end());
          return std::make_shared<ExtensionNode>(NewPath, ExtChild->Next);
        }
      }
    }

    return NewBranch;
  }

  if (auto ExtensionNodePtr = asExtensionNode(Node)) {
    size_t MatchedLen =
        nibbles::commonPrefixLength(Key, ExtensionNodePtr->Path);

    if (MatchedLen < ExtensionNodePtr->Path.size()) {
      return Node; // Key doesn't match extension path
    }

    Nibbles RemainingKey = nibbles::subslice(Key, MatchedLen);
    auto NewNext = remove(ExtensionNodePtr->Next, RemainingKey);

    if (NewNext == ExtensionNodePtr->Next) {
      return Node; // Nothing changed
    }

    if (NewNext->isEmpty()) {
      return EmptyNode::getInstance();
    }

    // Check if extension can be merged with child
    if (auto LeafNext = asLeafNode(NewNext)) {
      Nibbles NewPath = ExtensionNodePtr->Path;
      NewPath.insert(NewPath.end(), LeafNext->Path.begin(),
                     LeafNext->Path.end());
      return std::make_shared<LeafNode>(NewPath, LeafNext->Value);
    }

    if (auto ExtNext = asExtensionNode(NewNext)) {
      Nibbles NewPath = ExtensionNodePtr->Path;
      NewPath.insert(NewPath.end(), ExtNext->Path.begin(), ExtNext->Path.end());
      return std::make_shared<ExtensionNode>(NewPath, ExtNext->Next);
    }

    return std::make_shared<ExtensionNode>(ExtensionNodePtr->Path, NewNext);
  }

  return Node;
}

} // namespace zen::evm
