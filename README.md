# A Cross-Layer Design: Reliable Group Key Agreement with Propagation Status Awareness for UANET

*Version 1.1 – January 2026*

> **REGKA** is the reference implementation and NS‑3 simulation suite accompanying our paper:

## Description

In this paper, our core work is to design a *A Cross-Layer Design: Reliable Group Key Agreement with Propagation Status Awareness for UANET* to improve the success rate of group key agreement in UANET with unstable links. The core idea is to drive message propagation at the transmission layer according to the requirements of group key agreement. Specifically, we let each UAV perceive the key contribution reception status at the protocol layer to guide the transmission layer in forwarding the key contribution to the required UAVs, thereby avoiding unnecessary redundant forwardings. Furthermore, we take advantage of the aggregability of key contributions to enable a UAV to aggregate multiple key contribution messages at the protocol layer and send them in a single packet, further reducing the load of the transmission layer. This top‑down collaborative design enhances the propagation efficiency of key contributions in UANET, achieving higher group key agreement success rates and lower communication overhead.

These codes are to verify the performance of the proposed scheme. Specifically, we use three quantitative analyses that measure: (i) the group key agreement success rate, (ii) the group key agreement delay, (iii) the communication overhead, (iv) the key contribution propagation efficiency and (v) the group key agreement energy consumption.

> **Stochastic note** – Results involve random channel fading & gossip selection; absolute numbers may differ slightly from the paper, but **trends and relative gains remain stable**.

## Repository Layout

```
REGKA/
├─ EAGKA-Ours.cc         # Main simulation program entry, contains simulation scenario setup and execution logic
├─ KeyMatrix.cc          #  Designed KeyMatrix implementation for group key agreement
├─ KeyMatrix.h           # Designed KeyMatrix class definition and interface declarations
├─ KeyGenerationTree.cc  # Designed key generation tree implementation for efficient key aggregation
├─ KeyGenerationTree.h   # Designed key generation tree class definition and interface declarations
├─ AdhocUdpApplication.cc # Custom UDP application implementation for UANET communication simulation
├─ AdhocUdpApplication.h  # Custom UDP application class definition and interface declarations
├─ Analyze.py            # Results aggregation and analysis script
└─ allrun.sh             # Script for batch running different scenarios
```

## Batch Testing Scripts

For convenience and reproducibility, we provide two automation scripts that handle large-scale experiments and result processing:

### allrun.sh - Batch Experiment Controller

The `allrun.sh` script is a comprehensive batch testing automation tool that systematically explores different simulation scenarios:

**Key Features:**

- **Multi-dimensional parameter sweep**: Automatically tests across 4 different area configurations (200×200×80m to 600×600×250m) and corresponding node density ranges (5-55 nodes)
- **Statistical robustness**: Runs 200 independent experiments per configuration to ensure statistical significance
- **Parallel execution**: Uses GNU `parallel` with 120 concurrent jobs for efficient resource utilization
- **Automated compilation**: Sets appropriate NS-3 compilation flags and logging levels

### Analyze.py - Results Processing Pipeline

The `Analyze.py` script provides a complete data processing and analysis pipeline:

**Processing Steps:**

1. **CSV Aggregation**: Merges all individual result files from `results_cache/` directory
2. **Database Conversion**: Converts merged CSV data to SQLite database for efficient querying
3. **Statistical Analysis**: Generates comprehensive Excel reports with multiple analysis sheets:
   - Success rate analysis by scenario
   - Key agreement delay statistics
   - Communication overhead metrics
   - Performance trends across different configurations

**Output Files:**

- `{timestamp}_Result.csv`: Consolidated raw experimental data
- `{timestamp}_experiment_results.db`: SQLite database for advanced queries
- `{timestamp}_analysis_results.xlsx`: Multi-sheet Excel report with statistical summaries

**Usage Example:**

```bash
# Run complete experimental suite
./allrun.sh           # Executes all experiments (may take several hours)

# Manual result processing (if needed)
python Analyze.py       # Aggregate and analyze results
```

> **Performance Note**: The full experimental suite involves ~7000 simulation runs. On a 112 multi-core server, expect 2-4 hours of execution time depending on hardware capabilities.

## Dependencies

* ns-3 3.25
* python 2.7.12
* g++ 7.5.0
* c++ c++03
* pandas 0.24.2
* matplotlib 2.2.5
* numpy 1.16.6
* openpyxl 2.6.4

## How to run?

1. Install NS-3 (v3.25) on a Linux machine

```bash
Follow the official [ns-3 build guide](https://www.nsnam.org/docs/release/3.25/) or use your package manager.
```

2. Clone / move the repository into the `scratch` folder

```bash
# Example layout
   /home/<user>/workspace/ns-3.25/scratch/REGKA
```

3. Run the script

```bash
cd ns-3.25/scratch/REGKA
chmod +x allrun.sh        # one-time, if needed
./allrun.sh               # fires up the full experiment suite
```

> **Performance Note**: The full experimental suite involves ~7000 simulation runs. On a 112 multi-core server, expect 2-4 hours of execution time depending on hardware capabilities.

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

