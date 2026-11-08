#ifndef KEY_MATRIX_H
#define KEY_MATRIX_H

#include <vector>
#include <string>
#include <stdint.h>

class KeyMatrix
{
public:
  explicit KeyMatrix(uint32_t n = 0, uint32_t self = 0);
  void Initialize(uint32_t n, uint32_t self);
  void SetLocalCoverage(const std::vector<uint32_t>& coverage);
  void MergeRemoteSnapshot(const std::vector<uint8_t>& snapshot);
  double CalculateCR(uint32_t neighbor) const;
  double CalculatePD(uint32_t k) const;
  bool IsGlobalEstimateComplete() const;
  std::vector<uint8_t> Snapshot() const;
  bool HasLocalCoverage(uint32_t k) const;
  bool HasCoverage(uint32_t node, uint32_t k) const;
  uint32_t Size() const;

  void InitializeMatrix(uint32_t networkSize, uint32_t nodeId);
  bool HasKeyContribution(uint32_t node, uint32_t k) const;
  void ReceiveKeyContribution(uint32_t contributorId);
  void MergeMatrix(const KeyMatrix& received);
  double CalculateFD(uint32_t contributorId) const;
  bool IsFull1() const;
  bool SelfIsFull1() const;
  std::string MatrixToString() const;
  KeyMatrix StringToMatrix(const std::string& matrixString) const;

private:
  std::vector<std::vector<bool> > m_matrix;
  uint32_t m_networkSize;
  uint32_t m_nodeId;
};

#endif
