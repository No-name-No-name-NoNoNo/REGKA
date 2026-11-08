#include "ProtocolMessage.h"

#include <limits>
#include <stdexcept>

namespace {
const size_t HeaderSize = 50;
const size_t RecordSize = 36;
void Put(std::vector<uint8_t>& out, uint64_t value, unsigned width) {
  for (unsigned i = width; i > 0; --i) out.push_back(static_cast<uint8_t>(value >> ((i - 1) * 8)));
}
uint64_t Get(const std::vector<uint8_t>& bytes, size_t& pos, unsigned width) {
  if (pos > bytes.size() || width > bytes.size() - pos) throw std::invalid_argument("truncated message");
  uint64_t value = 0;
  for (unsigned i = 0; i < width; ++i) value = (value << 8) | bytes[pos++];
  return value;
}
uint64_t MatrixBytes(uint32_t n) { return (uint64_t(n) * n + 7) / 8; }
void ValidateRecord(const NodeRecord& record, uint32_t n, bool raw) {
  if (record.value.size() != 32) throw std::invalid_argument("tag must contain 32 bytes");
  uint64_t base = 1;
  while (base < n) base *= 2;
  if (n == 0 || (raw ? record.id >= n : (record.id == 0 || uint64_t(record.id) >= 2 * base)))
    throw std::invalid_argument("invalid record id");
}
}

size_t ProtocolMessage::SerializedSize(uint32_t cryptoOverhead) const {
  if (nodes.size() > std::numeric_limits<uint32_t>::max()) throw std::length_error("too many records");
  for (size_t i = 0; i < nodes.size(); ++i) ValidateRecord(nodes[i], networkSize, raw);
  if (!matrix.empty()) {
    if (uint64_t(matrix.size()) != uint64_t(networkSize) * networkSize)
      throw std::invalid_argument("matrix must contain N*N bits");
    for (size_t i = 0; i < matrix.size(); ++i)
      if (matrix[i] > 1) throw std::invalid_argument("matrix contains non-bit value");
  }
  const uint64_t total = HeaderSize + uint64_t(nodes.size()) * RecordSize
      + (matrix.empty() ? 0 : MatrixBytes(networkSize)) + cryptoOverhead;
  if (total > std::numeric_limits<size_t>::max()) throw std::length_error("message too large");
  return static_cast<size_t>(total);
}

std::vector<uint8_t> ProtocolMessage::Encode(uint32_t cryptoOverhead) const {
  const size_t size = SerializedSize(cryptoOverhead);
  std::vector<uint8_t> bytes;
  bytes.reserve(size);
  Put(bytes, Version, 4);
  Put(bytes, origin, 4);
  Put(bytes, originSequence, 8);
  Put(bytes, remainingHops, 4);
  Put(bytes, session, 8);
  Put(bytes, sequence, 8);
  Put(bytes, sender, 4);
  Put(bytes, raw ? 1 : 0, 1);
  Put(bytes, networkSize, 4);
  Put(bytes, nodes.size(), 4);
  Put(bytes, matrix.empty() ? 0 : 1, 1);
  for (size_t i = 0; i < nodes.size(); ++i) {
    Put(bytes, nodes[i].id, 4);
    bytes.insert(bytes.end(), nodes[i].value.begin(), nodes[i].value.end());
  }
  if (!matrix.empty()) {
    const size_t start = bytes.size();
    bytes.resize(start + static_cast<size_t>(MatrixBytes(networkSize)), 0);
    for (size_t i = 0; i < matrix.size(); ++i)
      bytes[start + i / 8] |= static_cast<uint8_t>(matrix[i] << (7 - i % 8));
  }
  bytes.resize(size, 0);
  return bytes;
}

ProtocolMessage ProtocolMessage::Decode(const std::vector<uint8_t>& bytes, uint32_t cryptoOverhead) {
  ProtocolMessage message;
  size_t pos = 0;
  if (Get(bytes, pos, 4) != Version) throw std::invalid_argument("unsupported protocol version");
  message.origin = static_cast<uint32_t>(Get(bytes, pos, 4));
  message.originSequence = Get(bytes, pos, 8);
  message.remainingHops = static_cast<uint32_t>(Get(bytes, pos, 4));
  message.session = Get(bytes, pos, 8);
  message.sequence = Get(bytes, pos, 8);
  message.sender = static_cast<uint32_t>(Get(bytes, pos, 4));
  const uint64_t rawFlag = Get(bytes, pos, 1);
  if (rawFlag > 1) throw std::invalid_argument("invalid raw flag");
  message.raw = rawFlag != 0;
  message.networkSize = static_cast<uint32_t>(Get(bytes, pos, 4));
  const uint64_t count = Get(bytes, pos, 4);
  const uint64_t matrixFlag = Get(bytes, pos, 1);
  if (matrixFlag > 1 || (matrixFlag && message.networkSize == 0)) throw std::invalid_argument("invalid matrix flag");
  const uint64_t expected = HeaderSize + count * RecordSize
      + (matrixFlag ? MatrixBytes(message.networkSize) : 0) + cryptoOverhead;
  if (expected != bytes.size()) throw std::invalid_argument("message length mismatch");
  for (uint64_t i = 0; i < count; ++i) {
    NodeRecord record;
    record.id = static_cast<uint32_t>(Get(bytes, pos, 4));
    record.value.assign(bytes.begin() + pos, bytes.begin() + pos + 32);
    pos += 32;
    ValidateRecord(record, message.networkSize, message.raw);
    message.nodes.push_back(record);
  }
  if (matrixFlag) {
    const uint64_t bits = uint64_t(message.networkSize) * message.networkSize;
    if (bits > message.matrix.max_size()) throw std::length_error("matrix too large");
    message.matrix.resize(static_cast<size_t>(bits));
    for (size_t i = 0; i < message.matrix.size(); ++i)
      message.matrix[i] = (bytes[pos + i / 8] >> (7 - i % 8)) & 1;
    const unsigned unused = static_cast<unsigned>((8 - bits % 8) % 8);
    pos += static_cast<size_t>(MatrixBytes(message.networkSize));
    if (unused && (bytes[pos - 1] & ((1U << unused) - 1))) throw std::invalid_argument("nonzero matrix padding");
  }
  for (; pos < bytes.size(); ++pos)
    if (bytes[pos] != 0) throw std::invalid_argument("nonzero overhead padding");
  return message;
}
