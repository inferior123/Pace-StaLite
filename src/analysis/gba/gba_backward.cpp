#include "sta/sta_data_structures.hpp"
#include "sta/sta_logger.hpp"

#include <cstddef>
#include <limits>

namespace sta {

void STAWorker::gba_reset_required_state() {
  const bool use_max = (analysis_mode == AnalysisMode::MAX);
  const double pos_inf = std::numeric_limits<double>::infinity();
  const double neg_inf = -pos_inf;
  const double init_required = use_max ? pos_inf : neg_inf;
  const std::size_t sentinel = std::numeric_limits<std::size_t>::max();

  for (auto &node : gba_graphy_.nodes) {
    node.required_rise = init_required;
    node.required_fall = init_required;
    node.next_node_rise = sentinel;
    node.next_node_fall = sentinel;
    node.next_path_rise = sentinel;
    node.next_path_fall = sentinel;
  }
}

void STAWorker::gba_seed_endpoint_requireds() {
  const bool use_max = (analysis_mode == AnalysisMode::MAX);
  for (const std::size_t end_pt : gba_graphy_.end_node) {
    auto it = gba_graphy_.pt_to_node.find(end_pt);
    if (it == gba_graphy_.pt_to_node.end()) {
      continue;
    }

    GbaNode &node = gba_graphy_.nodes[it->second];
    const std::size_t pseudo_path_idx = (node.prev_path_rise < gba_graphy_.paths.size())
                                            ? node.prev_path_rise
                                            : node.prev_path_fall;
    if (pseudo_path_idx == std::numeric_limits<std::size_t>::max()) {
      continue;
    }

    (void)use_max;
    // Endpoint required seed is currently path-centric and is finalized in
    // run_gba_backward_compute_required_and_slack(). Node-level values are
    // initialized here to keep a deterministic backward pipeline skeleton.
  }
}

void STAWorker::gba_backward_rebuild_node_required_links() {
  const std::size_t sentinel = std::numeric_limits<std::size_t>::max();
  for (auto &node : gba_graphy_.nodes) {
    node.next_node_rise = sentinel;
    node.next_node_fall = sentinel;
    node.next_path_rise = sentinel;
    node.next_path_fall = sentinel;
  }

  for (const auto &path : gba_graphy_.paths) {
    if (path.endnode >= gba_graphy_.nodes.size() ||
        path.startnode >= gba_graphy_.nodes.size()) {
      continue;
    }
    GbaNode &from = gba_graphy_.nodes[path.startnode];
    if (path.dir == TransitionDirection::RISING) {
      from.next_node_rise = path.endnode;
      from.next_path_rise = path.path_idx;
    } else if (path.dir == TransitionDirection::FALLING) {
      from.next_node_fall = path.endnode;
      from.next_path_fall = path.path_idx;
    }
  }
}

void STAWorker::gba_backward_propagate_required() {
  // Current implementation keeps required/slack path-centric.
  // This hook reserves the node-centric backward propagation stage.
}

void STAWorker::run_gba_backward_compute_required_and_slack() {
  gba_reset_required_state();
  gba_seed_endpoint_requireds();
  gba_backward_rebuild_node_required_links();
  gba_backward_propagate_required();

  for (std::size_t i = 0; i < res.paths.size(); ++i) {
    const auto [required, slack] = compute_require_and_slack(i);
    (void)required;
    (void)slack;
  }

  LOG_INFO << "[gba] backward stage ready: paths=" << res.paths.size() << "\n";
}

} // namespace sta
