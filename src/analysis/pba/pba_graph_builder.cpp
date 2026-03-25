#include "sta/sta_data_structures.hpp"
#include <cassert>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>

#include "sta/debug.h"

namespace sta {

std::size_t STAWorker::get_or_create_candidate_node(std::size_t point_idx) {
  if (point_idx >= res.points.size())
    return SIZE_MAX;
  auto it = candidate_graphy_.point_to_node.find(point_idx);
  if (it != candidate_graphy_.point_to_node.end())
    return it->second;

  std::size_t id = candidate_graphy_.nodes.size();
  candidate_graphy_.point_to_node.emplace(point_idx, id);

  CandidateNode node;
  node.id = id;
  node.point_idx = point_idx;
  node.relate_candidate_point = {};
  node.fanout_paths = {};
  candidate_graphy_.nodes.push_back(std::move(node));

  return id;
}

void STAWorker::build_candidate_graphy_dfs() {
  candidate_graphy_.paths.clear();
  candidate_timing_queue.clear();
  for (auto &node : candidate_graphy_.nodes) {
    node.fanout_paths.clear();
  }

  // 起点：input_clk_point_ids（顶层 input + clk）
  for (std::size_t pt_id : input_clk_point_ids) {
    get_or_create_candidate_node(pt_id);
  }

  // 终点：REGD、OUTPUT，以及 build_fanouts 中已建的 non-unate output、clk2q 等
  // candidate 点
  for (const auto &pt : res.points) {
    if (pt.type == REGD || pt.type == OUTPUT)
      get_or_create_candidate_node(pt.id);
  }

  // 所有 candidate 点（含 non-unate、clk2q）
  std::unordered_set<std::size_t> candidate_point_ids;
  for (const auto &kv : candidate_graphy_.point_to_node)
    candidate_point_ids.insert(kv.first);

  if (candidate_point_ids.empty()) {
    std::cerr << "  [WARNING] No candidate points for DFS\n";
    return;
  }

  std::unordered_set<std::size_t> visited;
  std::function<void(std::size_t pt_id, std::size_t start_node_id,
                     std::vector<std::size_t> path_edge_indices)>
      dfs = [&](std::size_t pt_id, std::size_t start_node_id,
                std::vector<std::size_t> path_edge_indices) {
        if (pt_id >= res.points.size() || visited.count(pt_id))
          return;
        visited.insert(pt_id);

        const std::size_t start_pt_id =
            candidate_graphy_.nodes[start_node_id].point_idx;
        const bool is_start_pt = (pt_id == start_pt_id);

        if (!is_start_pt && candidate_point_ids.count(pt_id)) {
          std::size_t end_node_id = candidate_graphy_.point_to_node.at(pt_id);
          // 起点到当前 candidate 的 path，用拷贝以便后面继续 DFS
          CandidatePath path;
          path.id = candidate_graphy_.paths.size();
          path.start_node = start_node_id;
          path.end_node = end_node_id;
          path.next_path = std::nullopt;
          path.fanouts_edge = path_edge_indices;
          const std::size_t path_id = path.id;
          candidate_graphy_.nodes[start_node_id].fanout_paths.push_back(
              path_id);
          candidate_graphy_.paths.push_back(std::move(path));

          const TimingPointRef &start_pt = res.points[start_pt_id];
          bool is_startpoint =
              (start_pt.type == INPUT || start_pt.type == CLK_PIN);
          if (is_startpoint)
            candidate_timing_queue.push_back(path_id);

          // relate_candidate_point 只记「能到的 output 节点」，不建
          // CandidatePath（path 里只含 unate 边，non-unate 在 run 里单独算） 不
          // return，继续对非 related 的 fanout 做 DFS
        }

        // 若当前是 candidate，其 relate_candidate_point 对应的 to_pt 不再从这里
        // DFS（已在上方记单边 path）
        std::unordered_set<std::size_t> related_output_pts;
        if (candidate_point_ids.count(pt_id)) {
          auto it_node = candidate_graphy_.point_to_node.find(pt_id);
          if (it_node != candidate_graphy_.point_to_node.end()) {
            const auto &rel =
                candidate_graphy_.nodes[it_node->second].relate_candidate_point;
            for (std::size_t nid : rel) {
              if (nid < candidate_graphy_.nodes.size())
                related_output_pts.insert(
                    candidate_graphy_.nodes[nid].point_idx);
            }
          }
        }

        for (const TimingEdge &e : res.points[pt_id].fanouts) {
          std::size_t to_pt = e.target_point;
          if (to_pt >= res.points.size() || visited.count(to_pt))
            continue;
          if (related_output_pts.count(to_pt))
            continue;
          std::size_t eid = res.get_edge_index(pt_id, to_pt);
          if (eid == SIZE_MAX)
            continue;
          std::vector<std::size_t> next_path = path_edge_indices;
          next_path.push_back(eid);
          dfs(to_pt, start_node_id, next_path);
        }

        visited.erase(pt_id);
      };

  for (std::size_t start_pt_id : input_clk_point_ids) {
    auto it = candidate_graphy_.point_to_node.find(start_pt_id);
    if (it == candidate_graphy_.point_to_node.end())
      continue;
    std::size_t start_node_id = it->second;
    visited.clear();
    dfs(start_pt_id, start_node_id, {});
  }

  // 从 non-unate 的 output 再启 DFS，否则 output 到下游的 path 没有（上面在
  // input 截断，不会走到 output）
  for (const auto &node : candidate_graphy_.nodes) {
    for (std::size_t to_node_id : node.relate_candidate_point) {
      if (to_node_id >= candidate_graphy_.nodes.size())
        continue;
      std::size_t output_pt = candidate_graphy_.nodes[to_node_id].point_idx;
      auto it = candidate_graphy_.point_to_node.find(output_pt);
      if (it == candidate_graphy_.point_to_node.end())
        continue;
      std::size_t start_node_id = it->second;
      visited.clear();
      dfs(output_pt, start_node_id, {});
    }
  }

  // 调试：打印节点与路径
  std::unordered_set<std::size_t> startpoint_nodes;
  for (const auto &node : candidate_graphy_.nodes) {
    if (node.point_idx >= res.points.size())
      continue;
    const TimingPointRef &pt = res.points[node.point_idx];
    if (pt.type == INPUT || pt.type == CLK_PIN)
      startpoint_nodes.insert(node.id);
  }

  // print_candidate_nodes(candidate_graphy_, res, startpoint_nodes);
  // print_candidate_paths(candidate_graphy_, startpoint_nodes);
}

} // namespace sta
