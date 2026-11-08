#include "AdhocUdpApplication.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>

using namespace ns3;

namespace {
const double T0 = 1.0;
const double STAGGER = 0.002;

std::string Join(const std::vector<uint32_t>& ids) {
  std::ostringstream out;
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i) out << ';';
    out << ids[i];
  }
  return out.str();
}
std::string Hex(const ValueTag& tag) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (uint8_t byte : tag) out << std::setw(2) << unsigned(byte);
  return out.str();
}
std::string Csv(const std::string& text) {
  std::string result = "\"";
  for (char c : text) { if (c == '"') result += '"'; result += c; }
  return result + '"';
}
void OpenOutput(std::ofstream& stream, const std::string& path) {
  stream.open(path.c_str());
  if (!stream) throw std::runtime_error("Cannot open output: " + path);
  stream.exceptions(std::ios::failbit | std::ios::badbit);
  stream << std::setprecision(12);
}

struct Observation {
  std::vector<Ptr<AdhocUdpApplication> > apps;
  bool adaptive;
  bool threshold70 = false;
  uint32_t thresholdCount = 0;
  double thresholdTime = -1;
  std::string stopReason = "timeout";
  bool stopped = false;
  bool Changed() {
    if (stopped) return true;
    bool complete = true;
    for (const auto& app : apps) {
      if (threshold70 ? app->CoverageCount() < thresholdCount : !app->IsReady()) {
        complete = false;
        break;
      }
    }
    if (complete) {
      stopped = true;
      stopReason = threshold70 ? "all_nodes_threshold70" : "all_nodes_ready";
      if (threshold70) {
        thresholdTime = Simulator::Now().GetSeconds();
        Simulator::Stop();
        return true;
      }
      const ValueTag root = apps.front()->RootValue();
      for (const auto& app : apps)
        if (!app->IsReady() || app->RootValue() != root)
          throw std::runtime_error("Normal termination without consistent ready roots");
      Simulator::Stop();
    }
    return stopped;
  }
};
}

int main(int argc, char* argv[]) {
  try {
    uint32_t numNodes = 5;
    double areaLength = 500, areaWidth = 500, areaHeight = 100;
    double maxProtocolDuration = 59;
    std::string broadcastDiagnostic = "off";
    double diagnosticInterval = STAGGER;
    std::string experimentMode = "full", runId = "1", outputPrefix = "eagka";
    bool staticNodes = false, packetLog = false;
    ProtocolOptions options;
    std::string gossipTtl = "15";
    CommandLine cmd;
    cmd.AddValue("numNodes", "Number of nodes (1..254)", numNodes);
    cmd.AddValue("areaLength", "Bounds length in meters", areaLength);
    cmd.AddValue("areaWidth", "Bounds width in meters", areaWidth);
    cmd.AddValue("areaHeight", "Bounds height in meters", areaHeight);
    cmd.AddValue("experimentMode", "full, forwarding_without_aggregation, gossip_with_aggregation, baseline_gossip, baseline_gossip_threshold70", experimentMode);
    cmd.AddValue("dropProbability", "Independent application ingress loss", options.dropProbability);
    cmd.AddValue("txCryptoDelay", "Per-packet TX CPU service seconds", options.txCryptoDelay);
    cmd.AddValue("rxCryptoDelay", "Per-packet RX CPU service seconds", options.rxCryptoDelay);
    cmd.AddValue("maxProtocolDuration", "Timeout seconds after t0=1", maxProtocolDuration);
    cmd.AddValue("runId", "Experiment label (does not change RNG run)", runId);
    cmd.AddValue("Run", "Alias for runId; use RngRun for ns-3 randomness", runId);
    cmd.AddValue("cryptoOverheadBytes", "Encoded extra bytes per packet", options.cryptoOverheadBytes);
    cmd.AddValue("outputPrefix", "Output pathname prefix; existing files rejected", outputPrefix);
    cmd.AddValue("staticNodes", "Fixed close-range positions for testing", staticNodes);
    cmd.AddValue("packetLog", "Write per-packet sends CSV (default false)", packetLog);
    cmd.AddValue("debug", "Enable per-node serial CPU task trace", options.debug);
    cmd.AddValue("gossipTtl", "Gossip only: positive integer; default 15, N selects network size", gossipTtl);
    cmd.AddValue("broadcastDiagnostic", "off, single (node 0), staggered (all nodes); fixed 5s window", broadcastDiagnostic);
    cmd.AddValue("diagnosticInterval", "Initial TX enqueue spacing in seconds (staggered only, not actual socket send spacing)", diagnosticInterval);
    cmd.Parse(argc, argv);
    if (broadcastDiagnostic != "off" && broadcastDiagnostic != "single" && broadcastDiagnostic != "staggered")
      throw std::runtime_error("Invalid broadcastDiagnostic");
    options.broadcastDiagnostic = broadcastDiagnostic != "off";
    if (!std::isfinite(diagnosticInterval) || diagnosticInterval < 0 ||
        (broadcastDiagnostic == "staggered" && diagnosticInterval * (numNodes - 1) >= 5))
      throw std::runtime_error("Diagnostic enqueue spacing must be nonnegative and staggered sends must fit the 5s window");
    if (options.broadcastDiagnostic && options.dropProbability != 0)
      throw std::runtime_error("Broadcast diagnostics require dropProbability=0 so socket arrivals are not hidden by ingress loss");
    if (gossipTtl == "N") options.gossipTtl = numNodes;
    else {
      if (gossipTtl.empty() || gossipTtl.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("gossipTtl must be a positive integer");
      const unsigned long long ttl = std::stoull(gossipTtl);
      if (!ttl || ttl > UINT32_MAX) throw std::runtime_error("gossipTtl must be a positive uint32");
      options.gossipTtl = static_cast<uint32_t>(ttl);
    }
    if (experimentMode == "onlyA" || experimentMode == "no-aggregation")
      experimentMode = "forwarding_without_aggregation";
    if (experimentMode == "onlyB" || experimentMode == "no-adaptive")
      experimentMode = "gossip_with_aggregation";
    if (experimentMode == "baseline") experimentMode = "baseline_gossip";
    const bool threshold70 = experimentMode == "baseline_gossip_threshold70";
    if (experimentMode != "full" && experimentMode != "forwarding_without_aggregation" &&
        experimentMode != "gossip_with_aggregation" && experimentMode != "baseline_gossip" &&
        !threshold70)
      throw std::runtime_error("Unknown experimentMode");
    options.adaptive = experimentMode == "full" || experimentMode == "forwarding_without_aggregation";
    options.aggregation = experimentMode == "full" || experimentMode == "gossip_with_aggregation";
    options.networkSize = numNodes;
    if (!numNodes || numNodes > 254 || !std::isfinite(areaLength) || areaLength <= 0 ||
        !std::isfinite(areaWidth) || areaWidth <= 0 || !std::isfinite(areaHeight) || areaHeight <= 0 ||
        !std::isfinite(options.dropProbability) || options.dropProbability < 0 || options.dropProbability > 1 ||
        !std::isfinite(options.txCryptoDelay) || options.txCryptoDelay < 0 ||
        !std::isfinite(options.rxCryptoDelay) || options.rxCryptoDelay < 0 ||
        !std::isfinite(maxProtocolDuration) || (!options.broadcastDiagnostic && maxProtocolDuration <= STAGGER * (numNodes - 1)) ||
        outputPrefix.empty()) throw std::runtime_error("Invalid simulation parameters");
    const std::vector<std::string> suffixes = options.broadcastDiagnostic ?
      std::vector<std::string>{"-diagnostic-totals.csv", "-diagnostic-sends.csv", "-diagnostic-senders.csv", "-diagnostic-config.txt", "-diagnostic-pairs.csv"} :
      std::vector<std::string>{"-summary.csv", "-sends.csv", "-nodes.csv", "-config.txt"};
    for (const auto& suffix : suffixes) {
      struct stat info;
      if (stat((outputPrefix + suffix).c_str(), &info) == 0)
        throw std::runtime_error("Refusing to overwrite " + outputPrefix + suffix);
    }
    std::ofstream summary, sends, nodeLog, config;
    OpenOutput(summary, outputPrefix + suffixes[0]);
    if (packetLog) OpenOutput(sends, outputPrefix + suffixes[1]);
    OpenOutput(nodeLog, outputPrefix + suffixes[2]);
    OpenOutput(config, outputPrefix + suffixes[3]);
    const bool synthetic = options.txCryptoDelay == 0 || options.rxCryptoDelay == 0 ||
                           options.cryptoOverheadBytes == 0;
    const std::string calibration = synthetic ? "synthetic/unvalidated" : "explicit/unvalidated";
    config << "protocolVersion=" << ProtocolMessage::Version
      << "\nneighborInitialization=random-half-member-map-grow-to-all-v2"
      << "\ngossipTtl=" << options.gossipTtl << "\ngossipTtlPolicy=" << (gossipTtl == "N" ? "engineering-default-N" : "explicit")
      << "\ngossipTtlScope=Gossip-only-not-adaptive\nneighborStreamBase=2000000\nnumNodes=" << numNodes << "\nareaLength=" << areaLength
      << "\nareaWidth=" << areaWidth << "\nareaHeight=" << areaHeight
      << "\nexperimentMode=" << experimentMode << "\ndropProbability=" << options.dropProbability
      << "\ntxCryptoDelay=" << options.txCryptoDelay << "\nrxCryptoDelay=" << options.rxCryptoDelay
      << "\ncryptoOverheadBytes=" << options.cryptoOverheadBytes
      << "\nmaxProtocolDuration=" << maxProtocolDuration << "\nrunId=" << runId
      << "\nRngSeed=" << RngSeedManager::GetSeed() << "\nRngRun=" << RngSeedManager::GetRun()
      << "\noutputPrefix=" << outputPrefix << "\nstaticNodes=" << staticNodes << "\ndebug=" << options.debug
      << "\npacketLog=" << packetLog << "\ncalibration=" << calibration << "\ncryptography=deterministic-32-byte-tags-not-production-crypto"
      << "\nsession=" << options.session << "\nt0=" << T0 << "\ninitialStaggerSeconds=" << (options.broadcastDiagnostic ? diagnosticInterval : STAGGER)
      << "\nsocketStart=0\ninitialBroadcastSenders=" << (broadcastDiagnostic == "single" ? 1 : numNodes)
      << "\ninitialBroadcastsPerSelectedNode=1\nperiodicDiscovery=none\nprotocolRetries=none"
      << "\nneighborInitialCount=" << numNodes / 2
      << "\nneighborCapacity=" << numNodes << "\nneighborEffectiveCapacity=" << numNodes - 1
      << "\nbroadcastDiagnostic=" << broadcastDiagnostic << "\ndiagnosticInterval=" << diagnosticInterval
      << "\ndiagnosticWindowSeconds=5\ndiagnosticForwarding=none\ndiagnosticEarlyStop=false"
      << "\ndiagnosticIntervalMeaning=initial-TX-enqueue-spacing-not-actual-socket-send-spacing;ignored-for-single"
      << "\ndiagnosticPendingMeaning=unique-socket-arrived-RX-pairs-not-yet-processed;excludes-pending-TX"
      << "\ndiagnosticIngressLoss=requires-dropProbability-zero"
      << "\ncryptoCostSource=main.tex-commented-primitive-table-and-Algorithm3-model-not-per-packet-measurements"
      << "\ncryptoCostDefaultTxMs=2*0.3472+2*0.0013+0.0398=0.7368"
      << "\ncryptoCostDefaultRxMs=3*0.3472+0.0493+2*0.0013+0.0398=1.1333"
      << "\ncryptoCostApproximation=ignore-scalar-field-multiply-add;point-subtraction-as-point-addition;CLI-overrides-effective-delays"
      << "\nsuccessCoverage=all" << (threshold70 ? " nodes have at least ceil(0.7n) distinct contributions" : " contributions or root")
      << "\nrequiredDistinctContributions=" << (threshold70 ? (7 * numNodes + 9) / 10 : numNodes)
      << "\ntermination=" << (options.broadcastDiagnostic ? "fixed-diagnostic-window-5s" : (threshold70 ? "all-nodes-threshold70" : "all-nodes-ready-v2"))
      << "\nrootCompletionPush=" << (options.broadcastDiagnostic ? "none" : "once-for-aggregation-modes")
      << "\nservice=shared-RX-TX-FIFO\ndelayUnit=seconds\npacketBytes=encoded-UDP-payload"
      << "\nencoding=ProtocolMessage-network-byte-order\nheaderBytes=50\nrecordBytes=36\nvalueTagBytes=32\nmatrixWire=packed-bits-MSB-first"
      << "\nheaderFields=version:u32;origin:u32;originSequence:u64;remainingHops:u32;session:u64;sequence:u64;sender:u32;raw:u8;networkSize:u32;nodeCount:u32;hasMatrix:u8"
      << "\nrecordFields=id:u32;value:32-bytes\ncryptoOverheadEncoding=zero-byte-trailer"
      << "\nstandard=802.11g\nmac=AdhocWifiMac\nrateManager=MinstrelWifiManager"
      << "\ndataMode=minstrel\ncontrolMode=minstrel\nnonUnicastMode=basic-rate-0"
      << "\nchannel=YansWifiChannel\nchannelModel=log-distance+random-shadowing+nakagami\nlinkCondition=LOS"
      << "\npropagationDelay=ConstantSpeedPropagationDelayModel"
      << "\npropagationLoss=LogDistancePropagationLossModel+RandomPropagationLossModel+NakagamiPropagationLossModel"
      << "\nexponent=2\nreferenceDistance=1\nreferenceLoss=40.05\nreferenceLossMeaning=Friis-FSPL-at-1m-2.4GHz\nfriisModelAttached=false"
      << "\nshadowing=Normal[Mean=0|Variance=4]\nshadowingSigmaDb=2"
      << "\nnakagamiM0=6\nnakagamiM1=4\nnakagamiM2=3\nnakagamiDistance1=400\nnakagamiDistance2=1000"
      << "\nTxPowerStart=21\nTxPowerEnd=21\nRxGain=3\nRxNoiseFigure=5.5\nCcaMode1Threshold=-84"
      << "\nsimulationCutoffSeconds=" << T0 + (options.broadcastDiagnostic ? 5 : maxProtocolDuration)
      << "\nphy=YansWifiPhyHelper::Default\notherWifiAttributes=ns-3.25-defaults"
      << "\nnetwork=IPv4\nsubnet=10.1.1.0/24\ntransport=UDP\nport=666\nbroadcast=255.255.255.255"
      << "\nmobility=" << (staticNodes ? "ConstantPositionMobilityModel" : "GaussMarkovMobilityModel")
      << "\nposition=uniform-RandomBox\nstaticPosition=(0.1*(id%16),0.1*(id/16),1)-clamped-to-half-bounds"
      << "\nalpha=0.85\ntimeStep=1\nmeanVelocity=Uniform[10,50]\nmeanDirection=Uniform[0,6.283185307]"
      << "\nmeanPitch=Uniform[0.05,0.05]\nnormalVelocity=Normal[0,0,0]"
      << "\nnormalDirection=Normal[mean=0,variance=0.2,bound=0.4]"
      << "\nnormalPitch=Normal[mean=0,variance=0.02,bound=0.04]"
      << "\npositionStreamBase=0\nmobilityStreamBase=1000\nwifiStreamBase=100000"
      << "\napplicationStreams=1000000+3*id+[0:drop,1:adaptive,2:gossip]\n";

    NodeContainer nodes;
    nodes.Create(numNodes);
    WifiHelper wifi;
    wifi.SetStandard(WIFI_PHY_STANDARD_80211g);
    wifi.SetRemoteStationManager("ns3::MinstrelWifiManager");
    YansWifiPhyHelper phy = YansWifiPhyHelper::Default();
    phy.Set("RxNoiseFigure", DoubleValue(5.5));
    phy.Set("RxGain", DoubleValue(3));
    phy.Set("TxPowerStart", DoubleValue(21));
    phy.Set("TxPowerEnd", DoubleValue(21));
    phy.Set("CcaMode1Threshold", DoubleValue(-84));
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel", "Exponent", DoubleValue(2),
      "ReferenceDistance", DoubleValue(1), "ReferenceLoss", DoubleValue(40.05));
    channel.AddPropagationLoss("ns3::RandomPropagationLossModel",
      "Variable", StringValue("ns3::NormalRandomVariable[Mean=0|Variance=4]"));
    channel.AddPropagationLoss("ns3::NakagamiPropagationLossModel",
      "m0", DoubleValue(6), "m1", DoubleValue(4), "m2", DoubleValue(3),
      "Distance1", DoubleValue(400), "Distance2", DoubleValue(1000));
    Ptr<YansWifiChannel> wifiChannel = channel.Create();
    phy.SetChannel(wifiChannel);
    NqosWifiMacHelper mac = NqosWifiMacHelper::Default();
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);
    const int64_t wifiStreams = wifi.AssignStreams(devices, 100000);
    const int64_t channelStreams = wifiChannel->AssignStreams(100000 + wifiStreams);
    InternetStackHelper internet;
    internet.Install(nodes);
    Ipv4AddressHelper addresses;
    addresses.SetBase("10.1.1.0", "255.255.255.0");
    const Ipv4InterfaceContainer interfaces = addresses.Assign(devices);
    for (uint32_t i = 0; i < numNodes; ++i) options.memberAddresses.push_back(interfaces.GetAddress(i));

    MobilityHelper mobility;
    Ptr<RandomBoxPositionAllocator> positions = CreateObject<RandomBoxPositionAllocator>();
    const char* axes[] = {"X", "Y", "Z"};
    const double lengths[] = {areaLength, areaWidth, areaHeight};
    for (uint32_t axis = 0; axis < 3; ++axis) {
      Ptr<UniformRandomVariable> random = CreateObject<UniformRandomVariable>();
      random->SetAttribute("Min", DoubleValue(0));
      random->SetAttribute("Max", DoubleValue(lengths[axis]));
      positions->SetAttribute(axes[axis], PointerValue(random));
    }
    const int64_t positionStreams = positions->AssignStreams(0);
    mobility.SetPositionAllocator(positions);
    if (staticNodes) mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    else mobility.SetMobilityModel("ns3::GaussMarkovMobilityModel",
      "Bounds", BoxValue(Box(0, areaLength, 0, areaWidth, 0, areaHeight)),
      "Alpha", DoubleValue(0.85), "TimeStep", TimeValue(Seconds(1)),
      "MeanVelocity", StringValue("ns3::UniformRandomVariable[Min=10|Max=50]"),
      "MeanDirection", StringValue("ns3::UniformRandomVariable[Min=0|Max=6.283185307]"),
      "MeanPitch", StringValue("ns3::UniformRandomVariable[Min=0.05|Max=0.05]"),
      "NormalVelocity", StringValue("ns3::NormalRandomVariable[Mean=0.0|Variance=0.0|Bound=0.0]"),
      "NormalDirection", StringValue("ns3::NormalRandomVariable[Mean=0.0|Variance=0.2|Bound=0.4]"),
      "NormalPitch", StringValue("ns3::NormalRandomVariable[Mean=0.0|Variance=0.02|Bound=0.04]"));
    mobility.Install(nodes);
    const int64_t mobilityStreams = mobility.AssignStreams(nodes, 1000);
    if (staticNodes) for (uint32_t i = 0; i < numNodes; ++i)
      nodes.Get(i)->GetObject<MobilityModel>()->SetPosition(Vector(
        std::min(0.1 * (i % 16), areaLength / 2),
        std::min(0.1 * (i / 16), areaWidth / 2), std::min(1.0, areaHeight / 2)));
    config << "positionStreamsUsed=" << positionStreams << "\nmobilityStreamsUsed=" << mobilityStreams
           << "\nwifiStreamsUsed=" << wifiStreams
           << "\nchannelStreamBase=" << 100000 + wifiStreams
           << "\nchannelStreamsUsed=" << channelStreams << '\n';

    if (packetLog) sends << "time,sender,receiver,nodeIds,coveredContributionIds,packetBytes,session,origin,originSequence,sequence,remainingHops\n";
    Observation observation;
    observation.adaptive = options.adaptive;
    observation.threshold70 = threshold70;
    observation.thresholdCount = (7 * numNodes + 9) / 10;
    typedef std::tuple<uint64_t,uint32_t,uint64_t,uint32_t> PairKey;
    struct PairTimes { double socket = -1, processed = -1; uint32_t bytes = 0; };
    std::map<PairKey, PairTimes> diagnosticPairs;
    const uint32_t diagnosticSenders = broadcastDiagnostic == "single" ? 1 : numNodes;
    for (uint32_t i = 0; i < numNodes; ++i) {
      Ptr<AdhocUdpApplication> app = CreateObject<AdhocUdpApplication>();
      app->Configure(i, options, 1000000 + 3 * i);
      if (options.broadcastDiagnostic) app->SetDiagnosticObserver(
        [&diagnosticPairs, &options](const ProtocolMessage& message, uint32_t receiver, bool processed) {
          auto& pair = diagnosticPairs[std::make_tuple(message.session, message.origin, message.originSequence, receiver)];
          double& time = processed ? pair.processed : pair.socket;
          if (time < 0) time = Simulator::Now().GetSeconds();
          pair.bytes = message.Encode(options.cryptoOverheadBytes).size();
        });
      app->SetObservers([&observation, &options]() { return !options.broadcastDiagnostic && observation.Changed(); },
        [&sends, packetLog](double time, uint32_t sender, int64_t receiver, const ProtocolMessage& message,
                 uint32_t bytes, const std::vector<uint32_t>& coverage) {
          if (!packetLog) return;
          std::vector<uint32_t> ids;
          for (const auto& record : message.nodes) ids.push_back(record.id);
          sends << time << ',' << sender << ',';
          if (receiver < 0) sends << "broadcast"; else sends << receiver;
          sends << ',' << Join(ids) << ',' << Join(coverage) << ',' << bytes << ',' << message.session << ',' << message.origin << ',' << message.originSequence
                << ',' << message.sequence << ',' << message.remainingHops << '\n';
        });
      nodes.Get(i)->AddApplication(app);
      app->SetStartTime(Seconds(0));
      observation.apps.push_back(app);
      if (!options.broadcastDiagnostic || i < diagnosticSenders)
        Simulator::Schedule(Seconds(T0 + (options.broadcastDiagnostic ? diagnosticInterval : STAGGER) * i),
                            &AdhocUdpApplication::BeginProtocol, app);
    }
    Simulator::Stop(Seconds(T0 + (options.broadcastDiagnostic ? 5 : maxProtocolDuration)));
    Simulator::Run();
    const double stopElapsed = Simulator::Now().GetSeconds() - T0;
    if (options.broadcastDiagnostic) {
      std::ofstream pairs;
      OpenOutput(pairs, outputPrefix + "-diagnostic-pairs.csv");
      pairs << "session,origin,originSequence,receiver,experimentMode,packetBytes,socketRxTime,processedRxTime\n";
      nodeLog << "sender,sendSuccess,sendErrors,packetBytes,uniqueSocketReceivers,uniqueProcessedReceivers,missingReceivers,pending\n";
      uint64_t actual = 0, processed = 0, sent = 0, errors = 0;
      const uint32_t packetBytes = 86 + (options.adaptive ? (numNodes * numNodes + 7) / 8 : 0) + options.cryptoOverheadBytes;
      for (uint32_t sender = 0; sender < diagnosticSenders; ++sender) {
        std::vector<uint32_t> missing;
        uint32_t arrivals = 0, completed = 0;
        for (uint32_t receiver = 0; receiver < numNodes; ++receiver) {
          if (sender == receiver) continue;
          const auto it = diagnosticPairs.find(std::make_tuple(options.session, sender, 1, receiver));
          pairs << options.session << ',' << sender << ",1," << receiver << ',' << experimentMode << ',' << packetBytes << ',';
          if (it != diagnosticPairs.end() && it->second.socket >= 0) {
            ++arrivals;
            pairs << it->second.socket;
          } else missing.push_back(receiver);
          pairs << ',';
          if (it != diagnosticPairs.end() && it->second.processed >= 0) {
            ++completed;
            pairs << it->second.processed;
          }
          pairs << '\n';
        }
        const auto& app = observation.apps[sender];
        nodeLog << sender << ',' << app->SentPackets() << ',' << app->SendErrors() << ',' << packetBytes
                << ',' << arrivals << ',' << completed << ',' << Join(missing) << ',' << arrivals - completed << '\n';
        actual += arrivals; processed += completed; sent += app->SentPackets(); errors += app->SendErrors();
      }
      summary << "diagnostic,interval,RngSeed,RngRun,experimentMode,packetBytes,expectedPairs,actualSocketPairs,processedPairs,pending,sentPackets,sendErrors,stopElapsed,txCryptoDelay,rxCryptoDelay,intervalMeaning,pendingMeaning\n"
              << broadcastDiagnostic << ',' << diagnosticInterval << ',' << RngSeedManager::GetSeed() << ',' << RngSeedManager::GetRun()
              << ',' << experimentMode << ',' << packetBytes << ',' << diagnosticSenders * (numNodes - 1)
              << ',' << actual << ',' << processed << ',' << actual - processed << ',' << sent << ',' << errors
              << ',' << stopElapsed << ',' << options.txCryptoDelay << ',' << options.rxCryptoDelay
              << ",initial-TX-enqueue-spacing-ignored-for-single,socket-arrived-unprocessed-RX-only-excludes-TX\n";
      std::cout << "diagnostic=" << broadcastDiagnostic << " socketPairs=" << actual << " processedPairs=" << processed
                << " pending=" << actual - processed << " stopElapsed=" << stopElapsed << '\n';
      Simulator::Destroy();
      return 0;
    }
    const uint32_t requiredContributions = threshold70 ? observation.thresholdCount : numNodes;
    bool success = true;
    double readyMax = T0;
    uint64_t sent = 0, bytes = 0, received = 0, errors = 0;
    ValueTag agreed;
    nodeLog << "nodeId,ready,readyTime,readyDelay,rootValue,rawContributions,coveredContributions,neighbors,matrixComplete,sentPackets,sentBytes,receivedPackets,sendErrors\n";
    for (uint32_t i = 0; i < numNodes; ++i) {
      const auto& app = observation.apps[i];
      const bool nodeSuccess = threshold70 ? app->CoverageCount() >= requiredContributions : app->IsReady();
      if (!nodeSuccess) success = false;
      else if (!threshold70) {
        readyMax = std::max(readyMax, app->ReadyTime());
        if (agreed.empty()) agreed = app->RootValue();
        else if (agreed != app->RootValue()) success = false;
      }
      sent += app->SentPackets(); bytes += app->SentBytes();
      received += app->ReceivedPackets(); errors += app->SendErrors();
      nodeLog << i << ',' << nodeSuccess << ',';
      if (!threshold70 && app->IsReady()) nodeLog << app->ReadyTime();
      else if (threshold70 && nodeSuccess && observation.thresholdTime >= 0)
        nodeLog << observation.thresholdTime;
      nodeLog << ',';
      if (!threshold70 && app->IsReady()) nodeLog << app->ReadyTime() - T0;
      else if (threshold70 && nodeSuccess && observation.thresholdTime >= 0)
        nodeLog << observation.thresholdTime - T0;
      nodeLog << ',' << Hex(app->RootValue()) << ',' << app->RawCount() << ',' << app->CoverageCount()
        << ',' << app->NeighborCount() << ',';
      if (options.adaptive) nodeLog << app->GlobalEstimateComplete();
      nodeLog << ',' << app->SentPackets() << ',' << app->SentBytes() << ',' << app->ReceivedPackets()
        << ',' << app->SendErrors() << '\n';
    }
    summary << "numNodes,areaLength,areaWidth,areaHeight,experimentMode,dropProbability,txCryptoDelay,rxCryptoDelay,maxProtocolDuration,runId,RngSeed,RngRun,cryptoOverheadBytes,outputPrefix,staticNodes,debug,calibration,t0,initialStaggerSeconds,gkaSuccess,stopReason,gkaDelay,stopElapsed,sentPackets,sentBytes,receivedPackets,sendErrors,protocolVersion,neighborInitialization,gossipTtl,gossipTtlPolicy,channelModel\n";
    summary << numNodes << ',' << areaLength << ',' << areaWidth << ',' << areaHeight << ',' << experimentMode
      << ',' << options.dropProbability << ',' << options.txCryptoDelay << ',' << options.rxCryptoDelay
      << ',' << maxProtocolDuration << ',' << Csv(runId) << ',' << RngSeedManager::GetSeed() << ','
      << RngSeedManager::GetRun() << ',' << options.cryptoOverheadBytes << ',' << Csv(outputPrefix)
      << ',' << staticNodes << ',' << options.debug << ',' << calibration << ',' << T0 << ',' << STAGGER
      << ',' << success << ',' << observation.stopReason << ',';
    if (threshold70 && success && observation.stopReason != "all_nodes_threshold70") success = false;
    if (threshold70 && success) readyMax = observation.thresholdTime;
    if (success) summary << readyMax - T0;
    summary << ',' << stopElapsed << ',' << sent << ',' << bytes << ',' << received << ',' << errors << ',' << ProtocolMessage::Version
      << ",random-half-member-map-grow-to-all-v2," << options.gossipTtl << ','
      << (gossipTtl == "N" ? "engineering-default-N" : "explicit")
      << ",log-distance+random-shadowing+nakagami\n";
    summary.close();
    if (packetLog) sends.close();
    nodeLog.close(); config.close();
    std::cout << "gkaSuccess=" << success << " stopReason=" << observation.stopReason
              << " calibration=" << calibration << " outputPrefix=" << outputPrefix << '\n';
    Simulator::Destroy();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "EAGKA-Ours: " << e.what() << '\n';
    Simulator::Destroy();
    return 1;
  }
}
