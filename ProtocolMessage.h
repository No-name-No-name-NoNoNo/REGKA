#ifndef EAGKA_PROTOCOL_MESSAGE_H
#define EAGKA_PROTOCOL_MESSAGE_H

#include "KeyGenerationTree.h"
#include <cstddef>

struct ProtocolMessage {
  static const uint32_t Version = 2;
  uint32_t origin = 0;
  uint64_t originSequence = 0;
  uint32_t remainingHops = 0;
  uint64_t session = 0;
  uint64_t sequence = 0;
  uint32_t sender = 0;
  bool raw = false;
  std::vector<NodeRecord> nodes;
  std::vector<uint8_t> matrix;
  uint32_t networkSize = 0;

  std::vector<uint8_t> Encode(uint32_t cryptoOverhead = 0) const;
  static ProtocolMessage Decode(const std::vector<uint8_t>& bytes, uint32_t cryptoOverhead = 0);
  size_t SerializedSize(uint32_t cryptoOverhead = 0) const;
};

#endif
