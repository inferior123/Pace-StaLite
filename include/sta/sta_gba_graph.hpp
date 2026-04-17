#ifndef STA_GBA_GRAPH_HPP
#define STA_GBA_GRAPH_HPP

#include "../cell/cell_data_structure.hpp"
#include "sta_timing_core.hpp"
#include "sta_topo.hpp"

#include <cstddef>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace sta {

struct GbaPath {
  size_t startnode;
  size_t endnode;
  celllib::TimingArc *arc;

  double slew;
  double incr;
  TransitionDirection dir;
  TransitionDirection input_dir;
  size_t path_idx;
};

struct GbaNode {
  size_t pt_idx;
  size_t id;

  double delay_rise;
  double delay_fall;
  double slew_rise;
  double slew_fall;
  double required_rise;
  double required_fall;

  size_t prev_node_rise;
  size_t prev_node_fall;
  size_t prev_path_rise;
  size_t prev_path_fall;
  size_t next_node_rise;
  size_t next_node_fall;
  size_t next_path_rise;
  size_t next_path_fall;

  std::optional<double> library_setup_time_rise;
  std::optional<double> library_setup_time_fall;
  std::optional<double> library_hold_time_rise;
  std::optional<double> library_hold_time_fall;

  std::vector<GbaPath> fanouts;

  void reset(AnalysisMode mode) {
    const double pos_inf = std::numeric_limits<double>::infinity();
    const double neg_inf = -pos_inf;
    const size_t invalid_idx = std::numeric_limits<size_t>::max();

    delay_rise = (mode == AnalysisMode::MAX) ? neg_inf : pos_inf;
    delay_fall = (mode == AnalysisMode::MAX) ? neg_inf : pos_inf;
    required_rise = (mode == AnalysisMode::MAX) ? pos_inf : neg_inf;
    required_fall = (mode == AnalysisMode::MAX) ? pos_inf : neg_inf;

    prev_node_rise = invalid_idx;
    prev_node_fall = invalid_idx;
    prev_path_rise = invalid_idx;
    prev_path_fall = invalid_idx;
    next_node_rise = invalid_idx;
    next_node_fall = invalid_idx;
    next_path_rise = invalid_idx;
    next_path_fall = invalid_idx;
  }

  void init_for_point(size_t point_idx, AnalysisMode mode) {
    const double pos_inf = std::numeric_limits<double>::infinity();
    const double neg_inf = -pos_inf;

    pt_idx = point_idx;
    id = point_idx;
    slew_rise = (mode == AnalysisMode::MAX) ? neg_inf : pos_inf;
    slew_fall = (mode == AnalysisMode::MAX) ? neg_inf : pos_inf;
    fanouts.clear();
    reset(mode);
  }
};

struct GbaGraphy {
  std::vector<GbaNode> nodes;
  std::vector<GbaPath> paths;

  std::unordered_map<size_t, size_t> pt_to_node;
  std::vector<size_t> end_node;

  TopoVisitor topo;
};

} // namespace sta

#endif // STA_GBA_GRAPH_HPP
