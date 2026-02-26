#include "sta/sta_data_structures.hpp"
#include <cassert>
#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <unordered_set>

#include "sta/debug.h"

using namespace verilog;

namespace sta {
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
    double input_slew_ns, bool use_max = false) const {
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

  if constexpr (kDebugNonUnateSegment) {
    if (kDebugNonUnateFilterPt == static_cast<std::size_t>(-1) ||
        from_pt == kDebugNonUnateFilterPt) {
      debug_non_unate_entry(from_pt, to_pt, from_ref, to_ref, output_dir,
                            input_slew_ns, analysis_mode);
    }
  }

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

    if (!arc.sdf_cond.has_value() || arc.sdf_cond->empty()) {
      has_unate = true;
      unate_delay = delay_tmp;
      unate_transition = slew_tmp;
    }

    if constexpr (kDebugNonUnateSegment) {
      if (kDebugNonUnateFilterPt == static_cast<std::size_t>(-1) ||
          from_pt == kDebugNonUnateFilterPt) {
        debug_non_unate_arc(arc, is_combinational, is_c2q, load_cap, delay_tmp,
                            slew_tmp);
      }
    }

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

  if (!has_candidate) {
    show_lib_details(to_ref.inst->module_name.c_str(), *cell_library_);
    assert(false);
  }

  if (has_unate && !use_max) {
    best_delay = unate_delay;
    best_slew = unate_transition;
  }

  if constexpr (kDebugNonUnateSegment) {
    if (kDebugNonUnateFilterPt == static_cast<std::size_t>(-1) ||
        from_pt == kDebugNonUnateFilterPt) {
      debug_non_unate_summary(from_pt, to_pt, has_unate, best_delay, best_slew,
                              unate_delay, unate_transition);
    }
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
  }
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

  // 收集所有起点:通过 point_to_node 得到 node_id
  std::vector<std::size_t> start_node_ids;
  for (std::size_t pt_id : input_clk_point_ids) {
    auto it = candidate_graphy_.point_to_node.find(pt_id);
    if (it != candidate_graphy_.point_to_node.end())
      start_node_ids.push_back(it->second);
  }

  if constexpr (kDebugCandidateDfs)
    debug_dfs_start_nodes(start_node_ids, candidate_graphy_);

  const double initial_slew_ns = 0.0; // 起点 slew，后续可改为 clk_slew 等

  for (std::size_t start_node_id : start_node_ids) {
    const std::size_t start_pt =
        candidate_graphy_.nodes[start_node_id].point_idx;

    if constexpr (kDebugCandidateDfs)
      debug_dfs_start_node(start_node_id, start_pt);

    using EmitChainsFn =
        std::function<void(std::size_t, TransitionDirection, double,
                           std::vector<TimingStep>, double)>;
    EmitChainsFn emit_chains;

    auto push_or_continue =
        [&](std::size_t end_node_id, const CandidatePathSegmentResult &seg,
            std::vector<TimingStep> new_steps, double new_delay) {
          std::size_t end_pt = candidate_graphy_.nodes[end_node_id].point_idx;
          bool terminal = is_terminal_node(res, candidate_graphy_, end_node_id);

          if constexpr (kDebugCandidateDfs)
            debug_dfs_push_or_continue(end_node_id, end_pt, new_delay, terminal,
                                       seg.output_dir);

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
              if constexpr (kDebugCandidateDfs)
                debug_dfs_dup_skip();
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
            if constexpr (kDebugCandidateDfs)
              debug_dfs_push_path(res.paths.size(), pr.startpoint, pr.endpoint,
                                  pr.data_arrival_time);
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
      if constexpr (kDebugCandidateDfs)
        debug_dfs_emit_chains(cur_node_id, cur_pt, cur_dir, delay_so_far,
                              cur_node.fanout_paths.size(),
                              cur_node.relate_candidate_point.size());

      // fanout_paths 里全是 unate 段，不会包含 non-unate 边
      for (std::size_t path_id : cur_node.fanout_paths) {
        const CandidatePath &path = candidate_graphy_.paths[path_id];
        std::size_t end_node_id = path.end_node;
        if constexpr (kDebugCandidateDfs)
          debug_dfs_unate_path(path_id, end_node_id,
                               candidate_graphy_.nodes[end_node_id].point_idx);
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
        if constexpr (kDebugCandidateDfs)
          debug_dfs_relate(to_node_id, to_pt);
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
      if constexpr (kDebugCandidateDfs)
        debug_dfs_branch(is_rise, start_node_id);
      emit_chains(start_node_id, dir, initial_slew_ns, {}, 0.0);
    }
  }
  if constexpr (kDebugCandidateDfs)
    debug_dfs_total_paths(res.paths.size());
}

} // namespace sta

void run_pba_analysis(sta::STAWorker &worker) {
  std::cout << "  [PBA] Step 1: build_fanouts()...\n";
  worker.build_fanouts();

  std::cout << "  [PBA-MAX] Step 2: calculate_load_capacitance()...\n";
  worker.caculate_load_cap();
  std::cout << "  [PBA-MAX] Step 3: build_candidate_graphy()...\n";
  worker.build_candidate_graphy_dfs();
  worker.run_candidate_graphy_dfs();

  worker.candidate_recaculate();
}
