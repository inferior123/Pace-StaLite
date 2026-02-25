#include "sta/sta_data_structures.hpp"
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <vector>

#include "sta/debug.h"

namespace sta {

void STAWorker::build_gba_graphy() {
  // 清空旧图
  gba_graphy_.nodes.clear();
  gba_graphy_.paths.clear();
  gba_graphy_.pt_to_node.clear();
  gba_graphy_.end_node.clear();

  const std::size_t point_count = res.points.size();
  std::cerr << "[gba] build_gba_graphy: points=" << point_count
            << " edges=" << res.edges.size() << "\n";

  if (point_count == 0) {
    std::cerr << "[gba] no res point detect, skip\n";
  }

  // 为每个 TimingPointRef 创建一个 GbaNode，并建立 pt_idx -> node_id 映射
  // MAX: 未到达用 -inf，更新时取更大；MIN: 未到达用 +inf，更新时取更小
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

  auto add_path = [this](std::size_t from_node, std::size_t to_node,
                        double incr_ps, double slew_ns,
                        TransitionDirection dir,
                        TransitionDirection input_dir) -> std::size_t {
    GbaPath path;
    path.startnode = from_node;
    path.endnode = to_node;
    path.incr = incr_ps;   // ps
    path.slew = slew_ns;   // ns
    path.dir = dir;
    path.input_dir = input_dir;
    path.path_idx = gba_graphy_.paths.size();

    gba_graphy_.nodes[from_node].fanouts.push_back(path);
    gba_graphy_.paths.push_back(path);

    return path.path_idx;
  };

  assert(cell_library_);

  // 1. 计算 point 图的入度，用于拓扑排序
  std::vector<std::size_t> indeg(point_count, 0);
  for (const auto &pt : res.points) {
    for (const auto &e : pt.fanouts) {
      if (e.target_point < point_count) {
        indeg[e.target_point]++;
      }
    }
  }

  // 2. Kahn 拓扑排序，得到 point 层面的 topo 序列
  std::vector<std::size_t> topo;
  topo.reserve(point_count);
  std::vector<std::size_t> queue;
  queue.reserve(point_count);

  for (std::size_t i = 0; i < point_count; ++i) {
    if (indeg[i] == 0) {
      queue.push_back(i);
    }
  }

  for (std::size_t head = 0; head < queue.size(); ++head) {
    std::size_t u_pt = queue[head];
    topo.push_back(u_pt);
    const auto &pt = res.points[u_pt];
    for (const auto &e : pt.fanouts) {
      if (e.target_point < point_count && --indeg[e.target_point] == 0) {
        queue.push_back(e.target_point);
      }
    }
  }

  // 3. 初始化起点（input + clock）：从这些点开始做 GBA 传播，slew/delay 先全部计算完
  for (std::size_t pt_idx : input_clk_point_ids) {
    auto it = gba_graphy_.pt_to_node.find(pt_idx);
    if (it == gba_graphy_.pt_to_node.end())
      continue;
    GbaNode &node = gba_graphy_.nodes[it->second];
    node.delay_rise = 0.0;
    node.delay_fall = 0.0;
    node.slew_rise = 0.0;
    node.slew_fall = 0.0;
    node.prev_node_rise = node.id;
    node.prev_node_fall = node.id;
  }

  // 4. 沿 topo 序做 DP：对每条 edge 计算 slew/delay，add_path，更新 v_node
  const bool use_max = (analysis_mode == AnalysisMode::MAX);

  for (std::size_t u_pt : topo) {
    auto it_u = gba_graphy_.pt_to_node.find(u_pt);
    if (it_u == gba_graphy_.pt_to_node.end())
      continue;
    std::size_t u_node_id = it_u->second;
    GbaNode &u_node = gba_graphy_.nodes[u_node_id];

    const TimingPointRef &u_ref = res.points[u_pt];

    // MAX: 已到达 = delay > -inf；MIN: 已到达 = delay < +inf
    const bool has_rise = (analysis_mode == AnalysisMode::MAX)
                              ? (u_node.delay_rise > neg_inf)
                              : (u_node.delay_rise < pos_inf);
    const bool has_fall = (analysis_mode == AnalysisMode::MAX)
                              ? (u_node.delay_fall > neg_inf)
                              : (u_node.delay_fall < pos_inf);

    if (!has_rise && !has_fall)
      continue;

    if (u_ref.type == OUTPUT || u_ref.type == REGD) {
      gba_graphy_.end_node.push_back(u_pt);
    }

    for (const auto &edge : u_ref.fanouts) {
      std::size_t v_pt = edge.target_point;
      if (v_pt >= point_count)
        continue;

      auto it_v = gba_graphy_.pt_to_node.find(v_pt);
      if (it_v == gba_graphy_.pt_to_node.end())
        continue;
      std::size_t v_node_id = it_v->second;
      GbaNode &v_node = gba_graphy_.nodes[v_node_id];

      // 4.1 以 RISING 作为当前输入方向
      if (has_rise) {
        double prev_slew_ns = u_node.slew_rise;
        double base_delay = u_node.delay_rise;

        auto segs = segment_delays_slews_gba(
            res, cell_library_, analysis_mode, u_pt, v_pt, prev_slew_ns,
            TransitionDirection::RISING);

        if (segs.empty()) {
          segment_res seg;
          seg.slew = prev_slew_ns;
          seg.delay = 0.0;
          seg.dir = TransitionDirection::RISING;
          segs.push_back(seg);
        }

        for (const auto &seg : segs) {
          double cand_delay = base_delay + seg.delay;
          std::size_t path_idx =
              add_path(u_node_id, v_node_id, seg.delay, seg.slew, seg.dir,
                       TransitionDirection::RISING);
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

      // 4.2 以 FALLING 作为当前输入方向
      if (has_fall) {
        double prev_slew_ns = u_node.slew_fall;
        double base_delay = u_node.delay_fall;

        auto segs = segment_delays_slews_gba(
            res, cell_library_, analysis_mode, u_pt, v_pt, prev_slew_ns,
            TransitionDirection::FALLING);

        if (segs.empty()) {
          segment_res seg;
          seg.slew = prev_slew_ns;
          seg.delay = 0.0;
          seg.dir = TransitionDirection::FALLING;
          segs.push_back(seg);
        }

        for (const auto &seg : segs) {
          double cand_delay = base_delay + seg.delay;
          std::size_t path_idx =
              add_path(u_node_id, v_node_id, seg.delay, seg.slew, seg.dir,
                       TransitionDirection::FALLING);
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
    }
  }
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

  // 1. 重新计算 point 图的拓扑顺序（与 build_gba_graphy 中一致）
  std::vector<std::size_t> indeg(point_count, 0);
  for (const auto &pt : res.points) {
    for (const auto &e : pt.fanouts) {
      if (e.target_point < point_count) {
        indeg[e.target_point]++;
      }
    }
  }

  std::vector<std::size_t> topo;
  topo.reserve(point_count);
  std::vector<std::size_t> queue;
  queue.reserve(point_count);

  for (std::size_t i = 0; i < point_count; ++i) {
    if (indeg[i] == 0) {
      queue.push_back(i);
    }
  }

  for (std::size_t head = 0; head < queue.size(); ++head) {
    std::size_t u_pt = queue[head];
    topo.push_back(u_pt);
    const auto &pt = res.points[u_pt];
    for (const auto &e : pt.fanouts) {
      if (e.target_point < point_count && --indeg[e.target_point] == 0) {
        queue.push_back(e.target_point);
      }
    }
  }

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
      pr.need_to_recaculate = false;
      pr.data_arrival_time = arrival;
      pr.steps = std::move(steps);

      compute_path_setup_hold(pr);
      res.paths.push_back(std::move(pr));
    }
  }
}
}

void run_gba_analysis(sta::STAWorker &worker) {
  std::cout << "  [GBA] Step 1: build_fanouts()...\n";
  worker.build_fanouts();
  std::cout << "  [GBA-MAX] Step 2: calculate_load_capacitance()...\n";
  worker.caculate_load_cap();
  std::cout << "  [GBA-MAX] Step 3: build_gba_graphy()...\n";
  worker.build_gba_graphy();

  worker.reset_gba_nodes_state();
  worker.run_gba_propagate(sta::PointType::CLK_PIN);
  worker.run_gba_timing_analysis(true);   // clear res.paths first

  worker.reset_gba_nodes_state();
  worker.run_gba_propagate(sta::PointType::INPUT);
  worker.run_gba_timing_analysis(false);  // append to res.paths
}