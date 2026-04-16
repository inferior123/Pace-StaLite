#include "cell/cell_data_structure.hpp"
#include "sta/sta_data_structures.hpp"
#include "sta/sta_logger.hpp"
#include <cassert>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string>

// Liberty LUT 返回 ns，STA 内部统一用 ps
static constexpr double NS_TO_PS = 1000.0;

double get_lut_avg(std::optional<celllib::LookupTable> lut) {
  if (lut.has_value() && !lut->index_1.empty()) {
    size_t mid = lut->index_1.size() / 2;
    return lut->index_1[mid];
  } else {
    LOG_WARN << "the LookupTable is unavaliable, use deault value 0";
    return 0.0;
  }
}

// 在一个 pin 的 timing_arcs 中，按 candidate.cpp 的逻辑筛选：
// - 只考虑组合弧或 C2Q 弧（RISING_EDGE/FALLING_EDGE）
// - related_pin 匹配给定的输入 pin 名
// - 且 sdf_cond 为空（即无条件 arc），返回第一条匹配的弧
const celllib::TimingArc *find_default_arc(celllib::Pin &pin,
                                           const std::string &related_pin) {
  for (const auto &arc : pin.timing_arcs) {
    bool is_combinational =
        (arc.timing_type == celllib::TimingType::COMBINATIONAL);
    bool is_c2q = (arc.timing_type == celllib::TimingType::RISING_EDGE ||
                   arc.timing_type == celllib::TimingType::FALLING_EDGE);
    if ((!is_combinational && !is_c2q) || arc.related_pin != related_pin)
      continue;
    // 只要 sdf_cond 为空（没有条件）才认为是默认 arc
    if (arc.sdf_cond.has_value())
      continue;
    return &arc;
  }
  return nullptr;
}

double calculate_delay_rise(const celllib::TimingArc arc,
                           const celllib::CellLibrary *lib,
                           double input_slew_rise, double load_cap) {
  // 计算cell_rise延迟（使用rise的slew）
  double delay_rise = 0.0;
  if (arc.cell_rise.has_value() && arc.cell_rise->template_name.has_value()) {
    std::string template_name = arc.cell_rise->template_name.value();
    const auto *templ = lib->get_table_template(template_name);
    if (templ && templ->variable_1.has_value() &&
        templ->variable_2.has_value()) {
      std::string var1 = templ->variable_1.value();
      std::string var2 = templ->variable_2.value();

      delay_rise = lib->calculate_lookuptable(
          arc.cell_rise.value(), input_slew_rise, load_cap, var1, var2);
      delay_rise *= NS_TO_PS; // Liberty ns -> ps
    }

  } else if (arc.intrinsic_rise.has_value()) {
    delay_rise = arc.intrinsic_rise.value() * NS_TO_PS;
  }

  return delay_rise;
}

double calculate_delay_fall(const celllib::TimingArc arc,
                           const celllib::CellLibrary *lib,
                           double input_slew_fall, double load_cap) {
  double delay_fall = 0.0;
  // 计算cell_fall延迟（使用fall的slew）
  if (arc.cell_fall.has_value() && arc.cell_fall->template_name.has_value()) {
    std::string template_name = arc.cell_fall->template_name.value();
    const auto *templ = lib->get_table_template(template_name);
    if (templ && templ->variable_1.has_value() &&
        templ->variable_2.has_value()) {
      std::string var1 = "input_net_transition";
      std::string var2 = "total_output_net_capacitance";

      delay_fall = lib->calculate_lookuptable(
          arc.cell_fall.value(), input_slew_fall, load_cap, var1, var2);
      delay_fall *= NS_TO_PS; // Liberty ns -> ps
    }
  } else if (arc.intrinsic_fall.has_value()) {
    delay_fall = arc.intrinsic_fall.value() * NS_TO_PS;
  }

  return delay_fall;
}

double calculate_transition_rise(const celllib::TimingArc arc,
                                const celllib::CellLibrary *lib,
                                double input_slew_rise, double load_cap) {
  double rise_transition_time = 0.0;
  if (arc.rise_transition.has_value() &&
      arc.rise_transition->template_name.has_value()) {
    std::string template_name = arc.rise_transition->template_name.value();
    const auto *templ = lib->get_table_template(template_name);
    if (templ && templ->variable_1.has_value() &&
        templ->variable_2.has_value()) {
      std::string var1 = templ->variable_1.value();
      std::string var2 = templ->variable_2.value();

      rise_transition_time = lib->calculate_lookuptable(
          arc.rise_transition.value(), input_slew_rise, load_cap, var1, var2);
      rise_transition_time *= NS_TO_PS; // Liberty ns -> ps
    }
  }

  return rise_transition_time;
}

double calculate_transition_fall(const celllib::TimingArc arc,
                                const celllib::CellLibrary *lib,
                                double input_slew_fall, double load_cap) {
  double fall_transition_time = 0.0;
  if (arc.fall_transition.has_value() &&
      arc.fall_transition->template_name.has_value()) {
    std::string template_name = arc.fall_transition->template_name.value();
    const auto *templ = lib->get_table_template(template_name);
    if (templ && templ->variable_1.has_value() &&
        templ->variable_2.has_value()) {
      std::string var1 = "input_net_transition";
      std::string var2 = "total_output_net_capacitance";

      fall_transition_time = lib->calculate_lookuptable(
          arc.fall_transition.value(), input_slew_fall, load_cap, var1, var2);
      fall_transition_time *= NS_TO_PS; // Liberty ns -> ps
    }
  }

  return fall_transition_time;
}

// 计算 setup_rise / setup_fall 约束（单位：ns，供 LUT 使用）
double calculate_setup_rise(const celllib::TimingArc arc,
                           const celllib::CellLibrary *lib, double data_trans,
                           double clk_trans) {
  double setup_rise = 0.0;
  if (arc.rise_constraint.has_value() &&
      arc.rise_constraint->template_name.has_value()) {
    std::string template_name = arc.rise_constraint->template_name.value();
    const auto *t = lib->get_table_template(template_name);
    if (t && t->variable_1.has_value() && t->variable_2.has_value()) {
      std::string var1 = t->variable_1.value();
      std::string var2 = t->variable_2.value();

      setup_rise = lib->calculate_lookuptable(arc.rise_constraint.value(),
                                             data_trans, clk_trans, var1, var2);
    }
  }
  return setup_rise;
}

double calculate_setup_fall(const celllib::TimingArc arc,
                           const celllib::CellLibrary *lib, double data_trans,
                           double clk_trans) {
  double setup_fall = 0.0;
  if (arc.fall_constraint.has_value() &&
      arc.fall_constraint->template_name.has_value()) {
    std::string template_name = arc.fall_constraint->template_name.value();
    const auto *t = lib->get_table_template(template_name);
    if (t && t->variable_1.has_value() && t->variable_2.has_value()) {
      std::string var1 = t->variable_1.value();
      std::string var2 = t->variable_2.value();
      setup_fall = lib->calculate_lookuptable(arc.fall_constraint.value(),
                                             data_trans, clk_trans, var1, var2);
    }
  }
  return setup_fall;
}

double calculate_hold_rise(const celllib::TimingArc arc,
                          const celllib::CellLibrary *lib, double data_trans,
                          double clk_trans) {
  double hold_rise = 0.0;
  if (arc.rise_constraint.has_value() &&
      arc.rise_constraint->template_name.has_value()) {
    std::string template_name = arc.rise_constraint->template_name.value();
    const auto *t = lib->get_table_template(template_name);
    if (t && t->variable_1.has_value() && t->variable_2.has_value()) {
      std::string var1 = t->variable_1.value();
      std::string var2 = t->variable_2.value();
      hold_rise = lib->calculate_lookuptable(arc.rise_constraint.value(),
                                            data_trans, clk_trans, var1, var2);
    }
  }
  return hold_rise;
}

double calculate_hold_fall(const celllib::TimingArc arc,
                          const celllib::CellLibrary *lib, double data_trans,
                          double clk_trans) {
  double hold_fall = 0.0;
  if (arc.fall_constraint.has_value() &&
      arc.fall_constraint->template_name.has_value()) {
    std::string template_name = arc.fall_constraint->template_name.value();
    const auto *t = lib->get_table_template(template_name);
    if (t && t->variable_1.has_value() && t->variable_2.has_value()) {
      std::string var1 = t->variable_1.value();
      std::string var2 = t->variable_2.value();
      hold_fall = lib->calculate_lookuptable(arc.fall_constraint.value(),
                                            data_trans, clk_trans, var1, var2);
    }
  }
  return hold_fall;
}

sta::TransitionDirection
speculate_transition_direction(bool is_clock_to_q, celllib::TimingArc arc,
                               sta::TransitionDirection input_direction) {
  sta::TransitionDirection output_direction = sta::TransitionDirection::UNKNOWN;

  if (is_clock_to_q) {
    if (input_direction == sta::TransitionDirection::UNKNOWN) {
      if (arc.timing_type == celllib::TimingType::RISING_EDGE) {
        output_direction = sta::TransitionDirection::RISING;
      } else if (arc.timing_type == celllib::TimingType::FALLING_EDGE) {
        output_direction = sta::TransitionDirection::FALLING;
      }
    } else {
      output_direction = input_direction;
    }
  } else {
    // 如果不是 clock-to-Q 那么就直接使用前一级设置的值推算
    if (arc.timing_sense == celllib::TimingSense::NEGATIVE_UNATE) {
      // 负单边：输入上升 -> 输出下降；输入下降 -> 输出上升
      if (input_direction == sta::TransitionDirection::RISING) {
        output_direction = sta::TransitionDirection::FALLING;
      } else if (input_direction == sta::TransitionDirection::FALLING) {
        output_direction = sta::TransitionDirection::RISING;
      } else {
        output_direction = sta::TransitionDirection::UNKNOWN;
      }
    } else if (arc.timing_sense == celllib::TimingSense::POSITIVE_UNATE) {
      // 正单边：输入沿方向保持不变
      output_direction = input_direction;
    } else {
      // celllib::TimingSense::NON_UNATE
      // 非单边：无法从输入方向唯一推断，保持 UNKNOWN
      output_direction = sta::TransitionDirection::UNKNOWN;
    }
  }

  return output_direction;
}

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
  if (!cell_library_) {
    LOG_ERROR << "segment_delays_slews_gba: cell_library is null";
    return seg_res;
  }
  const auto *cell = to_ref.std_cell;
  if (!cell) {
    LOG_ERROR << "segment_delays_slews_gba: null std_cell at to_pt="
              << to_pt << " (" << to_ref.port_name << ")";
    return seg_res;
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

    TransitionDirection dir_tmp = (is_c2q) ? TransitionDirection::UNKNOWN :
          speculate_transition_direction(is_clock_to_q, arc, cur_dir);

    double rise_cap = to_ref.rise_cap;
    double fall_cap = to_ref.fall_cap;

    auto push_rise = [&](double cap) {
      double delay = calculate_delay_rise(arc, cell_library_, prev_slew, cap);
      double slew = calculate_transition_rise(arc, cell_library_, prev_slew, cap) / 1000;
      seg_res.push_back({slew, delay, TransitionDirection::RISING});
    };
    auto push_fall = [&](double cap) {
      double delay = calculate_delay_fall(arc, cell_library_, prev_slew, cap);
      double slew = calculate_transition_fall(arc, cell_library_, prev_slew, cap) / 1000;
      seg_res.push_back({slew, delay, TransitionDirection::FALLING});
    };

    if (dir_tmp == TransitionDirection::RISING) {
      push_rise(rise_cap);
    } else if (dir_tmp == TransitionDirection::FALLING) {
      push_fall(fall_cap);
    } else {
      push_rise(rise_cap);
      push_fall(fall_cap);
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
  if (!cell_library_) {
    LOG_ERROR << "segment_delays_slews: cell_library is null";
    return seg_res;
  }
  const auto *cell = to_ref.std_cell;
  if (!cell) {
    LOG_ERROR << "segment_delays_slews: null std_cell at to_pt="
              << to_pt << " (" << to_ref.port_name << ")";
    return seg_res;
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
      delay_tmp = calculate_delay_rise(arc, cell_library_, prev_slew, load_cap);
      slew_tmp =
          calculate_transition_rise(arc, cell_library_, prev_slew, load_cap) /
          1000;
    } else {
      delay_tmp = calculate_delay_fall(arc, cell_library_, prev_slew, load_cap);
      slew_tmp =
          calculate_transition_fall(arc, cell_library_, prev_slew, load_cap) /
          1000;
    }

    seg_res.push_back({slew_tmp, delay_tmp, dir_tmp});
  }
  return seg_res;
}

// 沿路径单步重算 slew：使用该方向的 max/min cap load（rise_max_cap / fall_max_cap
// 或 rise_min_cap / fall_min_cap）代替普通 cap，以便 GBA 路径 setup/hold 计算
// 得到更保守的 slew 估计。
// 返回重算后的输出 slew（单位 ns）；若该步为 WIRE 或找不到 arc，则透传 prev_slew。
double recalc_slew_with_max_cap(const TimingRunResult &res,
                                const celllib::CellLibrary *cell_library_,
                                AnalysisMode mode, std::size_t from_pt,
                                std::size_t to_pt, double prev_slew,
                                TransitionDirection out_dir) {
  if (from_pt >= res.points.size() || to_pt >= res.points.size())
    return prev_slew;
  const TimingPointRef &from_ref = res.points[from_pt];
  const TimingPointRef &to_ref   = res.points[to_pt];

  const TimingEdge *edge = nullptr;
  for (const auto &e : from_ref.fanouts) {
    if (e.target_point == to_pt) { edge = &e; break; }
  }
  if (!edge || edge->type == WIRE)
    return prev_slew;

  if (!cell_library_)
    return prev_slew;
  const auto *cell = to_ref.std_cell;
  if (!cell) {
    LOG_ERROR << "recalc_slew_with_max_cap: null std_cell at to_pt="
              << to_pt << " (" << to_ref.port_name << ")";
    return prev_slew;
  }
  const auto *output_pin = cell->get_pin(to_ref.port_name);
  if (!output_pin)
    return prev_slew;

  std::string related_pin = from_ref.port_name;
  if (edge->type == SEQ_ARC && cell->ff.has_value() &&
      cell->ff->clocked_on.has_value())
    related_pin = cell->ff->clocked_on.value();

  double cap = 0.0;
  if (out_dir == TransitionDirection::RISING)
    cap = to_ref.rise_max_cap;
  else
    cap = to_ref.fall_max_cap;

  const bool use_max = (mode == AnalysisMode::MAX);
  double best_slew = use_max ? -std::numeric_limits<double>::infinity()
                             :  std::numeric_limits<double>::infinity();
  bool found = false;

  for (const auto &arc : output_pin->timing_arcs) {
    bool is_combinational = (arc.timing_type == celllib::TimingType::COMBINATIONAL);
    bool is_c2q = (arc.timing_type == celllib::TimingType::RISING_EDGE ||
                   arc.timing_type == celllib::TimingType::FALLING_EDGE);
    if ((!is_combinational && !is_c2q) || arc.related_pin != related_pin)
      continue;

    double slew_tmp = 0.0;
    if (out_dir == TransitionDirection::RISING)
      slew_tmp = calculate_transition_rise(arc, cell_library_, prev_slew, cap) / 1000.0;
    else if(out_dir == TransitionDirection::FALLING)
      slew_tmp = calculate_transition_fall(arc, cell_library_, prev_slew, cap) / 1000.0;
    else
      assert(false && "meet an unknown path");

    if (!found || (use_max ? slew_tmp > best_slew : slew_tmp < best_slew)) {
      best_slew = slew_tmp;
      found = true;
    }
  }

  return found ? best_slew : prev_slew;
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
  if (!cell_library_) {
    LOG_ERROR << "segment_delay_slew: cell_library is null";
    return;
  }
  const auto *cell = to_ref.std_cell;
  if (!cell) {
    LOG_ERROR << "segment_delay_slew: null std_cell at to_pt=" << to_pt
              << " (" << to_ref.port_name << ")";
    return;
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
      delay_tmp = calculate_delay_rise(arc, cell_library_, prev_slew, load_cap);
      slew_tmp =
          calculate_transition_rise(arc, cell_library_, prev_slew, load_cap) /
          1000;
    } else {
      delay_tmp = calculate_delay_fall(arc, cell_library_, prev_slew, load_cap);
      slew_tmp =
          calculate_transition_fall(arc, cell_library_, prev_slew, load_cap) /
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

} // namespace sta
