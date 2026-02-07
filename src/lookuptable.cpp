#include "sta/sta_data_structures.hpp"

// Liberty LUT 返回 ns，STA 内部统一用 ps
static constexpr double NS_TO_PS = 1000.0;

// 以下函数按头文件声明在全局命名空间实现
double get_lut_avg(std::optional<celllib::LookupTable> lut) {
  if (lut.has_value() && !lut->index_1.empty()) {
    size_t mid = lut->index_1.size() / 2;
    return lut->index_1[mid];
  } else {
    std::cout << "[WARNING] the LookupTable is unavaliable, use deault value 0"
              << std::endl;
    return 0.0;
  }
}
double caculate_delay_rise(const celllib::TimingArc arc,
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
      delay_rise = lib->caculate_lookuptable(
          arc.cell_rise.value(), input_slew_rise, load_cap, var1, var2);
      delay_rise *= NS_TO_PS; // Liberty ns -> ps
    }

  } else if (arc.intrinsic_rise.has_value()) {
    delay_rise = arc.intrinsic_rise.value() * NS_TO_PS;
  }

  return delay_rise;
}

double caculate_delay_fall(const celllib::TimingArc arc,
                           const celllib::CellLibrary *lib,
                           double input_slew_fall, double load_cap) {
  double delay_fall = 0.0;
  // 计算cell_fall延迟（使用fall的slew）
  if (arc.cell_fall.has_value() && arc.cell_fall->template_name.has_value()) {
    std::string template_name = arc.cell_fall->template_name.value();
    const auto *templ = lib->get_table_template(template_name);
    if (templ && templ->variable_1.has_value() &&
        templ->variable_2.has_value()) {
      std::string var1 = templ->variable_1.value();
      std::string var2 = templ->variable_2.value();
      delay_fall = lib->caculate_lookuptable(
          arc.cell_fall.value(), input_slew_fall, load_cap, var1, var2);
      delay_fall *= NS_TO_PS; // Liberty ns -> ps
    }
  } else if (arc.intrinsic_fall.has_value()) {
    delay_fall = arc.intrinsic_fall.value() * NS_TO_PS;
  }

  return delay_fall;
}

double caculate_transition_rise(const celllib::TimingArc arc,
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
      rise_transition_time = lib->caculate_lookuptable(
          arc.rise_transition.value(), input_slew_rise, load_cap, var1, var2);
      rise_transition_time *= NS_TO_PS; // Liberty ns -> ps
    }
  }

  return rise_transition_time;
}

double caculate_transition_fall(const celllib::TimingArc arc,
                                const celllib::CellLibrary *lib,
                                double input_slew_fall, double load_cap) {
  double fall_transition_time = 0.0;
  if (arc.fall_transition.has_value() &&
      arc.fall_transition->template_name.has_value()) {
    std::string template_name = arc.fall_transition->template_name.value();
    const auto *templ = lib->get_table_template(template_name);
    if (templ && templ->variable_1.has_value() &&
        templ->variable_2.has_value()) {
      std::string var1 = templ->variable_1.value();
      std::string var2 = templ->variable_2.value();
      fall_transition_time = lib->caculate_lookuptable(
          arc.fall_transition.value(), input_slew_fall, load_cap, var1, var2);
      fall_transition_time *= NS_TO_PS; // Liberty ns -> ps
    }
  }

  return fall_transition_time;
}

// 计算 setup_rise / setup_fall 约束（单位：ns，供 LUT 使用）
double caculate_setup_rise(const celllib::TimingArc arc,
                           const celllib::CellLibrary *lib,
                           double data_trans, double clk_trans) {
  double setup_rise = 0.0;
  if (arc.rise_constraint.has_value() &&
      arc.rise_constraint->template_name.has_value()) {
    std::string template_name = arc.rise_constraint->template_name.value();
    const auto *t = lib->get_table_template(template_name);
    if (t && t->variable_1.has_value() && t->variable_2.has_value()) {
      std::string var1 = t->variable_1.value();
      std::string var2 = t->variable_2.value();
      setup_rise = lib->caculate_lookuptable(
          arc.rise_constraint.value(), data_trans, clk_trans, var1, var2);
    }
  }
  return setup_rise;
}

double caculate_setup_fall(const celllib::TimingArc arc,
                           const celllib::CellLibrary *lib,
                           double data_trans, double clk_trans) {
  double setup_fall = 0.0;
  if (arc.fall_constraint.has_value() &&
      arc.fall_constraint->template_name.has_value()) {
    std::string template_name = arc.fall_constraint->template_name.value();
    const auto *t = lib->get_table_template(template_name);
    if (t && t->variable_1.has_value() && t->variable_2.has_value()) {
      std::string var1 = t->variable_1.value();
      std::string var2 = t->variable_2.value();
      setup_fall = lib->caculate_lookuptable(
          arc.fall_constraint.value(), data_trans, clk_trans, var1, var2);
    }
  }
  return setup_fall;
}

double caculate_hold_rise(const celllib::TimingArc arc,
                          const celllib::CellLibrary *lib,
                          double data_trans, double clk_trans) {
  double hold_rise = 0.0;
  if (arc.rise_constraint.has_value() &&
      arc.rise_constraint->template_name.has_value()) {
    std::string template_name = arc.rise_constraint->template_name.value();
    const auto *t = lib->get_table_template(template_name);
    if (t && t->variable_1.has_value() && t->variable_2.has_value()) {
      std::string var1 = t->variable_1.value();
      std::string var2 = t->variable_2.value();
      hold_rise = lib->caculate_lookuptable(
          arc.rise_constraint.value(), data_trans, clk_trans, var1, var2);
    }
  }
  return hold_rise;
}

double caculate_hold_fall(const celllib::TimingArc arc,
                          const celllib::CellLibrary *lib,
                          double data_trans, double clk_trans) {
  double hold_fall = 0.0;
  if (arc.fall_constraint.has_value() &&
      arc.fall_constraint->template_name.has_value()) {
    std::string template_name = arc.fall_constraint->template_name.value();
    const auto *t = lib->get_table_template(template_name);
    if (t && t->variable_1.has_value() && t->variable_2.has_value()) {
      std::string var1 = t->variable_1.value();
      std::string var2 = t->variable_2.value();
      hold_fall = lib->caculate_lookuptable(
          arc.fall_constraint.value(), data_trans, clk_trans, var1, var2);
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