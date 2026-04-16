#ifndef STA_TIMING_RESULT_HPP
#define STA_TIMING_RESULT_HPP

#include "cell/cell_data_structure.hpp"
#include "sta_instance.hpp"
#include "sta_timing_core.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace celllib {
class CellLibrary;
}

namespace sta {

class TimingRunResult;

enum EdgeType { WIRE, COMB_ARC, SEQ_ARC };

struct TimingPointRefKey {
  Instance *inst;
  std::string port_name;
  std::optional<SignalBit> bit;
};

struct TimingEdge {
  EdgeType type;
  size_t origin_point;
  size_t target_point;
  /// GBA / levelize: edge disabled to break a combinational loop
  /// (OpenSTA-style).
  bool loop_disabled = false;
};

class TimingPointRef {
public:
  std::size_t id;
  Instance *inst;
  const celllib::StandardCell *std_cell;
  std::string port_name;
  std::optional<SignalBit> bit;

  std::optional<size_t> candidate_idx;

  double load_cap = 0.0;
  double rise_cap = 0.0;
  double fall_cap = 0.0;

  double fall_max_cap = 0.0;
  double fall_min_cap = 0.0;
  double rise_max_cap = 0.0;
  double rise_min_cap = 0.0;

  PointType type;
  std::vector<TimingEdge> fanouts;

  void calculate_capacitance(bool is_max, const TimingRunResult &res,
                             const celllib::CellLibrary *cell_library);
};

inline bool operator==(const TimingPointRefKey &a, const TimingPointRefKey &b) {
  if (a.inst != b.inst)
    return false;
  if (a.port_name != b.port_name)
    return false;
  if (a.bit != b.bit)
    return false;
  return true;
}

struct TimingPointRefHash {
  std::size_t operator()(const TimingPointRefKey &p) const {
    std::size_t h1 = std::hash<Instance *>{}(p.inst);
    std::size_t h2 = std::hash<std::string>{}(p.port_name);
    std::size_t h3 = 0;
    if (p.bit.has_value()) {
      h3 = SignalBitHash{}(p.bit.value());
    }

    std::size_t h = h1;
    h ^= (h2 << 1);
    h ^= (h3 << 2);
    return h;
  }
};

struct TimingStep {
  size_t start_point;
  size_t end_point;
  double incr = 0.0;
  double slew = 0.0;
  double cap_load = 0.0;
  double arrival = 0.0;
  TransitionDirection dir = TransitionDirection::UNKNOWN;
};

struct TimingPathResult {
  PathGroup group = PathGroup::REG2REG;
  size_t index;

  size_t startpoint;
  size_t endpoint;

  bool need_to_recalculate;
  double data_arrival_time = 0.0;

  std::optional<double> library_setup_time;
  std::optional<double> library_hold_time;

  std::vector<TimingStep> steps;
};

struct TimingRunResult {
  std::vector<TimingPathResult> paths;
  std::vector<TimingPointRef> points;

  std::vector<TimingEdge> edges;

  std::unordered_map<TimingPointRefKey, std::size_t, TimingPointRefHash>
      point_index;

  std::size_t get_edge_index(std::size_t from_pt, std::size_t to_pt) const {
    for (std::size_t i = 0; i < edges.size(); ++i)
      if (edges[i].origin_point == from_pt && edges[i].target_point == to_pt)
        return i;
    return SIZE_MAX;
  }
};

inline PointType effective_start_type_for_group(const TimingPointRef &p) {
  if (p.inst == nullptr) {
    if (p.type == CLK_PIN || p.port_name == "__clk__" ||
        (p.bit.has_value() && p.bit->wire_name == "__clk__"))
      return CLK_PIN;
    return INPUT;
  }
  return p.type;
}

} // namespace sta

#endif // STA_TIMING_RESULT_HPP
