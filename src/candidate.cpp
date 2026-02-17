#include "sta/sta_data_structures.hpp"
#include <cassert>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <limits>
#include <string>
#include <unordered_set>

#include "sta/debug.h"

using namespace verilog;

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

std::size_t STAWorker::get_or_create_point_node(Instance *inst,
                                                const SignalBit &bit,
                                                const std::string &port_name) {
  return get_or_create_point(inst, port_name, bit, COMB_PIN);
}

std::size_t STAWorker::get_or_create_point(Instance *inst,
                                           const std::string &port_name,
                                           std::optional<SignalBit> bit,
                                           PointType type) {
  TimingPointRefKey key{inst, port_name, bit};
  auto it = res.point_index.find(key);
  if (it != res.point_index.end()) {
    return it->second;
  }

  std::size_t id = res.points.size();
  res.point_index.emplace(key, id);

  TimingPointRef point;
  point.id = id;
  point.inst = inst;
  point.port_name = port_name;
  point.bit = bit;
  point.type = type;
  point.fanouts = {};

  res.points.push_back(std::move(point));
  return id;
}

namespace {

void print_candidate_nodes(
    const sta::CandidateGraphy &cg, const sta::TimingRunResult &res,
    const std::unordered_set<std::size_t> &startpoint_nodes) {
  std::cout << "\n=== NODES ===\n";
  for (const auto &node : cg.nodes) {
    bool is_startpoint = startpoint_nodes.count(node.id) > 0;
    std::cout << "  [" << std::setw(3) << node.id << "] ";
    if (is_startpoint)
      std::cout << "[START] ";
    if (node.point_idx >= res.points.size()) {
      std::cout << "pt?" << node.point_idx << " | related point num: "
                << node.relate_candidate_point.size() << "\n";
      continue;
    }
    const sta::TimingPointRef &pt = res.points[node.point_idx];
    if (pt.inst == nullptr)
      std::cout << "PORT: ";
    else
      std::cout << "INST: " << pt.inst->instance_name << " ("
                << pt.inst->module_name << ") ";
    std::cout << "port:\"" << pt.port_name << "\"";
    std::cout << " | fanout_paths: " << node.fanout_paths.size();
    if (!node.fanout_paths.empty()) {
      std::cout << " -> [";
      for (size_t i = 0; i < node.fanout_paths.size() && i < 5; ++i) {
        if (i > 0)
          std::cout << ", ";
        std::cout << node.fanout_paths[i];
      }
      if (node.fanout_paths.size() > 5)
        std::cout << ", ...";
      std::cout << "]";
    }
    std::cout << "\n";
  }
}

void print_candidate_paths(
    const sta::CandidateGraphy &cg,
    const std::unordered_set<std::size_t> &startpoint_nodes) {
  std::cout << "\n=== PATHS ===\n";
  for (const auto &path : cg.paths) {
    bool is_startpath = startpoint_nodes.count(path.start_node) > 0;
    std::cout << "  [" << std::setw(3) << path.id << "] ";
    if (is_startpath)
      std::cout << "[START] ";
    std::cout << "Node[" << path.start_node << "] -> Node[" << path.end_node
              << "]";
    if (path.next_path.has_value())
      std::cout << " -> Path[" << path.next_path.value() << "]";
    else
      std::cout << " [END]";
    std::cout << " | fanouts_edge: " << path.fanouts_edge.size();
    if (!path.fanouts_edge.empty()) {
      std::cout << " | eid";
      for (size_t i = 0; i < path.fanouts_edge.size() && i < 5; ++i)
        std::cout << " " << path.fanouts_edge[i];
      if (path.fanouts_edge.size() > 5)
        std::cout << " ...";
    }
    std::cout << "\n";
  }
}
} // namespace

void STAWorker::build_candidate_graphy_dfs() {
  build_res_edges();

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

// 沿 path 的一段边 (from_pt -> to_pt) 计算延迟与输出方向。
// 若为 WIRE：delay=0，slew/方向透传；
// 若为 COMB_ARC/SEQ_ARC：遍历所有 related_pin 匹配、且 sdf_cond 为空的
// TimingArc，分别计算一遍延迟，在 MAX 模式下取最大，在 MIN 模式下取最小。
static void segment_delay_slew(const TimingRunResult &res,
                               const celllib::CellLibrary *cell_library_,
                               AnalysisMode mode, std::size_t from_pt,
                               std::size_t to_pt, double prev_slew,
                               TransitionDirection cur_dir, double &out_delay,
                               double &out_slew, TransitionDirection &out_dir) {
  out_delay = 0.0;
  out_slew = prev_slew;
  out_dir = cur_dir;
  if (from_pt >= res.points.size() || to_pt >= res.points.size())
    return;
  const TimingPointRef &from_ref = res.points[from_pt];
  const TimingPointRef &to_ref = res.points[to_pt];
  const TimingEdge *edge = nullptr;
  for (const auto &e : from_ref.fanouts) {
    if (e.target_point == to_pt) {
      edge = &e;
      break;
    }
  }
  if (!edge || edge->type == WIRE)
    return;
  Instance *inst = to_ref.inst;
  if (!inst || !cell_library_) {
    return;
  }
  const auto *cell = cell_library_->get_cell(inst->module_name);
  if (!cell)
    return;
  const std::string &output_pin_name = to_ref.port_name;
  const auto *output_pin = cell->get_pin(output_pin_name);
  if (!output_pin)
    return;
  // SEQ_ARC (clk2q)：库里弧的 related_pin 是 cell 的时钟 pin 名（如
  // CK），不是顶层时钟端口名
  std::string related_pin = from_ref.port_name;
  if (edge->type == SEQ_ARC && cell->ff.has_value() &&
      cell->ff->clocked_on.has_value()) {
    related_pin = cell->ff->clocked_on.value();
  }
  bool is_clock_to_q = (edge->type == SEQ_ARC);

  // 遍历所有满足条件的 arc，分别计算 delay 与 slew，按 mode 各自选最大/最小
  const celllib::Pin *pin_mut = output_pin;
  bool has_candidate = false;
  double best_delay =
      (mode == AnalysisMode::MAX ? -std::numeric_limits<double>::infinity()
                                 : std::numeric_limits<double>::infinity());
  double best_slew =
      (mode == AnalysisMode::MAX ? -std::numeric_limits<double>::infinity()
                                 : std::numeric_limits<double>::infinity());
  TransitionDirection best_dir = cur_dir;

  for (const auto &arc : pin_mut->timing_arcs) {
    bool is_combinational =
        (arc.timing_type == celllib::TimingType::COMBINATIONAL);
    bool is_c2q = (arc.timing_type == celllib::TimingType::RISING_EDGE ||
                   arc.timing_type == celllib::TimingType::FALLING_EDGE);
    if ((!is_combinational && !is_c2q) || arc.related_pin != related_pin)
      continue;

    TransitionDirection dir_tmp =
        speculate_transition_direction(is_clock_to_q, arc, cur_dir);
    if (dir_tmp == TransitionDirection::UNKNOWN) {
      assert(false);
    }

    // candidate 精确模式：按 output 上升/下降选用预计算好的 rise/fall 负载
    double load_cap = 0.0;
    if (dir_tmp == TransitionDirection::RISING)
      load_cap = to_ref.rise_cap;
    else if (dir_tmp == TransitionDirection::FALLING)
      load_cap = to_ref.fall_cap;
    else
      load_cap = to_ref.load_cap;

    double delay_tmp = 0.0;
    double slew_tmp = 0.0;
    if (dir_tmp == TransitionDirection::RISING) {
      delay_tmp = caculate_delay_rise(arc, cell_library_, prev_slew, load_cap);
      slew_tmp =
          caculate_transition_rise(arc, cell_library_, prev_slew, load_cap) /
          1000;
    } else {
      delay_tmp = caculate_delay_fall(arc, cell_library_, prev_slew, load_cap);
      slew_tmp =
          caculate_transition_fall(arc, cell_library_, prev_slew, load_cap) /
          1000;
    }

    if (!has_candidate) {
      has_candidate = true;
      best_delay = delay_tmp;
      best_slew = slew_tmp;
      best_dir = dir_tmp;
    } else {
      if ((mode == AnalysisMode::MAX && delay_tmp > best_delay) ||
          (mode == AnalysisMode::MIN && delay_tmp < best_delay)) {
        best_delay = delay_tmp;
        best_slew = slew_tmp;
        best_dir = dir_tmp;
      }
      if ((mode == AnalysisMode::MAX && slew_tmp > best_slew) ||
          (mode == AnalysisMode::MIN && slew_tmp < best_slew)) {
        best_slew = slew_tmp;
      }
    }
  }

  if (!has_candidate)
    return;

  out_delay = best_delay;
  out_slew = best_slew;
  out_dir = best_dir;
}

void STAWorker::caculate_candidate_load_cap() {
  if (!cell_library_)
    return;

  std::deque<std::size_t> queue(input_clk_point_ids.begin(),
                                input_clk_point_ids.end());
  std::unordered_set<std::size_t> visited;
  while (!queue.empty()) {
    std::size_t pt_id = queue.front();
    queue.pop_front();
    if (visited.count(pt_id))
      continue;
    visited.insert(pt_id);
    TimingPointRef &pt = res.points[pt_id];
    if (pt.type == COMB_PIN || pt.type == REGQ || pt.type == INPUT) {
      for (const TimingEdge &e : pt.fanouts) {
        if (e.target_point >= res.points.size())
          continue;
        const TimingPointRef &target = res.points[e.target_point];
        if (!target.inst) // 输出是output
          continue;
        const auto *fanout_cell =
            cell_library_->get_cell(target.inst->module_name);
        if (!fanout_cell)
          continue;
        const auto *input_pin = fanout_cell->get_pin(target.port_name);
        if (!input_pin)
          continue;
        const bool use_max = (get_analysis_mode() == AnalysisMode::MAX);

        if (use_max) {
          if (input_pin->rise_capacitance_max.has_value())
            pt.rise_cap += input_pin->rise_capacitance_max.value();
          else if (input_pin->capacitance.has_value())
            pt.rise_cap += input_pin->capacitance.value();
        } else {
          // min 模式：优先 min，若 min 无值则用 max
          if (input_pin->rise_capacitance_min.has_value())
            pt.rise_cap += input_pin->rise_capacitance_min.value();
          else if (input_pin->rise_capacitance_max.has_value())
            pt.rise_cap += input_pin->rise_capacitance_max.value();
          else if (input_pin->capacitance.has_value())
            pt.rise_cap += input_pin->capacitance.value();
        }

        if (use_max) {
          if (input_pin->fall_capacitance_max.has_value())
            pt.fall_cap += input_pin->fall_capacitance_max.value();
          else if (input_pin->capacitance.has_value())
            pt.fall_cap += input_pin->capacitance.value();
        } else {
          if (input_pin->fall_capacitance_min.has_value())
            pt.fall_cap += input_pin->fall_capacitance_min.value();
          else if (input_pin->fall_capacitance_max.has_value())
            pt.fall_cap += input_pin->fall_capacitance_max.value();
          else if (input_pin->capacitance.has_value())
            pt.fall_cap += input_pin->capacitance.value();
        }
      }
    }
    for (const TimingEdge &e : pt.fanouts) {
      if (visited.count(e.target_point) == 0)
        queue.push_back(e.target_point);
    }
  }
}

CandidatePathSegmentResult
STAWorker::compute_candidate_path_with_input(std::size_t path_id,
                                             TransitionDirection input_dir,
                                             double input_slew_ns) const {
  CandidatePathSegmentResult out;
  if (path_id >= candidate_graphy_.paths.size() || !cell_library_) {
    return out;
  }
  const CandidatePath &path = candidate_graphy_.paths[path_id];
  const std::size_t start_pt =
      candidate_graphy_.nodes[path.start_node].point_idx;

  double total_delay = 0.0;
  double prev_slew_ns = input_slew_ns;
  TransitionDirection cur_dir = input_dir;
  std::size_t from_pt = start_pt;

  AnalysisMode mode = get_analysis_mode();

  for (std::size_t eid : path.fanouts_edge) {
    if (eid >= res.edges.size())
      continue;
    std::size_t to_pt = res.edges[eid].target_point;
    double seg_delay = 0.0, seg_slew_ns = prev_slew_ns;
    TransitionDirection seg_dir = cur_dir;
    segment_delay_slew(res, cell_library_, mode, from_pt, to_pt, prev_slew_ns,
                       cur_dir, seg_delay, seg_slew_ns, seg_dir);
    total_delay += seg_delay;

    TimingStep step;
    step.start_point = from_pt;
    step.end_point = to_pt;
    step.incr = seg_delay;
    step.slew = seg_slew_ns * 1000.0; // ns -> ps
    step.cap_load = 0.0;
    if (to_pt < res.points.size()) {
      const auto &to_ref = res.points[to_pt];
      if (to_ref.inst) {
        if (seg_dir == TransitionDirection::RISING)
          step.cap_load = to_ref.rise_cap;
        else if (seg_dir == TransitionDirection::FALLING)
          step.cap_load = to_ref.fall_cap;
        else {
          std::cerr << "[Warning] use the fall back, should not reach here"
                    << std::endl;
          step.cap_load = to_ref.load_cap;
        }
      }
    }
    step.arrival = total_delay;
    step.dir = seg_dir;
    out.steps.push_back(step);

    prev_slew_ns = seg_slew_ns;
    cur_dir = seg_dir;
    from_pt = to_pt;
  }

  out.total_delay_ps = total_delay;
  out.output_slew_ns = prev_slew_ns;
  out.output_dir = cur_dir;
  return out;
}

CandidatePathSegmentResult STAWorker::compute_one_edge_non_unate_segment(
    std::size_t from_pt, std::size_t to_pt, TransitionDirection output_dir,
    double input_slew_ns) const {
  CandidatePathSegmentResult out;
  if (from_pt >= res.points.size() || to_pt >= res.points.size() ||
      !cell_library_)
    return out;
  const TimingPointRef &from_ref = res.points[from_pt];
  const TimingPointRef &to_ref = res.points[to_pt];
  if (!from_ref.inst || !to_ref.inst || from_ref.inst != to_ref.inst)
    return out;
  const auto *cell = cell_library_->get_cell(to_ref.inst->module_name);
  if (!cell)
    return out;
  const celllib::Pin *out_pin = cell->get_pin(to_ref.port_name);
  if (!out_pin)
    return out;

  // 遍历所有满足条件的 arc，分别计算 delay 与 slew，按 mode 各自选最大/最小
  const celllib::Pin *pin_mut = out_pin;
  bool has_candidate = false;
  bool has_unate = false;
  double best_delay = (analysis_mode == AnalysisMode::MAX
                           ? -std::numeric_limits<double>::infinity()
                           : std::numeric_limits<double>::infinity());
  double best_slew = (analysis_mode == AnalysisMode::MAX
                          ? -std::numeric_limits<double>::infinity()
                          : std::numeric_limits<double>::infinity());
  double unate_delay = 0.0;
  double unate_transition = 0.0;
  // if (from_pt == 192) {
  //   std::cerr << "[non_unate_debug] from_pt=" << from_pt << " to_pt=" << to_pt
  //             << " inst=" << (to_ref.inst ? to_ref.inst->instance_name : "?")
  //             << "(" << (to_ref.inst ? to_ref.inst->module_name : "?") << ")"
  //             << " output_pin=" << to_ref.port_name
  //             << " input_pin=" << from_ref.port_name
  //             << " output_dir=" << (output_dir == TransitionDirection::RISING
  //                                       ? "R"
  //                                       : (output_dir == TransitionDirection::FALLING ? "F"
  //                                                                                     : "?"))
  //             << " input_slew_ns=" << input_slew_ns
  //             << " rise_cap=" << to_ref.rise_cap
  //             << " fall_cap=" << to_ref.fall_cap
  //             << " mode=" << (analysis_mode == AnalysisMode::MAX ? "MAX" : "MIN")
  //             << "\n";
  // }

  // if(from_pt == 192) {
  //   std::cout << "TIMING ARC SIZE: " << pin_mut->timing_arcs.size() << std::endl;
  // }

  for (const auto &arc : pin_mut->timing_arcs) {
    bool is_combinational =
        (arc.timing_type == celllib::TimingType::COMBINATIONAL);
    bool is_c2q = (arc.timing_type == celllib::TimingType::RISING_EDGE ||
                   arc.timing_type == celllib::TimingType::FALLING_EDGE);
    if ((!is_combinational && !is_c2q) || arc.related_pin != from_ref.port_name)
      continue;

    // candidate 精确模式：按 output 上升/下降选用预计算好的 rise/fall 负载
    double load_cap = 0.0;
    if (output_dir == TransitionDirection::RISING)
      load_cap = to_ref.rise_cap;
    else if (output_dir == TransitionDirection::FALLING)
      load_cap = to_ref.fall_cap;
    else
      assert(false && "should not reach here");

    double delay_tmp = 0.0;
    double slew_tmp = 0.0;
    if (output_dir == TransitionDirection::RISING) {
      delay_tmp =
          caculate_delay_rise(arc, cell_library_, input_slew_ns, load_cap);
      slew_tmp = caculate_transition_rise(arc, cell_library_, input_slew_ns,
                                          load_cap) /
                 1000;
    } else {
      delay_tmp =
          caculate_delay_fall(arc, cell_library_, input_slew_ns, load_cap);
      slew_tmp = caculate_transition_fall(arc, cell_library_, input_slew_ns,
                                          load_cap) /
                 1000;
    }

    if(arc.sdf_cond->empty()) {
      has_unate = true;
      unate_delay = delay_tmp;
      unate_transition = slew_tmp;
    }

    // if (from_pt == 192) {
    //   std::cerr << "  [non_unate_debug] arc related_pin=" << arc.related_pin
    //             << " sdf_cond="
    //             << (arc.sdf_cond.has_value() ? arc.sdf_cond.value()
    //                                          : std::string("none"))
    //             << " is_comb=" << (is_combinational ? "Y" : "N")
    //             << " is_c2q=" << (is_c2q ? "Y" : "N")
    //             << " load_cap=" << load_cap
    //             << " delay_tmp=" << delay_tmp
    //             << " slew_tmp_ns=" << slew_tmp << "\n";
    // }

    if (!has_candidate) {
      has_candidate = true;
      best_delay = delay_tmp;
      best_slew = slew_tmp;
    } else {
      if ((analysis_mode == AnalysisMode::MAX && delay_tmp > best_delay) ||
          (analysis_mode == AnalysisMode::MIN && delay_tmp < best_delay)) {
        best_delay = delay_tmp;
        // best_slew = slew_tmp;
      }
      if ((analysis_mode == AnalysisMode::MAX && slew_tmp > best_slew) ||
          (analysis_mode == AnalysisMode::MIN && slew_tmp < best_slew)) {
        best_slew = slew_tmp;
      }
    }
  }
  
  if(!has_candidate) {
    show_lib_details(to_ref.inst->module_name.c_str(), *cell_library_);
    assert(false);
  }

  if(has_unate) {
    best_delay = unate_delay;
    best_slew = unate_transition;
  }

  out.total_delay_ps = best_delay;
  out.output_slew_ns = best_slew;
  out.output_dir = output_dir;
  TimingStep step;
  step.start_point = from_pt;
  step.end_point = to_pt;
  step.incr = best_delay;
  step.slew = best_slew * 1000;
  step.arrival = best_delay;
  step.dir = output_dir;
  step.cap_load = (output_dir == TransitionDirection::RISING) ? to_ref.rise_cap
                                                              : to_ref.fall_cap;
  out.steps.push_back(std::move(step));
  return out;
}

const char *point_type_str(sta::PointType t) {
  switch (t) {
  case sta::CLK_PIN:
    return "CLK_PIN";
  case sta::INPUT:
    return "INPUT";
  case sta::OUTPUT:
    return "OUTPUT";
  case sta::REGD:
    return "REGD";
  case sta::REGQ:
    return "REGQ";
  case sta::COMB_PIN:
    return "COMB_PIN";
  case sta::CLK_SOURCE:
    return "CLK_COURCE";
  }

  return "?";
}

void STAWorker::compute_path_setup_hold(TimingPathResult &pr) const {
  if (pr.steps.empty() || pr.endpoint >= res.points.size() || !cell_library_)
    return;
  const auto &end_ref = res.points[pr.endpoint];
  if (end_ref.type != REGD || !end_ref.inst)
    return;
  const auto *cell = cell_library_->get_cell(end_ref.inst->module_name);
  if (!cell)
    return;
  const auto *d_pin = cell->get_pin(end_ref.port_name);
  if (!d_pin)
    return;
  const TimingStep &st = pr.steps.back();
  double data_trans_ns = st.slew / 1000.0;
  double clk_trans = 0.0;
  for (const auto &arc : d_pin->timing_arcs) {
    using TT = celllib::TimingType;
    if (arc.timing_type == TT::SETUP_RISING ||
        arc.timing_type == TT::SETUP_FALLING) {
      double s = (st.dir == TransitionDirection::RISING
                      ? caculate_setup_rise(arc, cell_library_, data_trans_ns,
                                            clk_trans)
                      : caculate_setup_fall(arc, cell_library_, data_trans_ns,
                                            clk_trans)) *
                 1000.0;
      pr.library_setup_time = s;
    } else if (arc.timing_type == TT::HOLD_RISING ||
               arc.timing_type == TT::HOLD_FALLING) {
      double h = (st.dir == TransitionDirection::RISING
                      ? caculate_hold_rise(arc, cell_library_, data_trans_ns,
                                           clk_trans)
                      : caculate_hold_fall(arc, cell_library_, data_trans_ns,
                                           clk_trans)) *
                 1000.0;
      pr.library_hold_time = h;
    }
    // 其他 arc 类型（如 COMBINATIONAL 等）跳过，与原 display 内逻辑一致
  }
}

void STAWorker::display_result_path_detail(const TimingPathResult &pr) const {
  std::cout << "\n--- Result Path #" << pr.index << " ---" << std::endl;
  std::cout << "  startpoint: pt" << pr.startpoint;
  if (pr.startpoint < res.points.size()) {
    const auto &p = res.points[pr.startpoint];
    std::cout << " ";
    if (p.inst)
      std::cout << p.inst->instance_name << "(" << p.inst->module_name << ")/";
    std::cout << p.port_name << " type=" << point_type_str(p.type);
  }
  std::cout << "\n  endpoint:   pt" << pr.endpoint;
  std::cout << "\n  path id:    #" << pr.index;
  if (pr.endpoint < res.points.size()) {
    const auto &p = res.points[pr.endpoint];
    std::cout << " ";
    if (p.inst)
      std::cout << p.inst->instance_name << "(" << p.inst->module_name << ")/";
    std::cout << p.port_name << " type=" << point_type_str(p.type);
  }
  std::cout << "\n  data_arrival=" << pr.data_arrival_time << "ps\n";

  for (std::size_t i = 0; i < pr.steps.size(); ++i) {
    const TimingStep &st = pr.steps[i];
    std::cout << "  [" << i << "] pt" << st.start_point << " -> pt"
              << st.end_point;
    if (st.end_point < res.points.size()) {
      const auto &to_ref = res.points[st.end_point];
      std::cout << " ";
      if (to_ref.inst)
        std::cout << to_ref.inst->instance_name << "("
                  << to_ref.inst->module_name << ")/";
      std::cout << to_ref.port_name;
    }
    char d = (st.dir == TransitionDirection::RISING)
                 ? 'r'
                 : (st.dir == TransitionDirection::FALLING ? 'f' : '?');
    std::cout << " dir=" << d << " incr=" << st.incr << "ps"
              << " slew=" << (st.slew / 1000.0) << "ns"
              << " cap=" << st.cap_load << "pf"
              << " arrival=" << st.arrival << "ps";
    bool is_last = (i == pr.steps.size() - 1);
    if (is_last) {
      if (pr.library_setup_time.has_value())
        std::cout << " setup=" << pr.library_setup_time.value() << "ps";
      if (pr.library_hold_time.has_value())
        std::cout << " hold=" << pr.library_hold_time.value() << "ps";
    }
    std::cout << "\n";

    // Extra debug: print arc details (including timing_sense) for this step
    if (cell_library_ && st.start_point < res.points.size() &&
        st.end_point < res.points.size()) {
      const auto &from_ref = res.points[st.start_point];
      const auto &to_ref = res.points[st.end_point];

      if (to_ref.inst) {
        const auto *cell = cell_library_->get_cell(to_ref.inst->module_name);
        if (cell) {
          const auto *out_pin = cell->get_pin(to_ref.port_name);
          if (out_pin) {
            // Find edge type and related_pin (with clk2q remap)
            const TimingEdge *edge = nullptr;
            for (const auto &e : from_ref.fanouts) {
              if (e.target_point == st.end_point) {
                edge = &e;
                break;
              }
            }

            std::string related_pin = from_ref.port_name;
            if (edge && edge->type == SEQ_ARC && cell->ff.has_value() &&
                cell->ff->clocked_on.has_value()) {
              related_pin = cell->ff->clocked_on.value();
            }

            auto timing_type_str_local = [](celllib::TimingType t) {
              using T = celllib::TimingType;
              switch (t) {
              case T::COMBINATIONAL:
                return "COMBINATIONAL";
              case T::SETUP_RISING:
                return "SETUP_RISING";
              case T::SETUP_FALLING:
                return "SETUP_FALLING";
              case T::HOLD_RISING:
                return "HOLD_RISING";
              case T::HOLD_FALLING:
                return "HOLD_FALLING";
              case T::RISING_EDGE:
                return "RISING_EDGE";
              case T::FALLING_EDGE:
                return "FALLING_EDGE";
              case T::CLEAR:
                return "CLEAR";
              case T::PRESET:
                return "PRESET";
              case T::MIN_PULSE_WIDTH:
                return "MIN_PULSE_WIDTH";
              }
              return "?";
            };

            auto timing_sense_str_local = [](celllib::TimingSense s) {
              using S = celllib::TimingSense;
              switch (s) {
              case S::POSITIVE_UNATE:
                return "POSITIVE_UNATE";
              case S::NEGATIVE_UNATE:
                return "NEGATIVE_UNATE";
              case S::NON_UNATE:
                return "NON_UNATE";
              }
              return "?";
            };

            // Print all matching arcs for this from->to step
            for (const auto &arc : out_pin->timing_arcs) {
              bool is_combinational =
                  (arc.timing_type == celllib::TimingType::COMBINATIONAL);
              bool is_c2q =
                  (arc.timing_type == celllib::TimingType::RISING_EDGE ||
                   arc.timing_type == celllib::TimingType::FALLING_EDGE);
              if ((!is_combinational && !is_c2q) ||
                  arc.related_pin != related_pin)
                continue;

              std::cout << "      arc: related_pin=" << arc.related_pin
                        << " type=" << timing_type_str_local(arc.timing_type)
                        << " sense=" << timing_sense_str_local(arc.timing_sense)
                        << " sdf_cond=";
              if (arc.sdf_cond.has_value())
                std::cout << "\"" << arc.sdf_cond.value() << "\"";
              else
                std::cout << "none";
              std::cout << "\n";
            }
          }
        }
      }
    }
  }

  AnalysisMode mode = get_analysis_mode();
  if (mode == AnalysisMode::MAX) {
    double required = static_cast<double>(get_effective_clock_period());
    double setup_ps = pr.library_setup_time.value_or(0.0);
    if (setup_ps > 0.0)
      required -= setup_ps;
    double slack = required - pr.data_arrival_time;
    std::cout << "  required=" << required << "ps  slack=" << slack << "ps\n";
  } else {
    double required = pr.library_hold_time.value_or(0.0);
    double slack = pr.data_arrival_time - required;
    std::cout << "  required=" << required << "ps  slack=" << slack << "ps\n";
  }
  std::cout << "  ---\n";
}

// 判断节点是否为终点（D 端或 OUTPUT），非则多为 non-unate 等，需继续链下去
static bool is_terminal_node(const TimingRunResult &res,
                             const CandidateGraphy &cg, std::size_t node_id) {
  if (node_id >= cg.nodes.size())
    return true;
  std::size_t pt_id = cg.nodes[node_id].point_idx;
  if (pt_id >= res.points.size())
    return true;
  PointType t = res.points[pt_id].type;
  return (t == REGD || t == OUTPUT);
}

void STAWorker::run_candidate_graphy_dfs() {
  res.paths.clear();
  // 路径指纹去重：相同 (start,end,step 序列) 只保留一条，避免重复边或
  // reconvergence 产生大量重复
  std::unordered_set<std::string> path_printed;

  static const bool run_dfs_debug = false; // 调试时置 true，调完可改 false

  // 收集所有起点：input 或 clk 节点
  std::vector<std::size_t> start_node_ids;
  for (std::size_t i = 0; i < candidate_graphy_.nodes.size(); ++i) {
    const auto &node = candidate_graphy_.nodes[i];
    if (node.point_idx >= res.points.size())
      continue;
    PointType t = res.points[node.point_idx].type;
    if (t == INPUT || t == CLK_PIN)
      start_node_ids.push_back(i);
  }
  if (run_dfs_debug) {
    std::cerr << "[run_candidate_dfs] start_node_ids(" << start_node_ids.size()
              << "):";
    for (std::size_t nid : start_node_ids)
      std::cerr << " n" << nid << "(pt"
                << candidate_graphy_.nodes[nid].point_idx << ")";
    std::cerr << "\n";
  }

  const double initial_slew_ns = 0.0; // 起点 slew，后续可改为 clk_slew 等

  for (std::size_t start_node_id : start_node_ids) {
    const std::size_t start_pt =
        candidate_graphy_.nodes[start_node_id].point_idx;
    if (run_dfs_debug)
      std::cerr << "[run_candidate_dfs] === start_node n" << start_node_id
                << " pt" << start_pt << " ===\n";

    using EmitChainsFn =
        std::function<void(std::size_t, TransitionDirection, double,
                           std::vector<TimingStep>, double)>;
    EmitChainsFn emit_chains;

    auto push_or_continue = [&](std::size_t end_node_id,
                                const CandidatePathSegmentResult &seg,
                                std::vector<TimingStep> new_steps,
                                double new_delay) {
      std::size_t end_pt = candidate_graphy_.nodes[end_node_id].point_idx;
      bool terminal = is_terminal_node(res, candidate_graphy_, end_node_id);
      if (run_dfs_debug)
        std::cerr << "[run_candidate_dfs]   push_or_continue end_n"
                  << end_node_id << " pt" << end_pt << " delay=" << new_delay
                  << " " << (terminal ? "-> PUSH" : "-> recurse") << " dir="
                  << (seg.output_dir == TransitionDirection::RISING
                          ? "R"
                          : (seg.output_dir == TransitionDirection::FALLING
                                 ? "F"
                                 : "?"))
                  << "\n";
      if (terminal) {
        std::string fp =
            std::to_string(start_pt) + "_" + std::to_string(end_pt);
        for (const auto &st : new_steps) {
          fp += "_" + std::to_string(st.start_point) + "-" +
                std::to_string(st.end_point);
          fp += (st.dir == TransitionDirection::RISING)
                    ? "r"
                    : (st.dir == TransitionDirection::FALLING ? "f" : "?");
        }
        if (!path_printed.insert(fp).second) {
          if (run_dfs_debug)
            std::cerr << "[run_candidate_dfs]   (dup fp, skip)\n";
          return;
        }
        TimingPathResult pr;
        pr.startpoint = start_pt;
        pr.endpoint = end_pt;
        pr.data_arrival_time = new_delay;
        pr.steps = std::move(new_steps);
        pr.index = res.paths.size();
        if (start_pt < res.points.size() && end_pt < res.points.size())
          pr.group = classify_path_group(
              effective_start_type_for_group(res.points[start_pt]),
              res.points[end_pt].type);
        compute_path_setup_hold(pr);
        if (run_dfs_debug)
          std::cerr << "[run_candidate_dfs]   PUSH path #" << res.paths.size()
                    << " start_pt" << pr.startpoint << " -> end_pt"
                    << pr.endpoint << " arr=" << pr.data_arrival_time << "\n";
        res.paths.push_back(std::move(pr));
      } else {
        emit_chains(end_node_id, seg.output_dir, seg.output_slew_ns,
                    std::move(new_steps), new_delay);
      }
    };

    emit_chains = [&](std::size_t cur_node_id, TransitionDirection cur_dir,
                      double cur_slew_ns, std::vector<TimingStep> steps_so_far,
                      double delay_so_far) {
      const CandidateNode &cur_node = candidate_graphy_.nodes[cur_node_id];
      const std::size_t cur_pt = cur_node.point_idx;
      if (run_dfs_debug)
        std::cerr << "[run_candidate_dfs] emit_chains cur_n" << cur_node_id
                  << " pt" << cur_pt << " dir="
                  << (cur_dir == TransitionDirection::RISING ? "R" : "F")
                  << " delay_so_far=" << delay_so_far
                  << " fanout_paths=" << cur_node.fanout_paths.size()
                  << " relate=" << cur_node.relate_candidate_point.size()
                  << "\n";

      // fanout_paths 里全是 unate 段，不会包含 non-unate 边
      for (std::size_t path_id : cur_node.fanout_paths) {
        const CandidatePath &path = candidate_graphy_.paths[path_id];
        std::size_t end_node_id = path.end_node;
        if (run_dfs_debug)
          std::cerr << "[run_candidate_dfs]   path" << path_id << " -> end_n"
                    << end_node_id << " pt"
                    << candidate_graphy_.nodes[end_node_id].point_idx
                    << " unate\n";
        CandidatePathSegmentResult seg =
            compute_candidate_path_with_input(path_id, cur_dir, cur_slew_ns);
        std::vector<TimingStep> new_steps = steps_so_far;
        for (const TimingStep &s : seg.steps) {
          TimingStep step = s;
          step.arrival += delay_so_far;
          new_steps.push_back(std::move(step));
        }
        double new_delay = delay_so_far + seg.total_delay_ps;
        push_or_continue(end_node_id, seg, std::move(new_steps), new_delay);
      }

      // relate_candidate_point：non-unate 单边，不经过
      // segment_delay_slew，rise/fall 各算一次
      for (std::size_t to_node_id : cur_node.relate_candidate_point) {
        if (to_node_id >= candidate_graphy_.nodes.size())
          continue;
        std::size_t to_pt = candidate_graphy_.nodes[to_node_id].point_idx;
        if (run_dfs_debug)
          std::cerr << "[run_candidate_dfs]   relate -> end_n" << to_node_id
                    << " pt" << to_pt << " (rise+fall)\n";
        for (TransitionDirection dir :
             {TransitionDirection::RISING, TransitionDirection::FALLING}) {
          CandidatePathSegmentResult seg = compute_one_edge_non_unate_segment(
              cur_pt, to_pt, dir, cur_slew_ns);
          std::vector<TimingStep> new_steps = steps_so_far;
          for (const TimingStep &s : seg.steps) {
            TimingStep step = s;
            step.arrival += delay_so_far;
            new_steps.push_back(std::move(step));
          }
          double new_delay = delay_so_far + seg.total_delay_ps;
          push_or_continue(to_node_id, seg, std::move(new_steps), new_delay);
        }
      }
    };

    for (bool is_rise : {true, false}) {
      TransitionDirection dir =
          is_rise ? TransitionDirection::RISING : TransitionDirection::FALLING;
      if (run_dfs_debug)
        std::cerr << "[run_candidate_dfs] --- branch "
                  << (is_rise ? "rise" : "fall") << " from n" << start_node_id
                  << " ---\n";
      emit_chains(start_node_id, dir, initial_slew_ns, {}, 0.0);
    }
  }
  if (run_dfs_debug)
    std::cerr << "[run_candidate_dfs] total paths=" << res.paths.size() << "\n";
}

} // namespace sta
