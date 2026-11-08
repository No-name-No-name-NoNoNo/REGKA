#include "AdhocUdpApplication.h"
#include "ForwardingPolicy.h"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <limits>
#include <set>
#include <stdexcept>

NS_OBJECT_ENSURE_REGISTERED(AdhocUdpApplication);

TypeId AdhocUdpApplication::GetTypeId() {
  static TypeId tid = TypeId("AdhocUdpApplication").SetParent<Application>()
    .AddConstructor<AdhocUdpApplication>();
  return tid;
}

AdhocUdpApplication::AdhocUdpApplication()
  : m_id(0), m_sequence(0), m_running(false), m_busy(false), m_ready(false),
    m_readyTime(-1), m_sent(0), m_sentBytes(0), m_received(0), m_sendErrors(0) {}

void AdhocUdpApplication::Configure(uint32_t id, const ProtocolOptions& options,
                                     int64_t streamBase) {
  if (!options.networkSize || id >= options.networkSize)
    throw std::invalid_argument("valid network size and member ID required");
  m_id = id;
  m_options = options;
  m_dropRng = CreateObject<UniformRandomVariable>();
  m_adaptiveRng = CreateObject<UniformRandomVariable>();
  m_gossipRng = CreateObject<UniformRandomVariable>();
  m_dropRng->SetStream(streamBase);
  m_adaptiveRng->SetStream(streamBase + 1);
  m_gossipRng->SetStream(streamBase + 2);
  m_raw[id] = KeyGenerationTree::MakeContribution(id, options.session);
  if (options.memberAddresses.size() != options.networkSize || !options.gossipTtl)
    throw std::invalid_argument("member IP mapping and positive gossip TTL required");
  Ptr<UniformRandomVariable> neighborsRng = CreateObject<UniformRandomVariable>();
  neighborsRng->SetStream(2000000 + id);
  std::vector<uint32_t> candidates;
  for (uint32_t peer = 0; peer < options.networkSize; ++peer)
    if (peer != id) candidates.push_back(peer);
  const uint32_t capacity = options.networkSize / 2;
  for (uint32_t i = 0; i < capacity; ++i) {
    const uint32_t chosen = neighborsRng->GetInteger(i, candidates.size() - 1);
    std::swap(candidates[i], candidates[chosen]);
    LearnNeighbor(candidates[i], options.memberAddresses[candidates[i]]);
    if (options.debug) std::clog << "initial-neighbor node=" << id << " peer=" << candidates[i] << '\n';
  }
  if (options.aggregation) {
    m_tree.reset(new KeyGenerationTree(options.networkSize));
    m_tree->InsertLeaf(id, m_raw[id]);
  }
  if (options.adaptive) {
    m_matrix.reset(new KeyMatrix(options.networkSize, id));
    m_matrix->SetLocalCoverage(LocalCoverage());
  }
}

void AdhocUdpApplication::SetObservers(std::function<bool()> stateObserver,
  std::function<void(double,uint32_t,int64_t,const ProtocolMessage&,uint32_t,
                     const std::vector<uint32_t>&)> sendObserver) {
  m_stateObserver = stateObserver;
  m_sendObserver = sendObserver;
}

void AdhocUdpApplication::StartApplication() {
  m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
  if (m_socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), 666)) < 0)
    throw std::runtime_error("UDP bind failed");
  m_socket->SetAllowBroadcast(true);
  m_socket->SetRecvCallback(MakeCallback(&AdhocUdpApplication::Receive, this));
  m_running = true;
}

void AdhocUdpApplication::BeginProtocol() {
  if (!m_options.broadcastDiagnostic) {
    UpdateReadiness();
    if (m_stateObserver && m_stateObserver()) { StopApplication(); return; }
  }
  NodeRecord own = {m_id, m_raw.at(m_id)};
  QueueSend(Ipv4Address("255.255.255.255"), -1, true,
            std::vector<NodeRecord>(1, own), NewLogicalMessage());
  if (m_stateObserver) m_stateObserver();
}

void AdhocUdpApplication::StopApplication() {
  m_running = false;
  Simulator::Cancel(m_workEvent);
  m_queue.clear();
  m_busy = false;
  if (m_socket) {
    m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket> >());
    m_socket->Close();
  }
}

void AdhocUdpApplication::DoDispose() {
  StopApplication();
  m_socket = 0;
  m_stateObserver = std::function<bool()>();
  m_sendObserver = decltype(m_sendObserver)();
  Application::DoDispose();
}

void AdhocUdpApplication::Receive(Ptr<Socket> socket) {
  Address source;
  Ptr<Packet> packet;
  while ((packet = socket->RecvFrom(source))) {
    if (m_dropRng->GetValue() < m_options.dropProbability) continue;
    if (!InetSocketAddress::IsMatchingType(source)) continue;
    Work work;
    work.address = InetSocketAddress::ConvertFrom(source).GetIpv4();
    work.bytes.resize(packet->GetSize());
    packet->CopyData(work.bytes.data(), work.bytes.size());
    {
      work.message = ProtocolMessage::Decode(work.bytes, m_options.cryptoOverheadBytes);
      const ProtocolMessage& message = work.message;
      if (message.session != m_options.session || message.networkSize != m_options.networkSize ||
          message.sender >= m_options.networkSize || message.sender == m_id ||
          message.origin >= m_options.networkSize || !message.originSequence ||
          message.nodes.empty() || (!m_tree && !message.raw) ||
          (m_matrix && message.matrix.empty()) || (!m_matrix && !message.matrix.empty()))
        throw std::runtime_error("Internal protocol message mismatch");
      MessageCoverage(message);
    }
    if (m_diagnosticObserver) m_diagnosticObserver(work.message, m_id, false);
    m_queue.push_back(work);
    Pump();
  }
}

void AdhocUdpApplication::LearnNeighbor(uint32_t sender, Ipv4Address address) {
  if (sender >= m_options.networkSize || sender == m_id ||
      address == m_options.memberAddresses[m_id]) return;
  for (auto it = m_neighbors.begin(); it != m_neighbors.end();) {
    if (it->id == sender || it->address == address) it = m_neighbors.erase(it);
    else ++it;
  }
  const uint32_t capacity = m_options.networkSize - 1;
  if (!capacity) return;
  Neighbor neighbor = {sender, address};
  m_neighbors.push_back(neighbor);
  while (m_neighbors.size() > capacity) m_neighbors.pop_front();
}

ProtocolMessage AdhocUdpApplication::NewLogicalMessage() {
  ProtocolMessage message;
  message.session = m_options.session;
  message.origin = m_id;
  message.originSequence = ++m_originSequence;
  message.remainingHops = m_options.adaptive ? 0 : m_options.gossipTtl;
  return message;
}

void AdhocUdpApplication::QueueSend(Ipv4Address address, int64_t receiver, bool raw,
                                    const std::vector<NodeRecord>& records,
                                    const ProtocolMessage& identity) {
  if (records.empty()) return;
  Work work;
  work.transmit = true;
  work.address = address;
  work.receiver = receiver;
  work.message.session = identity.session;
  work.message.origin = identity.origin;
  work.message.originSequence = identity.originSequence;
  work.message.remainingHops = identity.remainingHops;
  work.message.sender = m_id;
  work.message.networkSize = m_options.networkSize;
  work.message.raw = raw;
  work.message.nodes = records;
  m_queue.push_back(work);
  Pump();
}

void AdhocUdpApplication::Pump() {
  if (!m_running || m_busy || m_queue.empty()) return;
  m_busy = true;
  Work& work = m_queue.front();
  if (work.transmit) {
    work.message.sequence = ++m_sequence;
    if (m_matrix) work.message.matrix = m_matrix->Snapshot();
    work.bytes = work.message.Encode(m_options.cryptoOverheadBytes);
  }
  if (m_options.debug)
    std::clog << std::setprecision(12) << "cpu-start time=" << Simulator::Now().GetSeconds() << " node=" << m_id
              << " task=" << (work.transmit ? "tx" : "rx") << '\n';
  m_workEvent = Simulator::Schedule(Seconds(work.transmit ? m_options.txCryptoDelay :
                                           m_options.rxCryptoDelay),
                                   &AdhocUdpApplication::FinishWork, this);
}

void AdhocUdpApplication::FinishWork() {
  Work work = m_queue.front();
  m_queue.pop_front();
  if (m_options.debug)
    std::clog << std::setprecision(12) << "cpu-finish time=" << Simulator::Now().GetSeconds() << " node=" << m_id
              << " task=" << (work.transmit ? "tx" : "rx") << '\n';
  if (work.transmit) {
    Ptr<Packet> packet = Create<Packet>(work.bytes.data(), work.bytes.size());
    int result = m_socket->SendTo(packet, 0, InetSocketAddress(work.address, 666));
    if (result == static_cast<int>(work.bytes.size())) {
      ++m_sent;
      m_sentBytes += work.bytes.size();
      if (m_sendObserver) m_sendObserver(Simulator::Now().GetSeconds(), m_id,
        work.receiver, work.message, work.bytes.size(), MessageCoverage(work.message));
    } else ++m_sendErrors;
  } else ProcessReceived(work);
  m_busy = false;
  Pump();
}

std::vector<uint32_t> AdhocUdpApplication::MessageCoverage(const ProtocolMessage& message) const {
  std::set<uint32_t> unique;
  for (const auto& node : message.nodes) {
    if (message.raw) {
      if (node.id >= m_options.networkSize) throw std::runtime_error("invalid contribution ID");
      unique.insert(node.id);
    } else {
      if (!m_tree) throw std::runtime_error("tree record in non-B mode");
      const auto coverage = m_tree->CoverageOf(node.id);
      unique.insert(coverage.begin(), coverage.end());
    }
  }
  return std::vector<uint32_t>(unique.begin(), unique.end());
}

std::vector<uint32_t> AdhocUdpApplication::LocalCoverage() const {
  if (m_tree) return m_tree->CoveredLeaves();
  std::vector<uint32_t> coverage;
  for (const auto& entry : m_raw) coverage.push_back(entry.first);
  return coverage;
}

uint32_t AdhocUdpApplication::CoverageCount() const { return LocalCoverage().size(); }
bool AdhocUdpApplication::GlobalEstimateComplete() const {
  return m_matrix && m_matrix->IsGlobalEstimateComplete();
}

void AdhocUdpApplication::ProcessReceived(const Work& work) {
  const auto& message = work.message;
  if (m_diagnosticObserver) m_diagnosticObserver(message, m_id, true);
  if (m_options.broadcastDiagnostic) { ++m_received; return; }
  const auto coverage = MessageCoverage(message);
  for (const auto& record : message.nodes) {
    if (message.raw) {
      auto existing = m_raw.find(record.id);
      if (existing != m_raw.end() && existing->second != record.value)
        throw std::runtime_error("conflicting raw contribution");
      m_raw[record.id] = record.value;
      if (m_tree) m_tree->InsertLeaf(record.id, record.value);
    } else m_tree->MergeNode(record.id, record.value);
  }
  ++m_received;
  auto& count = m_counts[std::make_tuple(message.session, message.origin, message.originSequence)];
  if (count != std::numeric_limits<uint64_t>::max()) ++count;
  if (m_options.debug)
    std::clog << "logical-rx node=" << m_id << " origin=" << message.origin
              << " originSequence=" << message.originSequence << " count=" << count
              << " ttl=" << message.remainingHops << " coverage=" << CoverageCount() << '\n';
  LearnNeighbor(message.sender, work.address);
  if (m_matrix) {
    m_matrix->MergeRemoteSnapshot(message.matrix);
    m_matrix->SetLocalCoverage(LocalCoverage());
  }
  const bool wasReady = m_ready;
  UpdateReadiness();
  if (m_stateObserver && m_stateObserver()) { StopApplication(); return; }
  if (m_tree && !wasReady && m_ready) {
    const std::vector<NodeRecord> root(1, NodeRecord{1, m_root});
    const auto identity = NewLogicalMessage();
    if (m_options.debug) std::clog << "root-push node=" << m_id << " originSequence=" << identity.originSequence << '\n';
    for (const auto& neighbor : m_neighbors) {
      bool complete = false;
      if (m_matrix) {
        complete = true;
        for (uint32_t k = 0; k < m_options.networkSize; ++k)
          if (!m_matrix->HasCoverage(neighbor.id, k)) { complete = false; break; }
      }
      if (!complete) QueueSend(neighbor.address, neighbor.id, false, root, identity);
    }
    return;
  }
  Forward(message, coverage);
}

void AdhocUdpApplication::Forward(const ProtocolMessage& message,
                                  const std::vector<uint32_t>& coverage) {
  if (m_matrix) {
    for (const auto& neighbor : m_neighbors) {
      const auto selected = ForwardingPolicy::SelectAdaptive(*m_matrix, neighbor.id,
        [this]() { return m_adaptiveRng->GetValue(); });
      if (m_tree) QueueSend(neighbor.address, neighbor.id, false, m_tree->Aggregate(selected), message);
      else for (uint32_t k : selected) {
        NodeRecord record = {k, m_raw.at(k)};
        QueueSend(neighbor.address, neighbor.id, true, std::vector<NodeRecord>(1, record), message);
      }
    }
    return;
  }
  if (message.remainingHops <= 1) return;
  ProtocolMessage relay = message;
  --relay.remainingHops;
  const double probability = ForwardingPolicy::GossipProbability(m_options.networkSize,
    m_counts.at(std::make_tuple(message.session, message.origin, message.originSequence)));
  if (!ForwardingPolicy::ShouldGossip(probability,
      [this]() { return m_gossipRng->GetValue(); })) return;
  if (m_tree) {
    const auto records = m_tree->Aggregate(coverage);
    for (const auto& neighbor : m_neighbors)
      QueueSend(neighbor.address, neighbor.id, false, records, relay);
  } else {
    for (const auto& neighbor : m_neighbors) {
      if (neighbor.id == message.sender) continue;
      for (uint32_t k : coverage) {
        NodeRecord record = {k, m_raw.at(k)};
        QueueSend(neighbor.address, neighbor.id, true, std::vector<NodeRecord>(1, record), relay);
      }
    }
  }
}

void AdhocUdpApplication::UpdateReadiness() {
  if (m_ready) return;
  if (m_tree) {
    if (!m_tree->HasRootValue()) return;
    m_root = m_tree->ValueTagAtRoot();
  } else {
    if (m_raw.size() != m_options.networkSize) return;
    std::vector<ValueTag> leaves(m_options.networkSize);
    for (const auto& entry : m_raw) leaves[entry.first] = entry.second;
    m_root = KeyGenerationTree::ComputeRootFromLeaves(leaves);
  }
  m_ready = true;
  m_readyTime = Simulator::Now().GetSeconds();
}
