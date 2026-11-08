#include "ForwardingPolicy.h"

#include <algorithm>
#include <stdexcept>

std::vector<uint32_t> ForwardingPolicy::SelectAdaptive(
  const KeyMatrix& matrix, uint32_t neighbor, const std::function<double()>& rng)
{
  std::vector<uint32_t> selected;
  if (neighbor >= matrix.Size()) {
    return selected;
  }
  if (!(rng() < matrix.CalculateCR(neighbor))) {
    return selected;
  }
  for (uint32_t k = 0; k < matrix.Size(); ++k) {
    if (matrix.HasLocalCoverage(k) && !matrix.HasCoverage(neighbor, k) &&
        rng() > matrix.CalculatePD(k)) {
      selected.push_back(k);
    }
  }
  return selected;
}

double ForwardingPolicy::GossipProbability(uint32_t N, uint64_t receptions)
{
  if (!N || !receptions) return 0.0;
  return std::max(0.0, 1.0 - (static_cast<double>(receptions) - 1.0) / N);
}

bool ForwardingPolicy::ShouldGossip(double probability, const std::function<double()>& rng)
{
  return rng() < probability;
}
