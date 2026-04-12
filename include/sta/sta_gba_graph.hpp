#ifndef STA_GBA_GRAPH_HPP
#define STA_GBA_GRAPH_HPP

#include "../cell/cell_data_structure.hpp"
#include "sta_timing_core.hpp"

#include <cstddef>
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

  size_t prev_node_rise;
  size_t prev_node_fall;
  size_t prev_path_rise;
  size_t prev_path_fall;

  std::optional<double> library_setup_time_rise;
  std::optional<double> library_setup_time_fall;
  std::optional<double> library_hold_time_rise;
  std::optional<double> library_hold_time_fall;

  std::vector<GbaPath> fanouts;
};

struct GbaGraphy {
  std::vector<GbaNode> nodes;
  std::vector<GbaPath> paths;

  std::unordered_map<size_t, size_t> pt_to_node;
  std::vector<size_t> end_node;

  std::vector<std::size_t> topo_order;
};

} // namespace sta

#endif // STA_GBA_GRAPH_HPP
