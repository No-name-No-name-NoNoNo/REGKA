# A Cross-Layer Design: Reliable Group Key Agreement with Propagation Status Awareness for UANET

Zhan Zhang, Zhe Ren, Xinghua Li, Yunwei Wang, Yinbin Miao, and Robert H. Deng

> **RGKA-PSA** (Reliable Group Key Agreement with Propagation Status Awareness) is the reference implementation and NS-3 simulation suite for the paper:
>
> **“A Cross-Layer Design: Reliable Group Key Agreement with Propagation Status Awareness for UANET.”**

## Description

This repository implements RGKA-PSA, a cross-layer group key agreement scheme for UANET. Unstable wireless links drop key-contribution packets, so members collect inconsistent contributions and group key agreement fails. Redundant forwarding at the communication layer, such as flooding and gossip, is not designed for group key agreement, and the extra traffic is costly for resource-constrained UAVs.

RGKA-PSA drives packet propagation at the communication layer according to the requirements of group key agreement. Each UAV keeps a lightweight KeyMatrix that records which members have received which key contributions. An adaptive forwarding strategy uses this status, together with the key contribution complement rate and the key contribution propagation degree, to choose neighbors and the forwarding probability, and thus avoids unnecessary retransmissions. A key contribution aggregation method, built on a Merkle-tree-based group key generation tree, combines multiple contributions into one packet and lowers the communication load.

The NS-3 code evaluates the scheme with five metrics: (i) the group key agreement success rate, (ii) the group key agreement delay, (iii) the communication overhead, (iv) the key contribution propagation efficiency, and (v) the group key agreement energy consumption.

## Repository Layout

```
EAGKA-Ours/
├─ EAGKA-Ours.cc            # Simulation entry: scenario setup and run
├─ AdhocUdpApplication.cc   # UDP application for UANET communication
├─ AdhocUdpApplication.h
├─ ProtocolMessage.cc       # Protocol message encode/decode
├─ ProtocolMessage.h
├─ KeyMatrix.cc             # Key matrix for group key agreement
├─ KeyMatrix.h
├─ KeyGenerationTree.cc     # Key generation tree and contribution aggregation
├─ KeyGenerationTree.h
├─ ForwardingPolicy.cc      # Cross-layer forwarding decisions
└─ ForwardingPolicy.h
```

## Dependencies

* ns-3 3.25
* g++ 7.5.0
* C++03

## How to run?

1. Install NS-3 (v3.25) on a Linux machine

```bash
Follow the official [ns-3 build guide](https://www.nsnam.org/docs/release/3.25/) or use your package manager.
```

2. Clone / move the repository into the `scratch` folder

```bash
# Example layout
   /home/<user>/workspace/ns-3.25/scratch/EAGKA-Ours
```

3. Build and run one scenario from the ns-3 root

```bash
cd ns-3.25
./waf --run "EAGKA-Ours --numNodes=5 --areaLength=200 --areaWidth=200 --areaHeight=80 --experimentMode=full"
```

`experimentMode` is one of `full`, `forwarding_without_aggregation`, `gossip_with_aggregation`, `baseline_gossip`, or `baseline_gossip_threshold70`. A run writes `<outputPrefix>-summary.csv`, `<outputPrefix>-nodes.csv`, and `<outputPrefix>-config.txt` (default prefix `eagka`) and refuses to overwrite existing files.

The experiments require substantial computational resources. One round of batch testing on a 112-core CPU server takes more than 48 hours.

## NS3 Simulation Parameters

In our simulation experiment, to reflect optimal physical environments and channel conditions, the link quality is configured as **LOS** (Line of Sight) for wide-open rural/over-sea scenarios. The channel model incorporates a combination of the Friis model, log-distance path loss, Nakagami fading, and random shadowing models to comprehensively simulate multipath fading and shadowing effects in UANET. Relevant parameters are configured according to common UAV hardware settings.

| Parameter                  | Value / Range                                         |
| -------------------------- | ----------------------------------------------------- |
| **Area size**              | 200 × 200 × 80 – 600 × 600 × 250 m³         |
| **Node number**            | 5 – 55                                               |
| **Flight speed**           | 0 – 20 m s⁻¹                                       |
| **Data transfer rate**     | 12 Mbit s⁻¹                                         |
| **Path-loss model**        | Friis · Log-Distance · Nakagami · Random shadowing |
| **Communication standard** | IEEE 802.11g                                          |
| **Motion model**           | Gauss–Markov                                         |
| **Link-quality levels**    | LOS (Line of Sight)                                  |
| **Antenna gain**           | 2 dBi                                                 |
| **Noise figure**           | 7 dB                                                  |

### Physical Layer Configuration (PHY)

| Parameter                    | Value                                               |
| ---------------------------- | --------------------------------------------------- |
| **TX Power Start/End**       | 21.0 dBm                                            |
| **RX Noise Figure**          | 5.5 dB                                              |
| **RX Gain**                  | 3.0 dBi                                             |
| **CCA Mode1 Threshold**      | -84.0 dBm                                           |

## Cryptographic Primitives Performance

The following table shows the computational cost of key cryptographic primitives measured on a Raspberry Pi device (Broadcom BCM2711, Quad-Core Cortex-A72, ARM v8, 64-bit SoC @ 1.5GHz).

| Symbol    | Description                              | Time (ms) |
|-----------|------------------------------------------|-----------|
| T_bp      | Bilinear pairing operation              | 5.5873    |
| T_bpm     | Bilinear pairing scalar multiplication  | 0.8437    |
| T_bpa     | Bilinear pairing scalar point addition  | 0.0208    |
| T_sm      | ECC scalar multiplication               | 0.3472    |
| T_sa      | ECC scalar addition                     | 0.0493    |
| T_h       | One-way hash function                   | 0.0013    |
| T_e/s     | Symmetric encryption/decryption         | 0.0398    |
| T_mph     | MapToPoint hash function                | 7.8937    |

> **Note**: These timing measurements provide a reference for computational complexity analysis and energy consumption estimation in UANET environments. Actual performance may vary depending on hardware specifications and implementation optimizations.


---

