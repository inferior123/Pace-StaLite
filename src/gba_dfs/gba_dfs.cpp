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
  gba_graphy_.nodes.reserve(point_count);
  for (std::size_t i = 0; i < point_count; ++i) {
    GbaNode node;
    node.pt_idx = i;
    node.id = i;
    node.delay_rise = -std::numeric_limits<double>::infinity();
    node.delay_fall = -std::numeric_limits<double>::infinity();
    node.slew_rise = 0.0;
    node.slew_fall = 0.0;
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
                        TransitionDirection input_dir) -> size_t {
    GbaPath path;
    path.startnode  = from_node;
    path.endnode    = to_node;
    path.incr       = incr_ps;   // ps
    path.slew       = slew_ns;   // ns
    path.dir        = dir;
    path.input_dir  = input_dir;

    gba_graphy_.nodes[from_node].fanouts.push_back(path);
    size_t path_idx = gba_graphy_.paths.size();
    gba_graphy_.paths.push_back(path);

    return path_idx;
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

  // 3. 初始化起点（input + clock）：从这些点开始做 GBA 传播
  // 这里直接复用 input_clk_point_ids 作为起点集合
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

  // 4. 沿 topo 序做 DP：对每条 edge，分别用 RISE/FALL 两个方向调用 segment_delays_slews
  const bool use_max = (analysis_mode == AnalysisMode::MAX);

  for (std::size_t u_pt : topo) {
    auto it_u = gba_graphy_.pt_to_node.find(u_pt);
    if (it_u == gba_graphy_.pt_to_node.end())
      continue;
    std::size_t u_node_id = it_u->second;
    GbaNode &u_node = gba_graphy_.nodes[u_node_id];

    const TimingPointRef &u_ref = res.points[u_pt];

    // 当前节点在 rise / fall 方向上是否已经有可达路径
    const bool has_rise =
        (u_node.delay_rise > -std::numeric_limits<double>::infinity());
    const bool has_fall =
        (u_node.delay_fall > -std::numeric_limits<double>::infinity());

    if (!has_rise && !has_fall)
      continue;

    if(u_ref.type == OUTPUT || u_ref.type == REGD) {
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

        auto segs = segment_delays_slews_gba(res, cell_library_, analysis_mode,
                                         u_pt, v_pt, prev_slew_ns,
                                         TransitionDirection::RISING);

        if (segs.empty()) {
          // 视为 WIRE：delay=0，slew 透传，方向保持不变
          segment_res seg;
          seg.slew = prev_slew_ns;
          seg.delay = 0.0;
          seg.dir = TransitionDirection::RISING;
          segs.push_back(seg);
        }
        
        for (const auto &seg : segs) {
          double cand_delay = base_delay + seg.delay;
          size_t path_idx = add_path(u_node_id, v_node_id, seg.delay, seg.slew,
                                     seg.dir, TransitionDirection::RISING);
          if (seg.dir == TransitionDirection::RISING) {
            if ((use_max && cand_delay > v_node.delay_rise) ||
                (!use_max && cand_delay < v_node.delay_rise)) {
              v_node.delay_rise = cand_delay;
              v_node.slew_rise = seg.slew;
              v_node.prev_node_rise = u_node_id;
              v_node.prev_path_rise = path_idx;
            }
          } else if (seg.dir == TransitionDirection::FALLING) {
            if ((use_max && cand_delay > v_node.delay_fall) ||
                (!use_max && cand_delay < v_node.delay_fall)) {
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

        auto segs = segment_delays_slews_gba(res, cell_library_, analysis_mode,
                                         u_pt, v_pt, prev_slew_ns,
                                         TransitionDirection::FALLING);

        if (segs.empty()) {
          // 视为 WIRE：delay=0，slew 透传，方向保持不变
          segment_res seg;
          seg.slew = prev_slew_ns;
          seg.delay = 0.0;
          seg.dir = TransitionDirection::FALLING;
          segs.push_back(seg);
        }

        for (const auto &seg : segs) {
          double cand_delay = base_delay + seg.delay;
          size_t path_idx = add_path(u_node_id, v_node_id, seg.delay, seg.slew,
                                     seg.dir, TransitionDirection::FALLING);
          if (seg.dir == TransitionDirection::RISING) {
            if ((use_max && cand_delay > v_node.delay_rise) ||
                (!use_max && cand_delay < v_node.delay_rise)) {
              v_node.delay_rise = cand_delay;
              v_node.slew_rise = seg.slew;
              v_node.prev_node_rise = u_node_id;
              v_node.prev_path_rise = path_idx;
            }
          } else if (seg.dir == TransitionDirection::FALLING) {
            if ((use_max && cand_delay > v_node.delay_fall) ||
                (!use_max && cand_delay < v_node.delay_fall)) {
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

  // 调试：打印每个点的类型、fanout 以及被选中的 end_node
  std::cerr << "[gba-debug] ===== TimingPointRef list =====\n";
  for (std::size_t i = 0; i < res.points.size(); ++i) {
    const auto &pt = res.points[i];
    std::cerr << "[gba-debug] pt " << i
              << " type=" << point_type_str(pt.type) << " fanouts=[";
    for (std::size_t j = 0; j < pt.fanouts.size(); ++j) {
      const auto &e = pt.fanouts[j];
      std::cerr << e.target_point;
      if (j + 1 < pt.fanouts.size()) {
        std::cerr << ", ";
      }
    }
    std::cerr << "]\n";
  }

  std::cerr << "[gba-debug] ===== selected end_node point ids =====\n";
  std::cerr << "[gba-debug] end_node ids: ";
  for (std::size_t k = 0; k < gba_graphy_.end_node.size(); ++k) {
    std::cerr << gba_graphy_.end_node[k];
    if (k + 1 < gba_graphy_.end_node.size()) {
      std::cerr << ", ";
    }
  }
  std::cerr << "\n";
}

// void STAWorker::run_gba_propagate(PointType pt_type) {
//   if(pt_type != PointType::CLK_PIN || pt_type != PointType::INPUT) {
//     assert(false);
//   }


// }

// 从 end_node 回溯构造 TimingPathResult，rise 和 fall 各回溯一条，并计算 setup/hold
void STAWorker::run_gba_timing_analysis() {
  res.paths.clear();

  constexpr double inf = std::numeric_limits<double>::infinity();

  for (std::size_t end_pt : gba_graphy_.end_node) {
    auto it = gba_graphy_.pt_to_node.find(end_pt);
    if (it == gba_graphy_.pt_to_node.end())
      continue;
    std::size_t node_id = it->second;
    GbaNode &node = gba_graphy_.nodes[node_id];

    // rise 和 fall 各回溯一条
    for (bool use_rise : {true, false}) {
      if (use_rise && node.delay_rise <= -inf)
        continue;
      if (!use_rise && node.delay_fall <= -inf)
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
  std::cout << "  [GBA-MAX] Step 3: build_candidate_graphy()...\n";
  worker.build_gba_graphy();
  worker.run_gba_timing_analysis();
}