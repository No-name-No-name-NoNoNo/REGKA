#include "KeyGenerationTree.h"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
ValueTag Mix(uint8_t domain, const ValueTag& input) {
  uint64_t state = UINT64_C(14695981039346656037) ^ domain;
  for (size_t i = 0; i < input.size(); ++i) {
    state ^= input[i];
    state *= UINT64_C(1099511628211);
  }
  ValueTag result(32);
  for (size_t i = 0; i < result.size(); ++i) {
    state += UINT64_C(0x9e3779b97f4a7c15);
    uint64_t z = state;
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    result[i] = static_cast<uint8_t>((z ^ (z >> 31)) >> 56);
  }
  return result;
}
ValueTag Parent(const ValueTag& left, const ValueTag& right) {
  ValueTag joined(left);
  joined.insert(joined.end(), right.begin(), right.end());
  return Mix(2, joined);
}
void RequireTag(const ValueTag& value) {
  if (value.size() != 32) throw std::invalid_argument("tag must contain 32 bytes");
}
}

KeyGenerationTree::KeyGenerationTree(uint32_t n) { Initialize(n); }

void KeyGenerationTree::Initialize(uint32_t n) {
  if (n > (UINT32_C(1) << 31)) throw std::invalid_argument("tree too large");
  m_size = n;
  m_base = 1;
  while (m_base < n) m_base <<= 1;
  m_values.clear();
  const ValueTag padding = Mix(3, ValueTag(32, 0));
  if (n != 0) {
    for (uint64_t k = n; k < m_base; ++k)
      StoreAndPropagate(static_cast<uint32_t>(m_base + k), padding);
  }
}

void KeyGenerationTree::ValidateNode(uint32_t id) const {
  if (m_size == 0 || id == 0 || uint64_t(id) >= uint64_t(m_base) * 2)
    throw std::out_of_range("invalid heap node");
}

uint32_t KeyGenerationTree::LeafNodeId(uint32_t k) const {
  if (k >= m_size) throw std::out_of_range("invalid contribution index");
  return m_base + k;
}

void KeyGenerationTree::InsertLeaf(uint32_t k, const ValueTag& raw) {
  RequireTag(raw);
  StoreAndPropagate(LeafNodeId(k), Mix(1, raw));
}

void KeyGenerationTree::MergeNode(uint32_t id, const ValueTag& value) {
  ValidateNode(id);
  RequireTag(value);
  StoreAndPropagate(id, value);
}

void KeyGenerationTree::StoreAndPropagate(uint32_t id, const ValueTag& value) {
  std::vector<NodeRecord> pending;
  ValueTag current(value);
  while (true) {
    std::map<uint32_t, ValueTag>::const_iterator existing = m_values.find(id);
    if (existing != m_values.end()) {
      if (existing->second != current) throw std::invalid_argument("conflicting node value");
      break;
    }
    pending.push_back(NodeRecord{id, current});
    if (id == 1) break;
    std::map<uint32_t, ValueTag>::const_iterator sibling = m_values.find(id ^ 1U);
    if (sibling == m_values.end()) break;
    current = (id & 1U) ? Parent(sibling->second, current) : Parent(current, sibling->second);
    id /= 2;
  }
  for (size_t i = 0; i < pending.size(); ++i)
    m_values.insert(std::make_pair(pending[i].id, pending[i].value));
}

std::vector<uint32_t> KeyGenerationTree::CoverageOf(uint32_t id) const {
  ValidateNode(id);
  uint64_t first = id, last = id;
  while (first < m_base) { first *= 2; last = last * 2 + 1; }
  std::vector<uint32_t> result;
  for (uint64_t leaf = first; leaf <= last && leaf < uint64_t(m_base) + m_size; ++leaf)
    result.push_back(static_cast<uint32_t>(leaf - m_base));
  return result;
}

NodeRecord KeyGenerationTree::HighestAvailableAncestor(uint32_t k) const {
  uint32_t id = LeafNodeId(k);
  NodeRecord best = {0, ValueTag()};
  for (; id != 0; id /= 2) {
    std::map<uint32_t, ValueTag>::const_iterator it = m_values.find(id);
    if (it != m_values.end()) best = NodeRecord{id, it->second};
  }
  if (best.id == 0) throw std::out_of_range("contribution is not covered");
  return best;
}

std::vector<uint32_t> KeyGenerationTree::CoveredLeaves() const {
  std::vector<uint32_t> result;
  for (uint32_t k = 0; k < m_size; ++k) {
    for (uint32_t id = m_base + k; id != 0; id /= 2) {
      if (m_values.count(id)) { result.push_back(k); break; }
    }
  }
  return result;
}

std::vector<NodeRecord> KeyGenerationTree::Aggregate(const std::vector<uint32_t>& leaves) const {
  std::set<uint32_t> ids;
  for (size_t i = 0; i < leaves.size(); ++i) ids.insert(HighestAvailableAncestor(leaves[i]).id);
  std::vector<NodeRecord> result;
  for (std::set<uint32_t>::const_iterator it = ids.begin(); it != ids.end(); ++it) {
    bool subsumed = false;
    for (uint32_t parent = *it / 2; parent; parent /= 2)
      if (ids.count(parent)) { subsumed = true; break; }
    if (!subsumed) result.push_back(NodeRecord{*it, m_values.at(*it)});
  }
  return result;
}

bool KeyGenerationTree::HasRootValue() const { return m_values.count(1) != 0; }
ValueTag KeyGenerationTree::ValueTagAtRoot() const {
  if (!HasRootValue()) throw std::out_of_range("root unavailable");
  return m_values.at(1);
}

ValueTag KeyGenerationTree::MakeContribution(uint32_t id, uint64_t session) {
  ValueTag input;
  for (int shift = 56; shift >= 0; shift -= 8) input.push_back(static_cast<uint8_t>(session >> shift));
  for (int shift = 24; shift >= 0; shift -= 8) input.push_back(static_cast<uint8_t>(id >> shift));
  return Mix(4, input);
}

ValueTag KeyGenerationTree::ComputeRootFromLeaves(const std::vector<ValueTag>& leaves) {
  if (leaves.size() > std::numeric_limits<uint32_t>::max()) throw std::length_error("too many leaves");
  KeyGenerationTree tree(static_cast<uint32_t>(leaves.size()));
  for (size_t k = 0; k < leaves.size(); ++k) tree.InsertLeaf(static_cast<uint32_t>(k), leaves[k]);
  return tree.ValueTagAtRoot();
}
