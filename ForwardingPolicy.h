#ifndef FORWARDING_POLICY_H
#define FORWARDING_POLICY_H

#include "KeyMatrix.h"
#include <functional>
#include <vector>
#include <stdint.h>

class ForwardingPolicy
{
public:
  static std::vector<uint32_t> SelectAdaptive(
    const KeyMatrix& matrix, uint32_t neighbor, const std::function<double()>& rng);
  static double GossipProbability(uint32_t N, uint64_t receptions);
  static bool ShouldGossip(double probability, const std::function<double()>& rng);
};

#endif
