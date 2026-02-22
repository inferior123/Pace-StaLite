#include "sta/sta_data_structures.hpp"
#include <cassert>

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

void STAWorker::recaculate_in2out() {
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

void STAWorker::candidate_recaculate() {
    recaculate_in2out();
}

}