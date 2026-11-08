#ifndef EAGKA_KEY_GENERATION_TREE_H
#define EAGKA_KEY_GENERATION_TREE_H

#include <cstdint>
#include <map>
#include <vector>

typedef std::vector<uint8_t> ValueTag;
struct NodeRecord {
  uint32_t id;
  ValueTag value;
};

class KeyGenerationTree {
public:
  explicit KeyGenerationTree(uint32_t n = 0);
  void Initialize(uint32_t n);
  void InsertLeaf(uint32_t k, const ValueTag& raw);
  void MergeNode(uint32_t id, const ValueTag& value);
  std::vector<uint32_t> CoveredLeaves() const;
  std::vector<uint32_t> CoverageOf(uint32_t id) const;
  std::vector<NodeRecord> Aggregate(const std::vector<uint32_t>& leaves) const;
  bool HasRootValue() const;
  ValueTag ValueTagAtRoot() const;
  uint32_t LeafNodeId(uint32_t k) const;
  NodeRecord HighestAvailableAncestor(uint32_t k) const;
  static ValueTag MakeContribution(uint32_t id, uint64_t session);
  static ValueTag ComputeRootFromLeaves(const std::vector<ValueTag>& leaves);

private:
  uint32_t m_size;
  uint32_t m_base;
  std::map<uint32_t, ValueTag> m_values;
  void ValidateNode(uint32_t id) const;
  void StoreAndPropagate(uint32_t id, const ValueTag& value);
};

#endif
