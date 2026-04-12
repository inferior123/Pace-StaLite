#include "sta/sta_data_structures.hpp"
#include "sta/sta_logger.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <limits>
#include <unordered_set>
#include <vector>

#include "sta/debug.h"

namespace sta {
namespace {

// --- GBA timing graph helpers (combinational point graph) --------------------

/// Fanout counts toward indegree / topo / loop-breaking iff it is enabled and
/// stays inside the point index range.
inline bool gba_timing_edge_active(const TimingEdge &e, std::size_t point_count) {
  return !e.loop_disabled && e.target_point < point_count;
}

/// OpenSTA-style loop breaking + Kahn topo on `points`, result in `out_topo`.
/// Mutates `TimingEdge::loop_disabled` on back edges. Kept in an inner namespace
/// so `STAWorker::gba_compute_topo_and_break_cycles` reads as a short pipeline.
namespace gba_topo {

enum class DfsVertexState : unsigned char { Fresh = 0, OnStack = 1, Done = 2 };

struct DfsFrame {
  std::size_t vertex;
  std::size_t fanout_index;
};

static void log_disabling_back_edge(std::size_t from_idx, std::size_t to_idx,
                                    const TimingPointRef &from_pt,
                                    const TimingPointRef &to_pt,
                                    const TimingEdge &edge) {
  LOG_WARN << "[GBA] WARNING: combinational loop (DFS back edge), "
              "disabling edge pt"
           << from_idx << " -> pt" << to_idx << " ("
           << (from_pt.inst ? from_pt.inst->instance_name : "(port)") << "/"
           << from_pt.port_name << " -> "
           << (to_pt.inst ? to_pt.inst->instance_name : "(port)") << "/"
           << to_pt.port_name << ", "
           << (edge.type == WIRE ? "WIRE" : "COMB_ARC") << ")\n";
}

static std::vector<std::size_t>
compute_in_degrees(const std::vector<TimingPointRef> &points,
                   std::size_t point_count) {
  std::vector<std::size_t> indeg(point_count, 0);
  for (const auto &pt : points) {
    for (const auto &e : pt.fanouts) {
      if (gba_timing_edge_active(e, point_count)) {
        indeg[e.target_point]++;
      }
    }
  }
  return indeg;
}

/// Phase-1 DFS seeds: in-degree 0, sorted, and with at least one active fanout.
static std::vector<std::size_t>
sorted_roots_with_fanout(const std::vector<TimingPointRef> &points,
                         std::size_t point_count,
                         const std::vector<std::size_t> &in_degrees) {
  std::vector<std::size_t> roots;
  roots.reserve(point_count);
  for (std::size_t i = 0; i < point_count; ++i) {
    if (in_degrees[i] != 0) {
      continue;
    }
    bool has_fanout = false;
    for (const auto &e : points[i].fanouts) {
      if (gba_timing_edge_active(e, point_count)) {
        has_fanout = true;
        break;
      }
    }
    if (has_fanout) {
      roots.push_back(i);
    }
  }
  std::sort(roots.begin(), roots.end());
  return roots;
}

static void dfs_disable_back_edges(std::vector<TimingPointRef> &points,
                                   std::size_t point_count,
                                   const std::vector<std::size_t> &start_vertices,
                                   std::vector<DfsVertexState> &state) {
  for (std::size_t root : start_vertices) {
    if (state[root] != DfsVertexState::Fresh) {
      continue;
    }
    state[root] = DfsVertexState::OnStack;
    std::vector<DfsFrame> stack;
    stack.push_back(DfsFrame{root, 0});
    while (!stack.empty()) {
      DfsFrame &fr = stack.back();
      auto &fanouts = points[fr.vertex].fanouts;
      if (fr.fanout_index >= fanouts.size()) {
        state[fr.vertex] = DfsVertexState::Done;
        stack.pop_back();
        if (!stack.empty()) {
          stack.back().fanout_index++;
        }
        continue;
      }
      TimingEdge &edge = fanouts[fr.fanout_index];
      if (!gba_timing_edge_active(edge, point_count)) {
        fr.fanout_index++;
        continue;
      }
      const std::size_t v = edge.target_point;
      if (state[v] == DfsVertexState::OnStack) {
        log_disabling_back_edge(fr.vertex, v, points[fr.vertex], points[v],
                                edge);
        edge.loop_disabled = true;
        fr.fanout_index++;
        continue;
      }
      if (state[v] == DfsVertexState::Fresh) {
        state[v] = DfsVertexState::OnStack;
        stack.push_back(DfsFrame{v, 0});
        continue;
      }
      fr.fanout_index++;
    }
  }
}

static std::vector<std::size_t>
sorted_vertices_with_state(const std::vector<DfsVertexState> &state,
                           std::size_t point_count, DfsVertexState match) {
  std::vector<std::size_t> out;
  out.reserve(point_count);
  for (std::size_t i = 0; i < point_count; ++i) {
    if (state[i] == match) {
      out.push_back(i);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

/// Kahn topo on the graph induced by active edges. If a residual cycle remains
/// (DFS back edges need not hit every feedback arc), append remaining vertices
/// so downstream propagation still visits every point.
static void kahn_topological_order(const std::vector<TimingPointRef> &points,
                                   std::size_t point_count,
                                   std::vector<std::size_t> &out_topo_order) {
  std::vector<std::size_t> indeg = compute_in_degrees(points, point_count);
  out_topo_order.clear();
  out_topo_order.reserve(point_count);
  std::vector<std::size_t> queue;
  queue.reserve(point_count);

  for (std::size_t i = 0; i < point_count; ++i) {
    if (indeg[i] == 0) {
      queue.push_back(i);
    }
  }

  for (std::size_t head = 0; head < queue.size(); ++head) {
    const std::size_t u = queue[head];
    out_topo_order.push_back(u);
    for (const auto &e : points[u].fanouts) {
      if (gba_timing_edge_active(e, point_count) &&
          --indeg[e.target_point] == 0) {
        queue.push_back(e.target_point);
      }
    }
  }

  if (out_topo_order.size() < point_count) {
    std::unordered_set<std::size_t> leftover;
    for (std::size_t i = 0; i < point_count; ++i) {
      if (indeg[i] > 0) {
        leftover.insert(i);
      }
    }
    LOG_WARN << "[GBA] WARNING: graph still cyclic after DFS loop break; "
             << leftover.size() << " node(s) not in topo (total="
             << point_count << ", reached=" << out_topo_order.size() << ")\n";
    std::cerr << "[GBA] WARNING: forcing remaining nodes into topo_order\n";
    for (std::size_t i : leftover) {
      out_topo_order.push_back(i);
    }
  }
}

} // namespace gba_topo

} // namespace

void STAWorker::gba_clear_graph_structure() {
  gba_graphy_.nodes.clear();
  gba_graphy_.paths.clear();
  gba_graphy_.pt_to_node.clear();
  gba_graphy_.end_node.clear();
  gba_graphy_.topo_order.clear();
}

void STAWorker::gba_allocate_nodes_for_points() {
  const std::size_t point_count = res.points.size();
  const double pos_inf = std::numeric_limits<double>::infinity();
  const double neg_inf = -pos_inf;
  const double init_delay =
      (analysis_mode == AnalysisMode::MAX) ? neg_inf : pos_inf;
  const double init_slew =
      (analysis_mode == AnalysisMode::MAX) ? neg_inf : pos_inf;

  gba_graphy_.nodes.reserve(point_count);
  for (std::size_t i = 0; i < point_count; ++i) {
    GbaNode node;
    node.pt_idx = i;
    node.id = i;
    node.delay_rise = init_delay;
    node.delay_fall = init_delay;
    node.slew_rise = init_slew;
    node.slew_fall = init_slew;
    node.prev_node_rise = std::numeric_limits<std::size_t>::max();
    node.prev_node_fall = std::numeric_limits<std::size_t>::max();
    node.prev_path_rise = std::numeric_limits<std::size_t>::max();
    node.prev_path_fall = std::numeric_limits<std::size_t>::max();
    node.fanouts.clear();

    gba_graphy_.pt_to_node[i] = node.id;
    gba_graphy_.nodes.push_back(std::move(node));
  }
}

std::size_t STAWorker::gba_append_path(std::size_t from_node, std::size_t to_node,
                                       double incr_ps, double slew_ns,
                                       TransitionDirection dir,
                                       TransitionDirection input_dir) {
  GbaPath path;
  path.startnode = from_node;
  path.endnode = to_node;
  path.incr = incr_ps;
  path.slew = slew_ns;
  path.dir = dir;
  path.input_dir = input_dir;
  path.path_idx = gba_graphy_.paths.size();

  gba_graphy_.nodes[from_node].fanouts.push_back(path);
  gba_graphy_.paths.push_back(path);

  return path.path_idx;
}

void STAWorker::gba_compute_topo_and_break_cycles(std::size_t point_count) {
  using gba_topo::DfsVertexState;
  using gba_topo::compute_in_degrees;
  using gba_topo::dfs_disable_back_edges;
  using gba_topo::kahn_topological_order;
  using gba_topo::sorted_roots_with_fanout;
  using gba_topo::sorted_vertices_with_state;

  // OpenSTA-style: DFS from sorted zero-indegree roots, then from any vertex
  // still unvisited; back edges set TimingEdge::loop_disabled. Kahn topo on
  // active edges; leftover SCC nodes are appended if the graph stays cyclic.

  const std::vector<std::size_t> indeg0 =
      compute_in_degrees(res.points, point_count);
  std::vector<DfsVertexState> visit(point_count, DfsVertexState::Fresh);

  dfs_disable_back_edges(res.points, point_count,
                         sorted_roots_with_fanout(res.points, point_count,
                                                  indeg0),
                         visit);

  dfs_disable_back_edges(
      res.points, point_count,
      sorted_vertices_with_state(visit, point_count, DfsVertexState::Fresh),
      visit);

  kahn_topological_order(res.points, point_count, gba_graphy_.topo_order);
}

void STAWorker::gba_seed_input_clock_nodes() {
  for (std::size_t pt_idx : input_clk_point_ids) {
    auto it = gba_graphy_.pt_to_node.find(pt_idx);
    if (it == gba_graphy_.pt_to_node.end()) {
      continue;
    }
    GbaNode &node = gba_graphy_.nodes[it->second];
    node.delay_rise = 0.0;
    node.delay_fall = 0.0;
    node.slew_rise = 0.0;
    node.slew_fall = 0.0;
    node.prev_node_rise = node.id;
    node.prev_node_fall = node.id;
  }
}

void STAWorker::gba_relax_fanout_segments(std::size_t u_pt, std::size_t v_pt,
                                         std::size_t u_node_id,
                                         std::size_t v_node_id, GbaNode &v_node,
                                         double base_delay_ps,
                                         double prev_slew_ns,
                                         TransitionDirection input_dir) {
  const bool use_max = (analysis_mode == AnalysisMode::MAX);

  auto segs = segment_delays_slews_gba(res, cell_library_, analysis_mode, u_pt,
                                       v_pt, prev_slew_ns, input_dir);

  if (segs.empty()) {
    segment_res seg;
    seg.slew = prev_slew_ns;
    seg.delay = 0.0;
    seg.dir = input_dir;
    segs.push_back(seg);
  }

  for (const auto &seg : segs) {
    const double cand_delay = base_delay_ps + seg.delay;
    const std::size_t path_idx =
        gba_append_path(u_node_id, v_node_id, seg.delay, seg.slew, seg.dir,
                        input_dir);

    if (seg.dir == TransitionDirection::RISING) {
      if ((use_max && seg.slew > v_node.slew_rise) ||
          (!use_max && seg.slew < v_node.slew_rise)) {
        v_node.delay_rise = cand_delay;
        v_node.slew_rise = seg.slew;
        v_node.prev_node_rise = u_node_id;
        v_node.prev_path_rise = path_idx;
      }
    } else if (seg.dir == TransitionDirection::FALLING) {
      if ((use_max && seg.slew > v_node.slew_fall) ||
          (!use_max && seg.slew < v_node.slew_fall)) {
        v_node.delay_fall = cand_delay;
        v_node.slew_fall = seg.slew;
        v_node.prev_node_fall = u_node_id;
        v_node.prev_path_fall = path_idx;
      }
    }
  }
}

void STAWorker::gba_forward_propagate_build_paths(std::size_t point_count) {
  const double pos_inf = std::numeric_limits<double>::infinity();
  const double neg_inf = -pos_inf;

  for (std::size_t u_pt : gba_graphy_.topo_order) {
    auto it_u = gba_graphy_.pt_to_node.find(u_pt);
    if (it_u == gba_graphy_.pt_to_node.end()) {
      continue;
    }
    const std::size_t u_node_id = it_u->second;
    GbaNode &u_node = gba_graphy_.nodes[u_node_id];
    const TimingPointRef &u_ref = res.points[u_pt];

    const bool has_rise = (analysis_mode == AnalysisMode::MAX)
                              ? (u_node.delay_rise > neg_inf)
                              : (u_node.delay_rise < pos_inf);
    const bool has_fall = (analysis_mode == AnalysisMode::MAX)
                              ? (u_node.delay_fall > neg_inf)
                              : (u_node.delay_fall < pos_inf);

    if (!has_rise && !has_fall) {
      continue;
    }

    if (u_ref.type == OUTPUT || u_ref.type == REGD) {
      gba_graphy_.end_node.push_back(u_pt);
    }

    for (const auto &edge : u_ref.fanouts) {
      if (edge.loop_disabled) {
        continue;
      }
      const std::size_t v_pt = edge.target_point;
      if (v_pt >= point_count) {
        continue;
      }
      auto it_v = gba_graphy_.pt_to_node.find(v_pt);
      if (it_v == gba_graphy_.pt_to_node.end()) {
        continue;
      }
      const std::size_t v_node_id = it_v->second;
      GbaNode &v_node = gba_graphy_.nodes[v_node_id];

      if (has_rise) {
        gba_relax_fanout_segments(u_pt, v_pt, u_node_id, v_node_id, v_node,
                                  u_node.delay_rise, u_node.slew_rise,
                                  TransitionDirection::RISING);
      }
      if (has_fall) {
        gba_relax_fanout_segments(u_pt, v_pt, u_node_id, v_node_id, v_node,
                                  u_node.delay_fall, u_node.slew_fall,
                                  TransitionDirection::FALLING);
      }
    }
  }
}

void STAWorker::build_gba_graphy() {
  gba_clear_graph_structure();

  const std::size_t point_count = res.points.size();
  LOG_INFO << "[gba] build_gba_graphy: points=" << point_count
           << " edges=" << res.edges.size() << "\n";

  if (point_count == 0) {
    LOG_WARN << "[gba] no res point detect, skip\n";
  }

  assert(cell_library_);

  gba_allocate_nodes_for_points();
  gba_compute_topo_and_break_cycles(point_count);
  gba_seed_input_clock_nodes();
  gba_forward_propagate_build_paths(point_count);
}

void STAWorker::reset_gba_nodes_state() {
  AnalysisMode mode = get_analysis_mode();
  const double pos_inf = std::numeric_limits<double>::infinity();
  const double neg_inf = -std::numeric_limits<double>::infinity();

  for (auto &node : gba_graphy_.nodes) {
    node.delay_rise = (mode == AnalysisMode::MAX) ? neg_inf : pos_inf;
    node.delay_fall = (mode == AnalysisMode::MAX) ? neg_inf : pos_inf;

    node.prev_node_rise = std::numeric_limits<std::size_t>::max();
    node.prev_node_fall = std::numeric_limits<std::size_t>::max();
    node.prev_path_rise = std::numeric_limits<std::size_t>::max();
    node.prev_path_fall = std::numeric_limits<std::size_t>::max();
  }
}

void STAWorker::run_gba_propagate(PointType pt_type) {
  // 目前仅支持从 CLK_PIN 或 INPUT 作为起点的两类传播
  if (pt_type != PointType::CLK_PIN && pt_type != PointType::INPUT) {
    assert(false && "run_gba_propagate only supports CLK_PIN or INPUT");
  }

  const std::size_t point_count = res.points.size();
  if (point_count == 0)
    return;

  assert(cell_library_);

  // 1. 直接复用 build_gba_graphy() 中已缓存的拓扑排序
  const std::vector<std::size_t> &topo = gba_graphy_.topo_order;

  // 2. 初始化起点：根据 pt_type 只选择 CLK_PIN 或 INPUT
  for (std::size_t pt_idx : input_clk_point_ids) {
    auto it = gba_graphy_.pt_to_node.find(pt_idx);
    if (it == gba_graphy_.pt_to_node.end())
      continue;

    PointType start_type = effective_start_type_for_group(res.points[pt_idx]);
    if (start_type != pt_type)
      continue;

    GbaNode &node = gba_graphy_.nodes[it->second];
    node.delay_rise = 0.0;
    node.delay_fall = 0.0;
    node.prev_node_rise = node.id;
    node.prev_node_fall = node.id;
  }

  // 3. 沿 topo 序做 DP：仅选择，不计算；使用 build_gba_graphy 中已有的 paths/fanouts
  const bool use_max = (analysis_mode == AnalysisMode::MAX);
  const double pos_inf = std::numeric_limits<double>::infinity();
  const double neg_inf = -pos_inf;

  for (std::size_t u_pt : topo) {
    auto it_u = gba_graphy_.pt_to_node.find(u_pt);
    if (it_u == gba_graphy_.pt_to_node.end())
      continue;
    std::size_t u_node_id = it_u->second;
    GbaNode &u_node = gba_graphy_.nodes[u_node_id];

    const bool has_rise = use_max ? (u_node.delay_rise > neg_inf)
                                 : (u_node.delay_rise < pos_inf);
    const bool has_fall = use_max ? (u_node.delay_fall > neg_inf)
                                 : (u_node.delay_fall < pos_inf);

    if (!has_rise && !has_fall)
      continue;

    if constexpr (kDebugGbaPropPath) {  // NOLINT
      const std::size_t filter = kDebugGbaPropPathFilterPt;
      if (filter == static_cast<std::size_t>(-1) || u_pt == filter) {
        const auto &u_ref = res.points[u_pt];
        LOG_DEBUG << "[prop] u_pt=" << u_pt
                  << " (" << (u_ref.inst ? u_ref.inst->instance_name : "(port)")
                  << "/" << u_ref.port_name << ")"
                  << " delay_rise=" << u_node.delay_rise << "ps"
                  << " delay_fall=" << u_node.delay_fall << "ps"
                  << " slew_rise_max=" << u_node.slew_rise << "ns"
                  << " slew_rise_min=" << u_node.slew_fall << "ns"
                  << " load_cap=" << u_ref.load_cap << "pF"
                  << " fanouts=" << u_node.fanouts.size() << "\n";
      }
    }

    // 遍历 u 的 fanouts（来自 build_gba_graphy），按 path 选择并更新 v 的 delay/prev
    for (const GbaPath &path : u_node.fanouts) {
      std::size_t v_node_id = path.endnode;
      if (v_node_id >= gba_graphy_.nodes.size())
        continue;
      GbaNode &v_node = gba_graphy_.nodes[v_node_id];

      double base_delay = (path.input_dir == TransitionDirection::RISING)
                              ? u_node.delay_rise
                              : u_node.delay_fall;
      if ((path.input_dir == TransitionDirection::RISING && !has_rise) ||
          (path.input_dir == TransitionDirection::FALLING && !has_fall))
        continue;

      double cand_delay = base_delay + path.incr;

      if constexpr (kDebugGbaPropPath) {  // NOLINT
        const std::size_t filter = kDebugGbaPropPathFilterPt;
        if (filter == static_cast<std::size_t>(-1) || u_pt == filter) {
          std::size_t v_pt = v_node.pt_idx;
          const auto &v_ref = res.points[v_pt];
          const char *idir = path.input_dir == TransitionDirection::RISING ? "R" : "F";
          const char *odir = path.dir == TransitionDirection::RISING ? "R" : "F";
          LOG_DEBUG << "  -> v_pt=" << v_pt
                    << " (" << (v_ref.inst ? v_ref.inst->instance_name : "(port)")
                    << "/" << v_ref.port_name << ")"
                    << " [" << idir << "->" << odir << "]"
                    << " incr=" << path.incr << "ps"
                    << " slew=" << path.slew << "ns"
                    << " v.load_cap=" << v_ref.load_cap << "pF"
                    << " v.rise_cap=" << v_ref.rise_cap << "pF"
                    << " v.fall_cap=" << v_ref.fall_cap << "pF"
                    << " base=" << base_delay << "ps"
                    << " cand=" << cand_delay << "ps"
                    << " v.delay_rise=" << v_node.delay_rise << "ps"
                    << " v.delay_fall=" << v_node.delay_fall << "ps"
                    << "\n";
        }
      }

      if (path.dir == TransitionDirection::RISING) {
        if ((use_max && cand_delay > v_node.delay_rise) ||
            (!use_max && cand_delay < v_node.delay_rise)) {
          v_node.delay_rise = cand_delay;
          v_node.prev_node_rise = u_node_id;
          v_node.prev_path_rise = path.path_idx;
        }
      } else if (path.dir == TransitionDirection::FALLING) {
        if ((use_max && cand_delay > v_node.delay_fall) ||
            (!use_max && cand_delay < v_node.delay_fall)) {
          v_node.delay_fall = cand_delay;
          v_node.prev_node_fall = u_node_id;
          v_node.prev_path_fall = path.path_idx;
        }
      }
    }
  }
}

void STAWorker::compute_setup_hold_gba(TimingPathResult &pr) {
  if (pr.steps.empty())
    return;

  // 起点 slew 单位为 ps（step.slew 存储单位），转换为 ns 供 recalc 函数使用
  double cur_slew_ns = pr.steps.front().slew / 1000.0;

  // 逐级用 max/min cap 重算输出 slew，链式传递，不修改 pr.steps 中的值
  for (const auto &s : pr.steps) {
    cur_slew_ns = recalc_slew_with_max_cap(
        res, cell_library_, analysis_mode,
        s.start_point, s.end_point,
        cur_slew_ns, s.dir);
  }

  // 用重算后的末步 slew 构造临时路径，仅借用 compute_path_setup_hold 计算约束
  TimingPathResult tmp = pr;
  tmp.steps.back().slew = cur_slew_ns * 1000.0; // ns -> ps
  compute_path_setup_hold(tmp);

  pr.library_setup_time = tmp.library_setup_time;
  pr.library_hold_time  = tmp.library_hold_time;
}

// 从 end_node 回溯构造 TimingPathResult，rise 和 fall 各回溯一条，并计算 setup/hold
void STAWorker::run_gba_timing_analysis(bool clear_paths_first) {
  if (clear_paths_first)
    res.paths.clear();

  constexpr double inf = std::numeric_limits<double>::infinity();

  for (std::size_t end_pt : gba_graphy_.end_node) {
    auto it = gba_graphy_.pt_to_node.find(end_pt);
    if (it == gba_graphy_.pt_to_node.end())
      continue;
    std::size_t node_id = it->second;
    GbaNode &node = gba_graphy_.nodes[node_id];

    // rise 和 fall 各回溯一条
    // MAX: 未到达时 delay == -inf
    // MIN: 未到达时 delay == +inf
    const bool use_max = (analysis_mode == AnalysisMode::MAX);
    for (bool use_rise : {true, false}) {
      const double d = use_rise ? node.delay_rise : node.delay_fall;
      if (use_max ? (d <= -inf) : (d >= inf))
        continue;

      std::size_t cur_node_id = node_id;
      bool cur_dir_rise = use_rise;

      std::vector<TimingStep> steps_rev;

      while (true) {
        GbaNode &cur_node = gba_graphy_.nodes[cur_node_id];
        std::size_t prev_id =
            cur_dir_rise ? cur_node.prev_node_rise : cur_node.prev_node_fall;

        // 到达起点（prev 指向自身）
        if (prev_id == cur_node_id)
          break;

        GbaNode &prev_node = gba_graphy_.nodes[prev_id];
        std::size_t path_idx =
            cur_dir_rise ? cur_node.prev_path_rise : cur_node.prev_path_fall;
        if (path_idx >= gba_graphy_.paths.size())
          break;

        const GbaPath &path = gba_graphy_.paths[path_idx];

        TimingStep step;
        step.start_point = prev_node.pt_idx;
        step.end_point = cur_node.pt_idx;
        step.cap_load = (path.dir == TransitionDirection::RISING) ? \
                  res.points[cur_node.pt_idx].rise_cap : \
                  res.points[cur_node.pt_idx].fall_cap;
        step.incr = path.incr;
        step.slew = path.slew * 1000.0;  // ns -> ps
        step.dir = path.dir;
        steps_rev.push_back(step);

        cur_dir_rise = (path.input_dir == TransitionDirection::RISING);
        cur_node_id = prev_id;
      }

      if (steps_rev.empty())
        continue;

      // 反转为 start -> end 顺序，并计算 arrival
      std::vector<TimingStep> steps(steps_rev.rbegin(), steps_rev.rend());
      double arrival = 0.0;
      for (auto &s : steps) {
        arrival += s.incr;
        s.arrival = arrival;
      }

      std::size_t start_pt = steps.front().start_point;
      PointType start_type = effective_start_type_for_group(res.points[start_pt]);
      PointType end_type = res.points[end_pt].type;

      TimingPathResult pr;
      pr.group = classify_path_group(start_type, end_type);
      pr.index = res.paths.size();
      pr.startpoint = start_pt;
      pr.endpoint = end_pt;
      pr.need_to_recalculate = false;
      pr.data_arrival_time = arrival;
      pr.steps = std::move(steps);

      compute_path_setup_hold(pr);
      res.paths.push_back(std::move(pr));
    }
  }
}

void run_gba_analysis(STAWorker &worker) {
  LOG_INFO << "Step 1: build_fanouts()";
  worker.build_fanouts();
  LOG_INFO << "Step 2: calculate_load_capacitance()";
  worker.calculate_load_cap();
  LOG_INFO << "Step 3: build_gba_graphy()";
  worker.build_gba_graphy();

  worker.reset_gba_nodes_state();
  worker.run_gba_propagate(PointType::CLK_PIN);
  worker.run_gba_timing_analysis(true);   // clear res.paths first

  worker.reset_gba_nodes_state();
  worker.run_gba_propagate(PointType::INPUT);
  worker.run_gba_timing_analysis(false);  // append to res.paths
}

} // namespace sta