#include "cell/cell_data_structure.hpp"
#include "sta/sta_data_structures.hpp"
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace sta {

// 判断 from_pt -> to_pt 是否存在 NON_UNATE 弧
static bool is_edge_non_unate(const TimingRunResult &res,
                               const celllib::CellLibrary *lib,
                               std::size_t from_pt, std::size_t to_pt) {
  if (from_pt >= res.points.size() || to_pt >= res.points.size() || !lib)
    return false;
  const auto &from_ref = res.points[from_pt];
  const auto &to_ref = res.points[to_pt];
  if (!from_ref.inst || !to_ref.inst || from_ref.inst != to_ref.inst)
    return false;
  const auto *cell = lib->get_cell(to_ref.inst->module_name);
  if (!cell)
    return false;
  const auto *out_pin = cell->get_pin(to_ref.port_name);
  if (!out_pin)
    return false;
  for (const auto &arc : out_pin->timing_arcs) {
    if (arc.related_pin == from_ref.port_name &&
        arc.timing_sense == celllib::TimingSense::NON_UNATE)
      return true;
  }
  return false;
}

// 根据已知的输出方向，反推 segment_delay_slew 所需的输入方向：
// POSITIVE_UNATE: input == output；NEGATIVE_UNATE: input == opposite(output)
static TransitionDirection derive_input_dir(const TimingRunResult &res,
                                             const celllib::CellLibrary *lib,
                                             std::size_t from_pt,
                                             std::size_t to_pt,
                                             TransitionDirection output_dir) {
  if (from_pt >= res.points.size() || to_pt >= res.points.size() || !lib)
    return output_dir;
  const auto &from_ref = res.points[from_pt];
  const auto &to_ref = res.points[to_pt];
  if (!from_ref.inst || !to_ref.inst || from_ref.inst != to_ref.inst)
    return output_dir;
  const auto *cell = lib->get_cell(to_ref.inst->module_name);
  if (!cell)
    return output_dir;
  const auto *out_pin = cell->get_pin(to_ref.port_name);
  if (!out_pin)
    return output_dir;
  for (const auto &arc : out_pin->timing_arcs) {
    if (arc.related_pin != from_ref.port_name)
      continue;
    if (arc.timing_sense == celllib::TimingSense::POSITIVE_UNATE)
      return output_dir;
    if (arc.timing_sense == celllib::TimingSense::NEGATIVE_UNATE)
      return (output_dir == TransitionDirection::RISING)
                 ? TransitionDirection::FALLING
                 : TransitionDirection::RISING;
  }
  return output_dir;
}

void STAWorker::recalculate_in2out() {
  for (auto &path : res.paths) {
    if (path.group != PathGroup::IN2OUT)
      continue;

    double cur_slew_ns = 0.0;
    double cumulative_arrival = 0.0;

    // 从第一步的输出方向反推初始输入方向
    TransitionDirection cur_dir = TransitionDirection::UNKNOWN;
    if (!path.steps.empty()) {
      const auto &first = path.steps.front();
      cur_dir = derive_input_dir(res, cell_library_, first.start_point,
                                 first.end_point, first.dir);
    }

    for (auto &step : path.steps) {
      if (is_edge_non_unate(res, cell_library_, step.start_point,
                            step.end_point)) {
        // 非单调弧：输出方向已知，use_max=true 取最大延迟
        CandidatePathSegmentResult seg = compute_one_edge_non_unate_segment(
            step.start_point, step.end_point, step.dir, cur_slew_ns,
            /*use_max=*/true);
        if (seg.steps.empty())
          continue;
        step.incr = seg.total_delay_ps;
        step.slew = seg.output_slew_ns * 1000.0;
        step.cap_load = seg.steps[0].cap_load;
        step.dir = seg.output_dir;
        cur_slew_ns = seg.output_slew_ns;
        cur_dir = seg.output_dir;
      } else {
        // 单调弧：传入输入方向，由 segment_delay_slew 推算输出方向
        double out_delay = 0.0, out_slew_ns = cur_slew_ns;
        TransitionDirection out_dir = cur_dir;
        segment_delay_slew(res, cell_library_, analysis_mode, step.start_point,
                           step.end_point, cur_slew_ns, cur_dir, out_delay,
                           out_slew_ns, out_dir);
        step.incr = out_delay;
        step.slew = out_slew_ns * 1000.0;
        step.dir = out_dir;
        if (step.end_point < res.points.size()) {
          const auto &to_ref = res.points[step.end_point];
          step.cap_load = (out_dir == TransitionDirection::RISING)  ? to_ref.rise_cap
                        : (out_dir == TransitionDirection::FALLING) ? to_ref.fall_cap
                        : to_ref.load_cap;
        }
        cur_slew_ns = out_slew_ns;
        cur_dir = out_dir;
      }

      cumulative_arrival += step.incr;
      step.arrival = cumulative_arrival;
    }

    path.data_arrival_time = cumulative_arrival;
  }
}

// 对一步计算所有可能的 (slew, delay) 对；unate 返回 1 对，non-unate 返回 2 对 (rise, fall)
static std::pair<std::vector<double>, std::vector<double>>
get_slews_and_delays(const TimingRunResult &res,
                     const celllib::CellLibrary *lib,
                     const STAWorker &worker,
                     const TimingStep &step, double prev_slew_ns,
                     TransitionDirection prev_dir, AnalysisMode mode) {
  std::vector<double> slews;
  std::vector<double> delays;

  if (step.start_point >= res.points.size() || step.end_point >= res.points.size() || !lib) {
    assert(false);
  }

  const auto &get_seg = segment_delays_slews(res, lib, mode, step.start_point, step.end_point,
                      prev_slew_ns, prev_dir);

  TransitionDirection step_dir = step.dir;
  for (const auto &seg : get_seg) {
    if (step_dir == TransitionDirection::UNKNOWN || step_dir == seg.dir) {
      slews.push_back(seg.slew);
      delays.push_back(seg.delay);
    }
  }
  return {slews, delays};
}

TimingPathResult recalculate_one_path(const STAWorker &worker, const TimingPathResult &path,
                                    AnalysisMode mode, size_t path_idx_for_dbg) {
  struct recalculate_frame {
    std::vector<double> slews;
    double delay;
    double incr;
    size_t cur_;
  };

  const bool dbg = false;
  const size_t dbg_interval = 1;

  const auto &res = worker.get_sta_res();
  const auto *lib = worker.get_cell_library();
  if (!lib || path.steps.empty()) {
    return path;
  }

  const double init_slew = 0.0;
  TransitionDirection init_dir = path.steps[0].dir;
  if (init_dir == TransitionDirection::UNKNOWN) {
    assert(false);
  }

  TimingPathResult best = path;
  double best_delay = (mode == AnalysisMode::MAX ? -std::numeric_limits<double>::infinity()
                                              : std::numeric_limits<double>::infinity());

  std::vector<recalculate_frame> stack;
  stack.push_back({{init_slew}, 0.0, 0});
  size_t cur_step_idx = 0;
  size_t iter_count = 0;

  if (dbg) {
    std::cerr << "[recalc] path " << path_idx_for_dbg << " steps=" << path.steps.size()
              << " start\n";
  }

  while (true) {
    iter_count++;
    if (dbg && dbg_interval > 0 && iter_count % dbg_interval == 0) {
      std::cerr << "[recalc] path " << path_idx_for_dbg << " iter=" << iter_count
                << " cur_step=" << cur_step_idx << "/" << path.steps.size()
                << " stack_sz=" << stack.size() << " "
                << (cur_step_idx >= path.steps.size() ? "BT" : "FWD") << "\n";
    }
    // 先检查是否已遍历完所有 step（上一轮处理完最后一步并 cur_step_idx++ 后进入）
    if (cur_step_idx >= path.steps.size()) {
      double cur_total_delay = stack.back().delay;  // 刚 push 的帧才是完整路径结果
      bool update = (mode == AnalysisMode::MAX) ? (cur_total_delay > best_delay)
                                                : (cur_total_delay < best_delay);
      if (update) {
        best_delay = cur_total_delay;
        best.data_arrival_time = cur_total_delay;
        for (size_t i = 1; i <= best.steps.size(); i++) {
          const auto &f = stack[i];
          best.steps[i - 1].incr = f.incr;
          best.steps[i - 1].slew = f.slews[f.cur_] * 1000.0;  // ns -> ps
          best.steps[i - 1].arrival = f.delay;
        }
      }
      stack.pop_back();
      if (stack.empty())
        break;
      cur_step_idx--;
      recalculate_frame &prev_top = stack.back();
      if (prev_top.cur_ + 1 < prev_top.slews.size()) {
        prev_top.cur_++;
      } else {
        while (!stack.empty() && stack.back().cur_ + 1 >= stack.back().slews.size()) {
          stack.pop_back();
          if (cur_step_idx > 0)
            cur_step_idx--;
        }
        if (stack.empty())
          break;
        stack.back().cur_++;
      }
      continue;
    }

    recalculate_frame &top = stack.back();
    const TimingStep &step = path.steps[cur_step_idx];
    double prev_slew = top.slews[top.cur_];
    double prev_delay = top.delay;
    TransitionDirection prev_dir = (cur_step_idx == 0)
                                      ? init_dir
                                      : path.steps[cur_step_idx - 1].dir;

    auto [new_slews, new_delays] =
        get_slews_and_delays(res, lib, worker, step, prev_slew, prev_dir, mode);

    assert(new_delays.size() == new_slews.size());
    if (new_delays.empty()) {
      // 处理 wire：delay=0，slew 透传
      new_delays.push_back(0.0);
      new_slews.push_back(prev_slew);
    }

    // 在 new_delays 里选一个最大的，加到 prev_delay 上，把新计算出的 total_delay push 进去
    size_t choose_idx = 0;
    if(mode == AnalysisMode::MAX) {
      for (size_t i = 1; i < new_delays.size(); i++) {
        if (new_delays[i] > new_delays[choose_idx])
          choose_idx = i;
      }
    } else {
      for (size_t i = 1; i < new_delays.size(); i++) {
        if (new_delays[i] < new_delays[choose_idx])
          choose_idx = i;
      }
    }
    double total_delay = prev_delay + new_delays[choose_idx];
    stack.push_back({new_slews, total_delay, new_delays[choose_idx], 0});

    cur_step_idx++;
  }

  if (dbg) {
    std::cerr << "[recalc] path " << path_idx_for_dbg << " done iters=" << iter_count
              << "\n";
  }
  // 循环结束以后根据 mode 判断应该取哪个值
  return best;
}

void general_recalculate(STAWorker &worker,
                        std::vector<TimingPathResult> &paths) {
  const size_t total = paths.size();
  if (total == 0)
    return;
  for (size_t i = 0; i < total; i++) {
    paths[i] =
        recalculate_one_path(worker, paths[i], worker.get_analysis_mode(), i);
    if ((i + 1) % 25 == 0 || i + 1 == total) {
      int pct = static_cast<int>(100 * (i + 1) / total);
      std::cerr << "\r  [recalculate] " << (i + 1) << "/" << total << " (" << pct
                << "%)" << std::flush;
    }
  }
  std::cerr << "\r  [recalculate] " << total << "/" << total << " (100%)\n";
}

void STAWorker::candidate_recalculate() {
    // recalculate_in2out();
    general_recalculate(*this, res.paths);
}

}