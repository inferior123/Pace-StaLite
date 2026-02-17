#include "sta/debug.h"
#include "cell/cell_data_structure.hpp"
#include "sta/sta_data_structures.hpp"
#include <cstddef>
#include <iostream>
#include <ostream>

namespace sta {

static const char *point_type_str(PointType t) {
  switch (t) {
  case CLK_PIN:
    return "CLK_PIN";
  case CLK:
    return "CLK";
  case INPUT:
    return "INPUT";
  case OUTPUT:
    return "OUTPUT";
  case REGD:
    return "REGD";
  case REGQ:
    return "REGQ";
  case COMB_PIN:
    return "COMB_PIN";
  case CLK_SOURCE:
    return "CLK_SOURCE";
  }
  return "?";
}

static void print_point_ref(const TimingPointRef &p) {
  std::cout << " pt" << p.id;
  if (p.inst)
    std::cout << " " << p.inst->instance_name << "(" << p.inst->module_name
              << ")/";
  std::cout << p.port_name << " type=" << point_type_str(p.type);
}

void display_longest_path(STAWorker &worker) {
  worker.divide_path_entry();
  TimingRunResult res = worker.get_sta_res();
  if (res.paths.empty()) {
    std::cout << "display_longest_path: no paths\n";
    return;
  }

  // Longest = max data_arrival_time
  size_t longest_idx = 0;
  for (size_t i = 1; i < res.paths.size(); ++i) {
    if (res.paths[i].data_arrival_time >
        res.paths[longest_idx].data_arrival_time)
      longest_idx = i;
  }
  const TimingPathResult &pr = res.paths[longest_idx];
  const auto &points = res.points;

  std::cout << "\n========== Longest Path (max data_arrival) ==========\n";
  std::cout << "Path #" << pr.index << "\n";
  std::cout << "  group=" << static_cast<int>(pr.group) << "\n";
  std::cout << "  startpoint:";
  if (pr.startpoint < points.size())
    print_point_ref(points[pr.startpoint]);
  std::cout << "\n";
  std::cout << "  endpoint:";
  if (pr.endpoint < points.size())
    print_point_ref(points[pr.endpoint]);
  std::cout << "\n";
  std::cout << "  data_arrival_time=" << pr.data_arrival_time << "ps\n";
  if (pr.library_setup_time.has_value())
    std::cout << "  library_setup_time=" << pr.library_setup_time.value()
              << "ps\n";
  if (pr.library_hold_time.has_value())
    std::cout << "  library_hold_time=" << pr.library_hold_time.value()
              << "ps\n";

  std::cout << "  --- steps (step = lowest granularity) ---\n";
  for (size_t i = 0; i < pr.steps.size(); ++i) {
    const TimingStep &st = pr.steps[i];
    char d = (st.dir == TransitionDirection::RISING)
                 ? 'r'
                 : (st.dir == TransitionDirection::FALLING ? 'f' : '?');
    std::cout << "  [" << i << "] start_point:";
    if (st.start_point < points.size())
      print_point_ref(points[st.start_point]);
    std::cout << "\n";
    std::cout << "      end_point:";
    if (st.end_point < points.size())
      print_point_ref(points[st.end_point]);
    std::cout << "\n";
    std::cout << "      dir=" << d << " incr=" << st.incr
              << "ps slew=" << (st.slew / 1000.0) << "ns";
    // cap_load：仅对 output 点有值（下游 input pin 电容之和），单位 pF；与 PT
    // 差异见下
    if (st.end_point < points.size() && points[st.end_point].inst) {
      std::cout << " cap_load=" << st.cap_load << "pf";
    }
    std::cout << " arrival=" << st.arrival << "ps\n";
  }
  std::cout << "========== end longest path ==========\n\n";
}

void display_points_fanout(sta::STAWorker &worker, size_t pt_no) {
  TimingRunResult res = worker.get_sta_res();
  if (pt_no >= res.points.size())
    return;
  const TimingPointRef &pt = res.points[pt_no];

  if (pt.fanouts.empty())
    return;

  double record_cap = pt.load_cap;
  double record_rise_cap = pt.rise_cap;
  double record_fall_cap = pt.rise_cap;

  std::cout << "pt" << pt_no;
  if (pt.inst) {
    std::cout << " instance=" << pt.inst->instance_name
              << " module_name=" << pt.inst->module_name;
  } else {
    std::cout << " (top-level port)";
  }
  std::cout << "  record_cap=" << record_cap << "pf"
            << "  record_rise_cap=" << record_rise_cap << "pf"
            << "  record_fall_cap=" << record_fall_cap << "pf";
  std::cout << "  "; // 空两格后打印各 fanout
  std::cout << std::endl;

  const celllib::CellLibrary *lib = worker.get_cell_library();
  if (!lib) {
    std::cout << "(no cell library)\n";
    return;
  }

  for (const auto &fanout : pt.fanouts) {
    size_t pt_target_no = fanout.target_point;
    if (pt_target_no >= res.points.size())
      continue;
    const TimingPointRef &pt_target = res.points[pt_target_no];

    if (pt_target.inst == nullptr)
      continue;

    const celllib::StandardCell *cell =
        lib->get_cell(pt_target.inst->module_name);
    const celllib::Pin *pin =
        cell ? cell->get_pin(pt_target.port_name) : nullptr;
    if (!pin) {
      std::cout << " [fanout pt" << pt_target_no << " "
                << pt_target.inst->instance_name << "("
                << pt_target.inst->module_name << ")/" << pt_target.port_name
                << " no_pin]";
      continue;
    }

    std::cout << " [pt" << pt_target_no << " " << pt_target.inst->instance_name
              << "(" << pt_target.inst->module_name << ")/"
              << pt_target.port_name << " cap=";
    if (pin->capacitance.has_value())
      std::cout << pin->capacitance.value() << "pf";
    else
      std::cout << "(no_cap)";

    auto cap_str = [](std::optional<double> v) {
      return v.has_value() ? (std::to_string(v.value()) + "pf")
                           : std::string("no_cap");
    };

    std::cout << " rise_cap=(" << cap_str(pin->rise_capacitance_max) << ","
              << cap_str(pin->rise_capacitance_min) << ")";
    std::cout << " fall_cap=(" << cap_str(pin->fall_capacitance_max) << ","
              << cap_str(pin->fall_capacitance_min) << ")";

    std::cout << "]";

    std::cout << std::endl;
  }
  std::cout << "\n";
}

void show_lib_details(const char *cell_name, celllib::CellLibrary lib) {
  if (!cell_name || !cell_name[0]) {
    std::cout << "show_lib_details: cell_name is empty\n";
    return;
  }

  const celllib::StandardCell *cell = lib.get_cell(cell_name);
  if (!cell) {
    std::cout << "show_lib_details: cell '" << cell_name
              << "' not found in CellLibrary\n";
    auto names = lib.get_cell_names();
    std::cout << "  available cells (" << names.size() << "):";
    for (const auto &n : names)
      std::cout << " " << n;
    std::cout << "\n";
    return;
  }

  auto pin_dir_str = [](celllib::PinDirection d) {
    switch (d) {
    case celllib::PinDirection::INPUT:
      return "INPUT";
    case celllib::PinDirection::OUTPUT:
      return "OUTPUT";
    case celllib::PinDirection::INOUT:
      return "INOUT";
    case celllib::PinDirection::INTERNAL:
      return "INTERNAL";
    }
    return "?";
  };

  auto timing_type_str = [](celllib::TimingType t) {
    using T = celllib::TimingType;
    switch (t) {
    case T::COMBINATIONAL:
      return "COMBINATIONAL";
    case T::SETUP_RISING:
      return "SETUP_RISING";
    case T::SETUP_FALLING:
      return "SETUP_FALLING";
    case T::HOLD_RISING:
      return "HOLD_RISING";
    case T::HOLD_FALLING:
      return "HOLD_FALLING";
    case T::RISING_EDGE:
      return "RISING_EDGE";
    case T::FALLING_EDGE:
      return "FALLING_EDGE";
    case T::CLEAR:
      return "CLEAR";
    case T::PRESET:
      return "PRESET";
    case T::MIN_PULSE_WIDTH:
      return "MIN_PULSE_WIDTH";
    }
    return "?";
  };

  auto timing_sense_str = [](celllib::TimingSense s) {
    using S = celllib::TimingSense;
    switch (s) {
    case S::POSITIVE_UNATE:
      return "POSITIVE_UNATE";
    case S::NEGATIVE_UNATE:
      return "NEGATIVE_UNATE";
    case S::NON_UNATE:
      return "NON_UNATE";
    }
    return "?";
  };

  auto print_lut = [&lib](const char *label, const celllib::LookupTable &tb) {
    std::cout << "      LUT " << label;
    if (tb.template_name.has_value())
      std::cout << " (template=" << tb.template_name.value() << ")";
    std::cout << "\n";
    if (tb.template_name.has_value()) {
      const auto *templ = lib.get_table_template(tb.template_name.value());
      if (templ) {
        std::cout << "        template.var1="
                  << templ->variable_1.value_or("none")
                  << " var2=" << templ->variable_2.value_or("none") << "\n";
      }
    }
    std::cout << "        index_1[" << tb.index_1.size() << "] =";
    for (double v : tb.index_1)
      std::cout << " " << v;
    std::cout << "\n";
    std::cout << "        index_2[" << tb.index_2.size() << "] =";
    for (double v : tb.index_2)
      std::cout << " " << v;
    std::cout << "\n";
    std::cout << "        values (rows=index_1, cols=index_2):\n";
    for (size_t i = 0; i < tb.values.size(); ++i) {
      std::cout << "          ";
      for (size_t j = 0; j < tb.values[i].size(); ++j)
        std::cout << " " << tb.values[i][j];
      std::cout << "\n";
    }
  };

  std::cout << "===== Cell '" << cell->name << "' details =====\n";

  // Basic info
  std::cout << "  is_sequential=" << (cell->is_sequential() ? "true" : "false")
            << "\n";
  std::cout << "  pins: " << cell->pins.size() << "\n";

  // Iterate all pins (inputs/outputs/inout)
  for (const auto &kv : cell->pins) {
    const celllib::Pin &pin = kv.second;
    std::cout << "  Pin " << pin.name << " dir=" << pin_dir_str(pin.direction);
    if (pin.is_clock)
      std::cout << " (clock)";
    std::cout << "\n";

    std::cout << "    cap=";
    if (pin.capacitance.has_value())
      std::cout << pin.capacitance.value() << "pf";
    else
      std::cout << "(none)";

    auto cap_str = [](const std::optional<double> &v) {
      return v.has_value() ? (std::to_string(v.value()) + "pf")
                           : std::string("none");
    };

    std::cout << " rise_cap=(" << cap_str(pin.rise_capacitance_max) << ","
              << cap_str(pin.rise_capacitance_min) << ")";
    std::cout << " fall_cap=(" << cap_str(pin.fall_capacitance_max) << ","
              << cap_str(pin.fall_capacitance_min) << ")";

    std::cout << " max_cap=";
    if (pin.max_capacitance.has_value())
      std::cout << pin.max_capacitance.value() << "pf";
    else
      std::cout << "(none)";
    std::cout << "\n";

    if (pin.function.has_value())
      std::cout << "    function=" << pin.function.value() << "\n";

    // Timing arcs on this pin
    if (!pin.timing_arcs.empty()) {
      std::cout << "    timing_arcs (" << pin.timing_arcs.size() << "):\n";
      for (size_t i = 0; i < pin.timing_arcs.size(); ++i) {
        const celllib::TimingArc &arc = pin.timing_arcs[i];
        std::cout << "      [" << i << "] related_pin=" << arc.related_pin
                  << " type=" << timing_type_str(arc.timing_type)
                  << " sense=" << timing_sense_str(arc.timing_sense)
                  << " sdf_cond="
                  << (arc.sdf_cond.has_value()
                          ? ("\"" + arc.sdf_cond.value() + "\"")
                          : std::string("none"))
                  << "\n";
        if (arc.intrinsic_rise.has_value())
          std::cout << "        intrinsic_rise=" << arc.intrinsic_rise.value()
                    << "ns\n";
        if (arc.intrinsic_fall.has_value())
          std::cout << "        intrinsic_fall=" << arc.intrinsic_fall.value()
                    << "ns\n";
        if (arc.cell_rise.has_value())
          print_lut("cell_rise", *arc.cell_rise);
        if (arc.cell_fall.has_value())
          print_lut("cell_fall", *arc.cell_fall);
        if (arc.rise_transition.has_value())
          print_lut("rise_transition", *arc.rise_transition);
        if (arc.fall_transition.has_value())
          print_lut("fall_transition", *arc.fall_transition);
        if (arc.rise_constraint.has_value())
          print_lut("rise_constraint", *arc.rise_constraint);
        if (arc.fall_constraint.has_value())
          print_lut("fall_constraint", *arc.fall_constraint);
      }
    }
  }

  std::cout << "===== end cell '" << cell->name << "' =====\n";
}

void debug_paths_through_instance(STAWorker &worker,
                                  const std::string &inst_substr) {
  worker.divide_path_entry();
  const AnalysisMode mode = worker.get_analysis_mode();
  const PathGroup group = PathGroup::REG2OUT;
  const AnalysisMode target_mode = AnalysisMode::MAX;

  if (mode != target_mode)
    return;

  // 实现 print_point_group 辅助函数
  auto print_point_group = [](sta::PathGroup group) -> const char * {
    switch (group) {
    case sta::PathGroup::REG2REG:
      return "REG2REG";
    case sta::PathGroup::IN2REG:
      return "IN2REG";
    case sta::PathGroup::REG2OUT:
      return "REG2OUT";
    case sta::PathGroup::IN2OUT:
      return "IN2OUT";
    default:
      return "?";
    }
  };

  size_t entries_size = worker.get_entries_size(group, mode);

  // 这里以 IN2OUT 组为例，查看最差的前 3 条路径
  std::cout << "[" << print_point_group(group) << " "
            << (mode == AnalysisMode::MAX ? "max" : "min")
            << "entries size: " << entries_size << "]\n";
  for (std::size_t i = 0; i < entries_size && i < 3; ++i) {
    const PathEntry *e = worker.get_top_k(group, mode, i); // 最差若干条
    if (!e || !e->path)
      break;
    worker.display_result_path_detail(*e->path);
    display_points_fanout(worker, 2137);
  }
}

} // namespace sta
