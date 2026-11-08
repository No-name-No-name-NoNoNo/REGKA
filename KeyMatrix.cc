#include "KeyMatrix.h"

#include <algorithm>
#include <stdexcept>

KeyMatrix::KeyMatrix(uint32_t n, uint32_t self)
  : m_networkSize(0), m_nodeId(0)
{
  Initialize(n, self);
}

void KeyMatrix::Initialize(uint32_t n, uint32_t self)
{
  if ((n == 0 && self != 0) || (n != 0 && self >= n)) {
    throw std::invalid_argument("KeyMatrix: invalid local node");
  }
  std::vector<std::vector<bool> > matrix(n, std::vector<bool>(n, false));
  for (uint32_t i = 0; i < n; ++i) {
    matrix[i][i] = true;
  }
  m_matrix.swap(matrix);
  m_networkSize = n;
  m_nodeId = self;
}

void KeyMatrix::SetLocalCoverage(const std::vector<uint32_t>& coverage)
{
  for (std::size_t i = 0; i < coverage.size(); ++i) {
    if (coverage[i] >= m_networkSize) {
      throw std::out_of_range("KeyMatrix: invalid contribution");
    }
  }
  if (m_networkSize == 0) {
    return;
  }
  std::vector<bool> row(m_networkSize, false);
  for (std::size_t i = 0; i < coverage.size(); ++i) {
    row[coverage[i]] = true;
  }
  m_matrix[m_nodeId].swap(row);
}

void KeyMatrix::MergeRemoteSnapshot(const std::vector<uint8_t>& snapshot)
{
  const std::size_t expected = static_cast<std::size_t>(m_networkSize) * m_networkSize;
  if (snapshot.size() != expected ||
      std::find_if(snapshot.begin(), snapshot.end(),
                   [](uint8_t value) { return value > 1; }) != snapshot.end()) {
    throw std::invalid_argument("KeyMatrix: invalid binary snapshot");
  }
  for (uint32_t i = 0; i < m_networkSize; ++i) {
    if (i == m_nodeId) {
      continue;
    }
    for (uint32_t j = 0; j < m_networkSize; ++j) {
      m_matrix[i][j] = m_matrix[i][j] ||
        snapshot[static_cast<std::size_t>(i) * m_networkSize + j] != 0;
    }
  }
}

double KeyMatrix::CalculateCR(uint32_t neighbor) const
{
  if (neighbor >= m_networkSize) {
    return 0.0;
  }
  uint32_t diffCount = 0;
  uint32_t unionCount = 0;
  for (uint32_t j = 0; j < m_networkSize; ++j) {
    if (m_matrix[m_nodeId][j] != m_matrix[neighbor][j]) {
      ++diffCount;
    }
    if (m_matrix[m_nodeId][j] || m_matrix[neighbor][j]) {
      ++unionCount;
    }
  }
  return unionCount == 0 ? 0.0 : static_cast<double>(diffCount) / unionCount;
}

double KeyMatrix::CalculatePD(uint32_t k) const
{
  if (m_networkSize <= 1 || k >= m_networkSize) {
    return 0.0;
  }
  uint32_t count = 0;
  for (uint32_t i = 0; i < m_networkSize; ++i) {
    count += m_matrix[i][k] ? 1 : 0;
  }
  return (static_cast<double>(count) - 1.0) / (m_networkSize - 1);
}

bool KeyMatrix::IsGlobalEstimateComplete() const
{
  if (m_networkSize == 0) {
    return false;
  }
  for (uint32_t i = 0; i < m_networkSize; ++i) {
    for (uint32_t j = 0; j < m_networkSize; ++j) {
      if (!m_matrix[i][j]) {
        return false;
      }
    }
  }
  return true;
}

std::vector<uint8_t> KeyMatrix::Snapshot() const
{
  std::vector<uint8_t> result;
  result.reserve(static_cast<std::size_t>(m_networkSize) * m_networkSize);
  for (uint32_t i = 0; i < m_networkSize; ++i) {
    for (uint32_t j = 0; j < m_networkSize; ++j) {
      result.push_back(m_matrix[i][j] ? 1 : 0);
    }
  }
  return result;
}

bool KeyMatrix::HasLocalCoverage(uint32_t k) const
{
  return HasCoverage(m_nodeId, k);
}

bool KeyMatrix::HasCoverage(uint32_t node, uint32_t k) const
{
  return node < m_networkSize && k < m_networkSize && m_matrix[node][k];
}

uint32_t KeyMatrix::Size() const
{
  return m_networkSize;
}

void KeyMatrix::InitializeMatrix(uint32_t networkSize, uint32_t nodeId)
{
  Initialize(networkSize, nodeId);
}

bool KeyMatrix::HasKeyContribution(uint32_t node, uint32_t k) const
{
  return HasCoverage(node, k);
}

void KeyMatrix::ReceiveKeyContribution(uint32_t contributorId)
{
  if (contributorId >= m_networkSize) {
    throw std::out_of_range("KeyMatrix: invalid contribution");
  }
  m_matrix[m_nodeId][contributorId] = true;
}

void KeyMatrix::MergeMatrix(const KeyMatrix& received)
{
  MergeRemoteSnapshot(received.Snapshot());
}

double KeyMatrix::CalculateFD(uint32_t contributorId) const
{
  return CalculatePD(contributorId);
}

bool KeyMatrix::IsFull1() const
{
  return IsGlobalEstimateComplete();
}

bool KeyMatrix::SelfIsFull1() const
{
  if (m_networkSize == 0) {
    return false;
  }
  for (uint32_t i = 0; i < m_networkSize; ++i) {
    if (!HasLocalCoverage(i)) {
      return false;
    }
  }
  return true;
}

std::string KeyMatrix::MatrixToString() const
{
  const std::vector<uint8_t> snapshot = Snapshot();
  std::string result;
  result.reserve(snapshot.size());
  for (std::size_t i = 0; i < snapshot.size(); ++i) {
    result.push_back(snapshot[i] ? '1' : '0');
  }
  return result;
}

KeyMatrix KeyMatrix::StringToMatrix(const std::string& matrixString) const
{
  if (matrixString.size() != static_cast<std::size_t>(m_networkSize) * m_networkSize ||
      matrixString.find_first_not_of("01") != std::string::npos) {
    throw std::invalid_argument("KeyMatrix: invalid matrix string");
  }
  KeyMatrix result(m_networkSize, m_nodeId);
  for (uint32_t i = 0; i < m_networkSize; ++i) {
    for (uint32_t j = 0; j < m_networkSize; ++j) {
      result.m_matrix[i][j] = matrixString[static_cast<std::size_t>(i) * m_networkSize + j] == '1';
    }
  }
  return result;
}
