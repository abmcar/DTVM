// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef ZEN_EVM_MERKLE_PATRICIA_TRIE_H
#define ZEN_EVM_MERKLE_PATRICIA_TRIE_H

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zen::evm {

// Nibble type (4-bit value)
using Nibble = uint8_t;
using Nibbles = std::vector<Nibble>;

// Forward declarations
class Node;
class EmptyNode;
class LeafNode;
class BranchNode;
class ExtensionNode;

// Node type enumeration
enum class NodeType { Empty, Leaf, Branch, Extension };

// Node interface
class Node {
public:
  virtual ~Node() = default;
  virtual std::vector<uint8_t> hash() const = 0;
  virtual std::vector<uint8_t> serialize() const = 0;
  virtual NodeType getType() const = 0;
  virtual bool isEmpty() const { return false; }
};

// Empty node (null node)
class EmptyNode : public Node {
public:
  std::vector<uint8_t> hash() const override;
  std::vector<uint8_t> serialize() const override;
  NodeType getType() const override { return NodeType::Empty; }
  bool isEmpty() const override { return true; }

  static std::shared_ptr<EmptyNode> getInstance();

private:
  static std::shared_ptr<EmptyNode> Instance;
};

// Leaf node: stores key-value pair at the end of a path
class LeafNode : public Node {
public:
  Nibbles Path;
  std::vector<uint8_t> Value;

  LeafNode(const Nibbles &Path, const std::vector<uint8_t> &Value);

  // Helper factory method for creating from key bytes
  static std::shared_ptr<LeafNode>
  fromKeyValue(const std::vector<uint8_t> &Key,
               const std::vector<uint8_t> &Value);

  std::vector<uint8_t> hash() const override;
  std::vector<uint8_t> serialize() const override;
  NodeType getType() const override { return NodeType::Leaf; }
};

// Branch node: has up to 16 children (for each hex digit) + optional value
class BranchNode : public Node {
public:
  std::array<std::shared_ptr<Node>, 16> Branches;
  std::optional<std::vector<uint8_t>> Value;

  BranchNode();

  void setBranch(Nibble Index, std::shared_ptr<Node> NodePtr);
  void removeBranch(Nibble Index);
  void setValue(const std::vector<uint8_t> &Val);
  void removeValue();

  std::vector<uint8_t> hash() const override;
  std::vector<uint8_t> serialize() const override;
  NodeType getType() const override { return NodeType::Branch; }

  // Check if node has any branches or value
  bool hasContent() const;

  // Count non-empty branches
  size_t branchCount() const;

  // Get the single branch index (if only one branch exists)
  std::optional<Nibble> getSingleBranch() const;
};

// Extension node: compress common path prefix
class ExtensionNode : public Node {
public:
  Nibbles Path;
  std::shared_ptr<Node> Next;

  ExtensionNode(const Nibbles &Path, std::shared_ptr<Node> Next);

  std::vector<uint8_t> hash() const override;
  std::vector<uint8_t> serialize() const override;
  NodeType getType() const override { return NodeType::Extension; }
};

// Nibbles utility functions
namespace nibbles {
// Convert byte to two nibbles
std::pair<Nibble, Nibble> fromByte(uint8_t Byte);

// Convert bytes to nibbles
Nibbles fromBytes(const std::vector<uint8_t> &Bytes);

// Convert string to nibbles
Nibbles fromString(const std::string &Str);

// Convert nibbles to prefixed bytes (for serialization)
std::vector<uint8_t> toPrefixed(const Nibbles &NibblesData, bool IsLeaf);

// Convert nibbles back to bytes (must be even length)
std::vector<uint8_t> toBytes(const Nibbles &NibblesData);

// Find common prefix length between two nibble arrays
size_t commonPrefixLength(const Nibbles &A, const Nibbles &B);

// Get subslice of nibbles
Nibbles subslice(const Nibbles &NibblesData, size_t Start,
                 size_t End = SIZE_MAX);
} // namespace nibbles

// Main Merkle Patricia Trie class
class MerklePatriciaTrie {
private:
  std::shared_ptr<Node> Root;

  // Internal recursive functions
  std::shared_ptr<Node> get(std::shared_ptr<Node> NodePtr,
                            const Nibbles &Key) const;

  std::optional<std::vector<uint8_t>> getWithPath(std::shared_ptr<Node> NodePtr,
                                                  const Nibbles &Key) const;

  std::shared_ptr<Node> put(std::shared_ptr<Node> NodePtr, const Nibbles &Key,
                            const std::vector<uint8_t> &Value);

  std::shared_ptr<Node> remove(std::shared_ptr<Node> NodePtr,
                               const Nibbles &Key);

  // Helper functions for node operations
  std::shared_ptr<Node> putInBranch(std::shared_ptr<BranchNode> Branch,
                                    const Nibbles &Key,
                                    const std::vector<uint8_t> &Value);

  std::shared_ptr<Node> putInExtension(std::shared_ptr<ExtensionNode> Ext,
                                       const Nibbles &Key,
                                       const std::vector<uint8_t> &Value);

  std::shared_ptr<Node> putInLeaf(std::shared_ptr<LeafNode> Leaf,
                                  const Nibbles &Key,
                                  const std::vector<uint8_t> &Value);

public:
  MerklePatriciaTrie();

  std::optional<std::vector<uint8_t>>
  get(const std::vector<uint8_t> &Key) const;
  void put(const std::vector<uint8_t> &Key, const std::vector<uint8_t> &Value);
  bool remove(const std::vector<uint8_t> &Key);

  std::vector<uint8_t> rootHash() const;

  bool empty() const;

  std::shared_ptr<Node> getRoot() const { return Root; }
};

} // namespace zen::evm

#endif // ZEN_EVM_MERKLE_PATRICIA_TRIE_H
