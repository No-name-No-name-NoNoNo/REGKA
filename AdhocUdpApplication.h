#ifndef ADHOC_UDP_APPLICATION_H_
#define ADHOC_UDP_APPLICATION_H_

#include "KeyMatrix.h"
#include "KeyGenerationTree.h"
#include "ProtocolMessage.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <tuple>

using namespace ns3;

struct ProtocolOptions {
  uint32_t networkSize = 5;
  uint64_t session = 1;
  uint32_t gossipTtl = 15;
  std::vector<Ipv4Address> memberAddresses;
  bool adaptive = true;
  bool aggregation = true;
  double dropProbability = 0;
  double txCryptoDelay = 0.0007368;
  double rxCryptoDelay = 0.0011333;
  bool broadcastDiagnostic = false;
  uint32_t cryptoOverheadBytes = 0;
  bool debug = false;
};

class AdhocUdpApplication : public Application {
public:
  static TypeId GetTypeId();
  AdhocUdpApplication();
  void Configure(uint32_t id, const ProtocolOptions& options, int64_t streamBase);
  void SetObservers(std::function<bool()> stateObserver,
    std::function<void(double,uint32_t,int64_t,const ProtocolMessage&,uint32_t,
                       const std::vector<uint32_t>&)> sendObserver);
  void BeginProtocol();
  void SetDiagnosticObserver(std::function<void(const ProtocolMessage&,uint32_t,bool)> observer) {
    m_diagnosticObserver = observer;
  }
  bool IsReady() const { return m_ready; }
  double ReadyTime() const { return m_readyTime; }
  const ValueTag& RootValue() const { return m_root; }
  bool GlobalEstimateComplete() const;
  uint64_t SentPackets() const { return m_sent; }
  uint64_t SentBytes() const { return m_sentBytes; }
  uint64_t ReceivedPackets() const { return m_received; }
  uint64_t SendErrors() const { return m_sendErrors; }
  uint32_t RawCount() const { return m_raw.size(); }
  uint32_t CoverageCount() const;
  uint32_t NeighborCount() const { return m_neighbors.size(); }

protected:
  virtual void DoDispose();
private:
  friend struct ApplicationTest;
  struct Neighbor { uint32_t id; Ipv4Address address; };
  struct Work {
    bool transmit = false;
    ProtocolMessage message;
    Ipv4Address address;
    int64_t receiver = -1;
    std::vector<uint8_t> bytes;
  };
  virtual void StartApplication();
  virtual void StopApplication();
  void Receive(Ptr<Socket> socket);
  void LearnNeighbor(uint32_t sender, Ipv4Address address);
  ProtocolMessage NewLogicalMessage();
  void QueueSend(Ipv4Address address, int64_t receiver, bool raw,
                 const std::vector<NodeRecord>& records, const ProtocolMessage& identity);
  void Pump();
  void FinishWork();
  void ProcessReceived(const Work& work);
  void Forward(const ProtocolMessage& message, const std::vector<uint32_t>& coverage);
  void UpdateReadiness();
  std::vector<uint32_t> LocalCoverage() const;
  std::vector<uint32_t> MessageCoverage(const ProtocolMessage& message) const;
  ProtocolOptions m_options;
  uint32_t m_id;
  uint64_t m_sequence;
  uint64_t m_originSequence = 0;
  bool m_running, m_busy, m_ready;
  double m_readyTime;
  ValueTag m_root;
  Ptr<Socket> m_socket;
  Ptr<UniformRandomVariable> m_dropRng, m_adaptiveRng, m_gossipRng;
  EventId m_workEvent;
  std::deque<Work> m_queue;
  std::list<Neighbor> m_neighbors;
  std::map<uint32_t,ValueTag> m_raw;
  std::unique_ptr<KeyGenerationTree> m_tree;
  std::unique_ptr<KeyMatrix> m_matrix;
  std::map<std::tuple<uint64_t,uint32_t,uint64_t>,uint64_t> m_counts;
  uint64_t m_sent, m_sentBytes, m_received, m_sendErrors;
  std::function<void(const ProtocolMessage&,uint32_t,bool)> m_diagnosticObserver;
  std::function<bool()> m_stateObserver;
  std::function<void(double,uint32_t,int64_t,const ProtocolMessage&,uint32_t,
                     const std::vector<uint32_t>&)> m_sendObserver;
};
#endif
