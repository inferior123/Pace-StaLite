#include "sta/sta_data_structures.hpp"
#include <cassert>
#include <cstddef>
#include <limits>
#include <string>

#include "sta/debug.h"

namespace sta {
// 沿 path 的一段边 (from_pt -> to_pt) 计算延迟与输出方向。
// 若为 WIRE：delay=0，slew/方向透传；
// 若为 COMB_ARC/SEQ_ARC：遍历所有 related_pin 匹配、且 sdf_cond 为空的
// TimingArc，分别计算一遍延迟，在 MAX 模式下取最大，在 MIN 模式下取最小。
std::vector<segment_res> segment_delays_slews_gba(const TimingRunResult &res,
                        const celllib::CellLibrary *cell_library_,
                        AnalysisMode mode, std::size_t from_pt,
                        std::size_t to_pt, double prev_slew,
                        TransitionDirection cur_dir) {
  std::vector<segment_res> seg_res;
  if (from_pt >= res.points.size() || to_pt >= res.points.size())
    return seg_res;
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
    return seg_res;
  Instance *inst = to_ref.inst;
  if (!inst || !cell_library_) {
    std::cout << "[Error] cannot find the pt inst or the cell_lib is null " << std::endl;
    assert(false);
  }
  const auto *cell = cell_library_->get_cell(inst->module_name);
  if (!cell) {
    std::cout << "[Error] cannot find the cell " << inst->module_name << " in the cell_library" << std::endl;
    assert(false);
  }
    
  const std::string &output_pin_name = to_ref.port_name;
  const auto *output_pin = cell->get_pin(output_pin_name);
  if (!output_pin)
    return seg_res;

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

  for (const auto &arc : pin_mut->timing_arcs) {
    bool is_combinational =
        (arc.timing_type == celllib::TimingType::COMBINATIONAL);
    bool is_c2q = (arc.timing_type == celllib::TimingType::RISING_EDGE ||
                   arc.timing_type == celllib::TimingType::FALLING_EDGE);
    if ((!is_combinational && !is_c2q) || arc.related_pin != related_pin)
      continue;

    TransitionDirection dir_tmp =
        speculate_transition_direction(is_clock_to_q, arc, cur_dir);

    double rise_cap = to_ref.rise_cap;
    double fall_cap = to_ref.fall_cap;

    double delay_tmp = 0.0;
    double slew_tmp = 0.0;
    if (dir_tmp == TransitionDirection::RISING) {
      delay_tmp = caculate_delay_rise(arc, cell_library_, prev_slew, rise_cap);
      slew_tmp =
          caculate_transition_rise(arc, cell_library_, prev_slew, rise_cap) /
          1000;
      seg_res.push_back({slew_tmp, delay_tmp, dir_tmp});
    } else if (dir_tmp == TransitionDirection::FALLING) {
      delay_tmp = caculate_delay_fall(arc, cell_library_, prev_slew, fall_cap);
      slew_tmp =
          caculate_transition_fall(arc, cell_library_, prev_slew, fall_cap) /
          1000;
      seg_res.push_back({slew_tmp, delay_tmp, dir_tmp});
    } else {
      delay_tmp = caculate_delay_rise(arc, cell_library_, prev_slew, rise_cap);
      slew_tmp =
          caculate_transition_rise(arc, cell_library_, prev_slew, rise_cap) /
          1000;
      seg_res.push_back({slew_tmp, delay_tmp, TransitionDirection::RISING});
      delay_tmp = caculate_delay_fall(arc, cell_library_, prev_slew, fall_cap);
      slew_tmp =
          caculate_transition_fall(arc, cell_library_, prev_slew, fall_cap) /
          1000;
      seg_res.push_back({slew_tmp, delay_tmp, TransitionDirection::FALLING});
    }
  }
  return seg_res;
}

std::vector<segment_res> segment_delays_slews(const TimingRunResult &res,
                        const celllib::CellLibrary *cell_library_,
                        AnalysisMode mode, std::size_t from_pt,
                        std::size_t to_pt, double prev_slew,
                        TransitionDirection cur_dir) {
  std::vector<segment_res> seg_res;
  if (from_pt >= res.points.size() || to_pt >= res.points.size())
    return seg_res;
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
    return seg_res;
  Instance *inst = to_ref.inst;
  if (!inst || !cell_library_) {
    std::cout << "[Error] cannot find the pt inst or the cell_lib is null " << std::endl;
    assert(false);
  }
  const auto *cell = cell_library_->get_cell(inst->module_name);
  if (!cell) {
    std::cout << "[Error] cannot find the cell " << inst->module_name << " in the cell_library" << std::endl;
    assert(false);
  }
    
  const std::string &output_pin_name = to_ref.port_name;
  const auto *output_pin = cell->get_pin(output_pin_name);
  if (!output_pin)
    return seg_res;

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

  for (const auto &arc : pin_mut->timing_arcs) {
    bool is_combinational =
        (arc.timing_type == celllib::TimingType::COMBINATIONAL);
    bool is_c2q = (arc.timing_type == celllib::TimingType::RISING_EDGE ||
                   arc.timing_type == celllib::TimingType::FALLING_EDGE);
    if ((!is_combinational && !is_c2q) || arc.related_pin != related_pin)
      continue;

    TransitionDirection dir_tmp =
        speculate_transition_direction(is_clock_to_q, arc, cur_dir);

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

    seg_res.push_back({slew_tmp, delay_tmp, dir_tmp});
  }
  return seg_res;
}

void segment_delay_slew(const TimingRunResult &res,
                        const celllib::CellLibrary *cell_library_,
                        AnalysisMode mode, std::size_t from_pt,
                        std::size_t to_pt, double prev_slew,
                        TransitionDirection cur_dir, double &out_delay,
                        double &out_slew, TransitionDirection &out_dir) {
  out_delay = 0.0;
  out_slew = prev_slew;
  out_dir = cur_dir;
  if (from_pt >= res.points.size() || to_pt >= res.points.size())
    return ;
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
    return ;
  Instance *inst = to_ref.inst;
  if (!inst || !cell_library_) {
    std::cout << "[Error] cannot find the pt inst or the cell_lib is null " << std::endl;
    assert(false);
  }
  const auto *cell = cell_library_->get_cell(inst->module_name);
  if (!cell) {
    std::cout << "[Error] cannot find the cell " << inst->module_name << " in the cell_library" << std::endl;
    assert(false);
  }
    
  const std::string &output_pin_name = to_ref.port_name;
  const auto *output_pin = cell->get_pin(output_pin_name);
  if (!output_pin)
    return ;

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
      if (mode == AnalysisMode::MAX) {
        if (delay_tmp > best_delay) {
          best_delay = delay_tmp;
          best_slew = slew_tmp;
          best_dir = dir_tmp;
        }
        if (slew_tmp > best_slew) {
          best_slew = slew_tmp;
        }
      } else {
        if (delay_tmp < best_delay) {
          best_delay = delay_tmp;
          best_dir = dir_tmp;
        }
        if (slew_tmp < best_slew) {
          best_slew = slew_tmp;
        }
      }
    }
  }

  if (!has_candidate)
    return ;

  out_delay = best_delay;
  out_slew = best_slew;
  out_dir = best_dir;
}

}