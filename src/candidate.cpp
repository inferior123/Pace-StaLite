#include "sta/sta_data_structures.hpp"
#include <cassert>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <string>
#include <unordered_set>

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
          CandidatePath path;
          path.id = candidate_graphy_.paths.size();
          path.start_node = start_node_id;
          path.end_node = end_node_id;
          path.next_path = std::nullopt;
          path.fanouts_edge = std::move(path_edge_indices);
          const std::size_t path_id = path.id;
          candidate_graphy_.nodes[start_node_id].fanout_paths.push_back(
              path_id);
          candidate_graphy_.paths.push_back(std::move(path));

          const TimingPointRef &start_pt = res.points[start_pt_id];
          bool is_startpoint = (start_pt.type == INPUT || start_pt.type == CLK);
          if (is_startpoint)
            candidate_timing_queue.push_back(path_id);

          visited.erase(pt_id);
          return;
        }

        for (const TimingEdge &e : res.points[pt_id].fanouts) {
          std::size_t to_pt = e.target_point;
          if (to_pt >= res.points.size() || visited.count(to_pt))
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

  // 调试：打印节点与路径
  std::unordered_set<std::size_t> startpoint_nodes;
  for (const auto &node : candidate_graphy_.nodes) {
    if (node.point_idx >= res.points.size())
      continue;
    const TimingPointRef &pt = res.points[node.point_idx];
    if (pt.type == INPUT || pt.type == CLK)
      startpoint_nodes.insert(node.id);
  }

  std::cout << "\n=== NODES ===\n";
  for (const auto &node : candidate_graphy_.nodes) {
    bool is_startpoint = startpoint_nodes.count(node.id) > 0;
    std::cout << "  [" << std::setw(3) << node.id << "] ";
    if (is_startpoint)
      std::cout << "[START] ";
    if (node.point_idx >= res.points.size()) {
      std::cout << "pt?" << node.point_idx
                << " | fanout_paths: " << node.fanout_paths.size() << "\n";
      continue;
    }
    const TimingPointRef &pt = res.points[node.point_idx];
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

  std::cout << "\n=== PATHS ===\n";
  for (const auto &path : candidate_graphy_.paths) {
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

// 沿 path 的一段边 (from_pt -> to_pt) 计算延迟与输出方向。
// 若为 WIRE：delay=0，slew/方向透传；若为 COMB_ARC/SEQ_ARC：用 LUT 计算。
static void segment_delay_slew(const TimingRunResult &res,
                               const celllib::CellLibrary *cell_library_,
                               std::size_t from_pt, std::size_t to_pt,
                               double prev_slew, TransitionDirection cur_dir,
                               double &out_delay, double &out_slew,
                               TransitionDirection &out_dir) {
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
  double load_cap = 0.0;
  if (inst->load_capacitance.count(output_pin_name))
    load_cap = inst->load_capacitance.at(output_pin_name);
  // SEQ_ARC (clk2q)：库里弧的 related_pin 是 cell 的时钟 pin 名（如
  // CK），不是顶层时钟端口名
  std::string related_pin = from_ref.port_name;
  if (edge->type == SEQ_ARC && cell->ff.has_value() &&
      cell->ff->clocked_on.has_value()) {
    related_pin = cell->ff->clocked_on.value();
  }
  bool is_clock_to_q = (edge->type == SEQ_ARC);
  const celllib::TimingArc *arc_ptr = nullptr;
  for (const auto &arc : output_pin->timing_arcs) {
    bool is_combinational =
        (arc.timing_type == celllib::TimingType::COMBINATIONAL);
    bool is_c2q = (arc.timing_type == celllib::TimingType::RISING_EDGE ||
                   arc.timing_type == celllib::TimingType::FALLING_EDGE);
    if ((!is_combinational && !is_c2q) || arc.related_pin != related_pin)
      continue;
    arc_ptr = &arc;
    break;
  }
  if (!arc_ptr) {
    return;
  }
  const celllib::TimingArc &arc = *arc_ptr;
  out_dir = speculate_transition_direction(is_clock_to_q, arc, cur_dir);
  if (out_dir == TransitionDirection::UNKNOWN) {
    std::cerr << "[ERROR] UNKNOWN transition direction in candidate path "
                 "segment (from_pt="
              << from_pt << " to_pt=" << to_pt << ")\n";
    assert(false && "candidate path must have determined transition direction");
  }
  if (out_dir == TransitionDirection::RISING) {
    out_delay = caculate_delay_rise(arc, cell_library_, prev_slew, load_cap);
    out_slew =
        caculate_transition_rise(arc, cell_library_, prev_slew, load_cap) /
        1000;
  } else {
    assert(out_dir == TransitionDirection::FALLING);
    out_delay = caculate_delay_fall(arc, cell_library_, prev_slew, load_cap);
    out_slew =
        caculate_transition_fall(arc, cell_library_, prev_slew, load_cap) /
        1000;
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

  for (std::size_t eid : path.fanouts_edge) {
    if (eid >= res.edges.size())
      continue;
    std::size_t to_pt = res.edges[eid].target_point;
    double seg_delay = 0.0, seg_slew_ns = prev_slew_ns;
    TransitionDirection seg_dir = cur_dir;
    segment_delay_slew(res, cell_library_, from_pt, to_pt, prev_slew_ns,
                       cur_dir, seg_delay, seg_slew_ns, seg_dir);
    total_delay += seg_delay;

    TimingStep step;
    step.start_point = from_pt;
    step.end_point = to_pt;
    step.incr = seg_delay;
    step.slew = seg_slew_ns * 1000.0; // ns -> ps
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

const char *point_type_str(sta::PointType t) {
  switch (t) {
  case sta::CLK:
    return "CLK";
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
  }

  return "?";
}

void STAWorker::display_result_path_detail(TimingPathResult &pr,
                                           std::size_t path_index) const {
  std::cout << "\n--- Result Path #" << path_index << " ---\n";
  std::cout << "  startpoint: pt" << pr.startpoint;
  if (pr.startpoint < res.points.size()) {
    const auto &p = res.points[pr.startpoint];
    std::cout << " ";
    if (p.inst)
      std::cout << p.inst->instance_name << "(" << p.inst->module_name << ")/";
    std::cout << p.port_name << " type=" << point_type_str(p.type);
  }
  std::cout << "\n  endpoint:   pt" << pr.endpoint;
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
              << " arrival=" << st.arrival << "ps";

    bool is_last = (i == pr.steps.size() - 1);
    if (is_last && pr.endpoint < res.points.size() && cell_library_) {
      const auto &end_ref = res.points[pr.endpoint];
      if (end_ref.type == REGD && end_ref.inst) {
        const auto *cell = cell_library_->get_cell(end_ref.inst->module_name);
        if (cell) {
          const auto *d_pin = cell->get_pin(end_ref.port_name);
          if (d_pin) {
            double data_trans_ns = st.slew / 1000.0;
            double clk_trans = 0.0;
            for (const auto &arc : d_pin->timing_arcs) {
              using TT = celllib::TimingType;
              if (arc.timing_type == TT::SETUP_RISING ||
                  arc.timing_type == TT::SETUP_FALLING) {
                double s =
                    (st.dir == TransitionDirection::RISING
                         ? caculate_setup_rise(arc, cell_library_,
                                               data_trans_ns, clk_trans)
                         : caculate_setup_fall(arc, cell_library_,
                                               data_trans_ns, clk_trans)) *
                    1000.0;
                std::cout << " setup=" << s << "ps";
                pr.library_setup_time = s;
              } else if (arc.timing_type == TT::HOLD_RISING ||
                         arc.timing_type == TT::HOLD_FALLING) {
                double h =
                    (st.dir == TransitionDirection::RISING
                         ? caculate_hold_rise(arc, cell_library_, data_trans_ns,
                                              clk_trans)
                         : caculate_hold_fall(arc, cell_library_, data_trans_ns,
                                              clk_trans)) *
                    1000.0;
                std::cout << " hold=" << h << "ps";
                pr.library_hold_time = h;
              }
            }
          }
        }
      }
    }
    std::cout << "\n";
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

  // 收集所有起点：input 或 clk 节点
  std::vector<std::size_t> start_node_ids;
  for (std::size_t i = 0; i < candidate_graphy_.nodes.size(); ++i) {
    const auto &node = candidate_graphy_.nodes[i];
    if (node.point_idx >= res.points.size())
      continue;
    PointType t = res.points[node.point_idx].type;
    if (t == INPUT || t == CLK)
      start_node_ids.push_back(i);
  }

  const double initial_slew_ns = 0.0; // 起点 slew，后续可改为 clk_slew 等

  for (std::size_t start_node_id : start_node_ids) {
    const std::size_t start_pt =
        candidate_graphy_.nodes[start_node_id].point_idx;

    std::function<void(std::size_t cur_node_id, TransitionDirection cur_dir,
                       double cur_slew_ns, std::vector<TimingStep> steps_so_far,
                       double delay_so_far)>
        emit_chains = [&](std::size_t cur_node_id, TransitionDirection cur_dir,
                          double cur_slew_ns,
                          std::vector<TimingStep> steps_so_far,
                          double delay_so_far) {
          const CandidateNode &cur_node = candidate_graphy_.nodes[cur_node_id];
          for (std::size_t path_id : cur_node.fanout_paths) {
            CandidatePathSegmentResult seg = compute_candidate_path_with_input(
                path_id, cur_dir, cur_slew_ns);

            std::vector<TimingStep> new_steps = steps_so_far;
            for (TimingStep &s : seg.steps) {
              s.arrival += delay_so_far;
              new_steps.push_back(s);
            }
            double new_delay = delay_so_far + seg.total_delay_ps;

            const CandidatePath &path = candidate_graphy_.paths[path_id];
            std::size_t end_node_id = path.end_node;
            std::size_t end_pt = candidate_graphy_.nodes[end_node_id].point_idx;

            if (is_terminal_node(res, candidate_graphy_, end_node_id)) {
              // 指纹：start_end + 各步 (from_pt->to_pt)，相同则视为重复 path
              std::string fp =
                  std::to_string(start_pt) + "_" + std::to_string(end_pt);
              for (const auto &st : new_steps)
                fp += "_" + std::to_string(st.start_point) + "-" +
                      std::to_string(st.end_point);
              if (!path_printed.insert(fp).second)
                continue;

              TimingPathResult pr;
              pr.startpoint = start_pt;
              pr.endpoint = end_pt;
              pr.data_arrival_time = new_delay;
              pr.steps = std::move(new_steps);
              if (start_pt < res.points.size() && end_pt < res.points.size())
                pr.group = classify_path_group(
                    effective_start_type_for_group(res.points[start_pt]),
                    res.points[end_pt].type);
              display_result_path_detail(pr, res.paths.size());
              res.paths.push_back(std::move(pr));
            } else {
              emit_chains(end_node_id, seg.output_dir, seg.output_slew_ns,
                          std::move(new_steps), new_delay);
            }
          }
        };

    for (bool is_rise : {true, false}) {
      TransitionDirection dir =
          is_rise ? TransitionDirection::RISING : TransitionDirection::FALLING;
      emit_chains(start_node_id, dir, initial_slew_ns, {}, 0.0);
    }
  }
}

} // namespace sta
