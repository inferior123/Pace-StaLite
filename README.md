# PBA-STA: Path-Based Static Timing Analysis Tool

> Undergraduate thesis project. A from-scratch implementation of a Static Timing Analysis engine supporting both GBA (Graph-Based Analysis) and PBA (Path-Based Analysis), used to verify timing constraints in digital integrated circuit designs.

[中文版](README_zh.md)

## Overview

Static Timing Analysis (STA) is a critical step in the digital IC design flow. It verifies whether a circuit meets its timing constraints — setup and hold — by computing signal propagation delays through all timing paths, replacing expensive dynamic simulation.

This project is a learning-oriented STA tool that aims to understand the core principles and engineering details of STA, including:

- Standard cell library (Liberty) parsing and modeling
- Timing graph construction and delay propagation
- Lookup table (LUT) bilinear interpolation
- GBA vs PBA analysis strategies
- Timing report generation and validation

## Features

### Input Formats

| Format | Description | Parser |
|--------|-------------|--------|
| `.lib` | Liberty standard cell library | iEDA Rust Liberty Parser |
| `.v` | Gate-level netlist (Verilog) | Rust Verilog Parser (`third_party/verilog`) |
| `.sdc` | Timing constraints | Custom parser |

### Analysis Capabilities

- **GBA (Graph-Based Analysis)**: Topological-sort-based full-graph propagation for worst-case timing
- **PBA (Path-Based Analysis)**: DFS-based per-path analysis with slew recalculation during propagation for more accurate results
- **Setup / Hold checking**: Separate verification of setup and hold timing constraints
- **Four path groups**: REG2REG, IN2REG, REG2OUT, IN2OUT

### Output

Generates 8 PrimeTime-format timing reports covering all combinations of setup/hold × 4 path groups.

## Project Structure

```
proj/
├── CMakeLists.txt                  # CMake build configuration
├── Makefile                        # Alternative Makefile build
├── include/
│   ├── sta/                         # Core STA worker/config/data interfaces
│   ├── cell/                        # Cell library data structures and cache
│   ├── sdc/                         # SDC parser interfaces
│   ├── interface/                   # Verilog/SDC adapters
│   ├── timing/                      # Timing graph/arc evaluation interfaces
│   ├── analysis/                    # PBA/GBA analysis interfaces
│   └── report/                      # PT-style report interfaces
├── src/
│   ├── main.cpp                     # Entry point (signal_test / auto_test)
│   ├── sta.cpp                      # Core STA flow orchestration
│   ├── verilog_adapter.cpp          # Rust-Verilog → internal netlist conversion
│   ├── timing/                      # Timing graph builder and arc evaluation
│   ├── analysis/
│   │   ├── pba/                     # PBA graph build / enumerate / recalculate
│   │   └── gba/                     # GBA forward/backward propagation
│   └── report/                      # PT-style timing report generation
├── doc/                             # Design documentation
├── lib/                             # Liberty library files
├── Testing/                         # Test designs (~85 cases)
├── result/                          # Analysis output
├── scripts/                         # Utility scripts (visualization, regression)
└── third_party/
    └── liberty-parser/              # iEDA Liberty parser
```

## Core Concepts

### Timing Graph Model

The core of STA is a directed timing graph where nodes represent pins of standard cell instances and edges represent signal propagation.

```
TimingPointRef (Timing Point)
  ├── inst              # Instance name
  ├── port_name         # Pin name
  ├── type              # INPUT / OUTPUT / CLK_PIN / REGQ / REGD / COMB_PIN
  ├── fanouts           # List of fanout TimingEdges
  └── load capacitance  # rise_cap / fall_cap (min/max variants)

TimingEdge (Timing Edge)
  ├── type          # WIRE / COMB_ARC / SEQ_ARC
  ├── from          # Source timing point
  └── target_point  # Destination timing point
```

Edge types:
- **WIRE**: Net connection (delay typically negligible or estimated via wire load model)
- **COMB_ARC**: Combinational logic arc from input pin to output pin
- **SEQ_ARC**: Sequential arc representing CLK → Q propagation delay of flip-flops

### Cell Library Modeling

```
CellLibrary
  └── StandardCell
      └── Pin
          ├── direction         # input / output / inout
          ├── capacitance       # Input pin capacitance
          └── TimingArc
              ├── timing_sense     # unate / non-unate
              ├── cell_rise        # Output rise delay LUT
              ├── cell_fall        # Output fall delay LUT
              ├── rise_transition  # Output rise transition LUT
              └── fall_transition  # Output fall transition LUT
```

Delays and transition times are stored as 2D lookup tables indexed by input transition time and output load capacitance, with bilinear interpolation for precise values.

### GBA vs PBA

**GBA (Graph-Based Analysis)**:
- Maintains only the worst-case timing value (arrival time, slew) at each timing point
- Propagates via topological sort, taking max (setup) or min (hold) at each node
- Fast but potentially over-pessimistic, since merging worst-case slews from different paths loses path-specific information

**PBA (Path-Based Analysis)**:
- Builds a candidate graph and enumerates individual timing paths via DFS
- Recalculates slew along each specific path, avoiding the pessimism of GBA's worst-case merging
- More accurate but computationally more expensive

The key difference lies in handling "ignorance nodes" (non-unate cells, CLK→Q arcs, etc.): PBA defers direction resolution until propagating along a concrete path, rather than merging directions prematurely.

## Build & Run

### Dependencies

- C++17 compiler (GCC 9+ / Clang 10+)
- CMake 3.16+
- Rust / Cargo (Liberty + Verilog parsers)
- Python 3 (regression testing scripts)

### Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Run

```bash
# run single design from a constraint file
./pba_sta path/to/design.sdc

# run built-in batch regression flow
./pba_sta
```

Main entry behavior (in `src/main.cpp`):
- `./pba_sta <sdc_file>`: invoke `signal_test` for one design
- `./pba_sta`: invoke `auto_test` for batch/regression scenarios
- extra debug argument counts dispatch to helper probes in `include/sta/debug.h`

### Regression Testing

```bash
python3 run_sta.py                # Run iSTA regression tests
python3 compare_timing_reports.py # Compare against reference results (PrimeTime / iEDA)
```

## STA Flow

```
1. Parse Inputs
   ├── Liberty → CellLibrary (cell delay models)
   ├── Verilog → Gate-level netlist instance list
   └── SDC → Clock definitions, I/O constraints

2. Build Timing Graph
   ├── Create TimingPointRef for each pin of each instance
   ├── Establish WIRE edges from netlist connectivity
   ├── Establish COMB_ARC / SEQ_ARC edges from cell library
   └── Use Union-Find for signal equivalence

3. Compute Load Capacitance
   └── BFS from input/clk points, summing fanout pin capacitances

4. Timing Propagation
   ├── GBA: Topological sort propagation of arrival time and slew
   └── PBA: DFS path enumeration with per-path propagation

5. Timing Check
   ├── Setup: data_arrival_time <= data_required_time ?
   └── Hold:  data_arrival_time >= data_required_time ?

6. Generate Reports
   └── PT-format timing reports (8 categories)
```

## References

- [iSTA](https://gitee.com/OSCC-Project/iSTA) — Open-source STA tool from Peng Cheng Laboratory (reference implementation)
- Synopsys PrimeTime User Guide — Industry-standard STA tool documentation
- *Static Timing Analysis for Nanometer Designs* — Classic STA textbook
- Liberty User Guide — Standard cell library format specification

## License

This project is for educational and research purposes only.

The third-party dependencies [iEDA Liberty Parser](third_party/liberty-parser/) and [iEDA Verilog Parser](third_party/verilog/) are licensed under Mulan PSL v2.
