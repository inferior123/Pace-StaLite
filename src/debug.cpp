#include "sta/debug.h"
#include "cell/cell_data_structure.hpp"
#include "sta/sta_data_structures.hpp"
#include <cstddef>
#include <iostream>
#include <ostream>
#include <string>

namespace sta {

static const char *dir_char(TransitionDirection d) {
  return d == TransitionDirection::RISING    ? "R"
         : d == TransitionDirection::FALLING ? "F"
                                             : "?";
}

void debug_dfs_start_nodes(const std::vector<std::size_t> &node_ids,
                           const CandidateGraphy &cg) {
  std::cerr << "[run_candidate_dfs] start_node_ids(" << node_ids.size() << "):";
  for (std::size_t nid : node_ids)
    std::cerr << " n" << nid << "(pt" << cg.nodes[nid].point_idx << ")";
  std::cerr << "\n";
}

void debug_dfs_start_node(std::size_t node_id, std::size_t pt) {
  std::cerr << "[run_candidate_dfs] === start_node n" << node_id << " pt" << pt
            << " ===\n";
}

void debug_dfs_push_or_continue(std::size_t end_node_id, std::size_t end_pt,
                                double delay, bool terminal,
                                TransitionDirection dir) {
  std::cerr << "[run_candidate_dfs]   push_or_continue end_n" << end_node_id
            << " pt" << end_pt << " delay=" << delay << " "
            << (terminal ? "-> PUSH" : "-> recurse")
            << " dir=" << dir_char(dir) << "\n";
}

void debug_dfs_dup_skip() {
  std::cerr << "[run_candidate_dfs]   (dup fp, skip)\n";
}

void debug_dfs_push_path(std::size_t path_idx, std::size_t startpoint,
                         std::size_t endpoint, double arrival) {
  std::cerr << "[run_candidate_dfs]   PUSH path #" << path_idx << " start_pt"
            << startpoint << " -> end_pt" << endpoint << " arr=" << arrival
            << "\n";
}

void debug_dfs_emit_chains(std::size_t node_id, std::size_t pt,
                           TransitionDirection dir, double delay_so_far,
                           std::size_t fanout_cnt, std::size_t relate_cnt) {
  std::cerr << "[run_candidate_dfs] emit_chains cur_n" << node_id << " pt"
            << pt << " dir=" << dir_char(dir)
            << " delay_so_far=" << delay_so_far << " fanout_paths=" << fanout_cnt
            << " relate=" << relate_cnt << "\n";
}

void debug_dfs_unate_path(std::size_t path_id, std::size_t end_node_id,
                          std::size_t end_pt) {
  std::cerr << "[run_candidate_dfs]   path" << path_id << " -> end_n"
            << end_node_id << " pt" << end_pt << " unate\n";
}

void debug_dfs_relate(std::size_t to_node_id, std::size_t to_pt) {
  std::cerr << "[run_candidate_dfs]   relate -> end_n" << to_node_id << " pt"
            << to_pt << " (rise+fall)\n";
}

void debug_dfs_branch(bool is_rise, std::size_t start_node_id) {
  std::cerr << "[run_candidate_dfs] --- branch " << (is_rise ? "rise" : "fall")
            << " from n" << start_node_id << " ---\n";
}

void debug_dfs_total_paths(std::size_t total) {
  std::cerr << "[run_candidate_dfs] total paths=" << total << "\n";
}

void debug_non_unate_entry(std::size_t from_pt, std::size_t to_pt,
                           const TimingPointRef &from_ref,
                           const TimingPointRef &to_ref,
                           TransitionDirection output_dir, double input_slew_ns,
                           AnalysisMode mode) {
  std::cerr << "[non_unate_debug] from_pt=" << from_pt << " to_pt=" << to_pt
            << " inst=" << (to_ref.inst ? to_ref.inst->instance_name : "?")
            << "(" << (to_ref.inst ? to_ref.inst->module_name : "?") << ")"
            << " output_pin=" << to_ref.port_name
            << " input_pin=" << from_ref.port_name
            << " output_dir="
            << (output_dir == TransitionDirection::RISING
                    ? "R"
                    : (output_dir == TransitionDirection::FALLING ? "F" : "?"))
            << " input_slew_ns=" << input_slew_ns
            << " rise_cap=" << to_ref.rise_cap
            << " fall_cap=" << to_ref.fall_cap
            << " mode=" << (mode == AnalysisMode::MAX ? "MAX" : "MIN") << "\n";
}

void debug_non_unate_arc(const celllib::TimingArc &arc, bool is_comb,
                         bool is_c2q, double load_cap, double delay_tmp,
                         double slew_tmp) {
  std::cerr << "  [non_unate_debug] arc related_pin=" << arc.related_pin
            << " sdf_cond="
            << (arc.sdf_cond.has_value() ? arc.sdf_cond.value()
                                         : std::string("none"))
            << " is_comb=" << (is_comb ? "Y" : "N")
            << " is_c2q=" << (is_c2q ? "Y" : "N") << " load_cap=" << load_cap
            << " delay_tmp=" << delay_tmp << " slew_tmp_ns=" << slew_tmp
            << "\n";
}

void debug_non_unate_summary(std::size_t from_pt, std::size_t to_pt,
                             bool has_unate, double best_delay, double best_slew,
                             double unate_delay, double unate_slew) {
  std::cerr << "[non_unate_debug_summary] from_pt=" << from_pt
            << " to_pt=" << to_pt << " has_unate=" << (has_unate ? "Y" : "N")
            << " best_delay=" << best_delay << " best_slew_ns=" << best_slew
            << " unate_delay=" << unate_delay
            << " unate_slew_ns=" << unate_slew << std::endl;
}

} // namespace sta

std::string group_type_str(sta::PathGroup group) {
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
}

auto timing_type_str(celllib::TimingType type) {
  using T = celllib::TimingType;
  switch (type) {
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
}

auto timing_sense_str(celllib::TimingSense sense) {
  using S = celllib::TimingSense;
  switch (sense) {
  case S::POSITIVE_UNATE:
    return "POSITIVE_UNATE";
  case S::NEGATIVE_UNATE:
    return "NEGATIVE_UNATE";
  case S::NON_UNATE:
    return "NON_UNATE";
  }
  return "?";
}

auto pin_dir_str(celllib::PinDirection dir) {
  switch (dir) {
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
}

std::string point_type_str(sta::PointType type) {
  switch (type) {
  case sta::CLK:
    return "CLK";
  case sta::CLK_PIN:
    return "CLK_PIN";
  case sta::INPUT:
    return "INPUT";
  case sta::OUTPUT:
    return "OUTPUT";
  case sta::REGD:
    return "REGD";
  case sta::REGQ:
    return "REGQ";
  case sta::COMB_PIN:
    return "COMB_PIN";
  case sta::CLK_SOURCE:
    return "CLK_COURCE";
  }

  return "?";
}

namespace sta {

void STAWorker::display_result_path_detail(const TimingPathResult &pr) const {
  std::cout << "\n--- Result Path #" << pr.index << " ---" << std::endl;
  std::cout << "  startpoint: pt" << pr.startpoint;
  if (pr.startpoint < res.points.size()) {
    const auto &p = res.points[pr.startpoint];
    std::cout << " ";
    if (p.inst)
      std::cout << p.inst->instance_name << "(" << p.inst->module_name << ")/";
    std::cout << p.port_name << " type=" << point_type_str(p.type);
  }
  std::cout << "\n  endpoint:   pt" << pr.endpoint;
  std::cout << "\n  path id:    #" << pr.index;
  if (pr.endpoint < res.points.size()) {
    const auto &p = res.points[pr.endpoint];
    std::cout << " ";
    if (p.inst)
      std::cout << p.inst->instance_name << "(" << p.inst->module_name << ")/";
    std::cout << p.port_name << " type=" << point_type_str(p.type);
  }
  std::cout << "\n  data_arrival=" << pr.data_arrival_time << "ps\n";

  for (std::size_t i = 0; i < pr.steps.size(); ++i) {
    const TimingStep &st = pr.steps[i];
    std::cout << "  [" << i << "] pt" << st.start_point << " -> pt"
              << st.end_point;
    if (st.end_point < res.points.size()) {
      const auto &to_ref = res.points[st.end_point];
      std::cout << " ";
      if (to_ref.inst)
        std::cout << to_ref.inst->instance_name << "("
                  << to_ref.inst->module_name << ")/";
      std::cout << to_ref.port_name;
    }
    char d = (st.dir == TransitionDirection::RISING)
                 ? 'r'
                 : (st.dir == TransitionDirection::FALLING ? 'f' : '?');
    std::cout << " dir=" << d << " incr=" << st.incr << "ps"
              << " slew=" << (st.slew / 1000.0) << "ns"
              << " cap=" << st.cap_load << "pf"
              << " arrival=" << st.arrival << "ps";
    bool is_last = (i == pr.steps.size() - 1);
    if (is_last) {
      if (pr.library_setup_time.has_value())
        std::cout << " setup=" << pr.library_setup_time.value() << "ps";
      if (pr.library_hold_time.has_value())
        std::cout << " hold=" << pr.library_hold_time.value() << "ps";
    }
    std::cout << "\n";

    // Extra debug: print arc details (including timing_sense) for this step
    if (cell_library_ && st.start_point < res.points.size() &&
        st.end_point < res.points.size()) {
      const auto &from_ref = res.points[st.start_point];
      const auto &to_ref = res.points[st.end_point];

      if (to_ref.inst) {
        const auto *cell = cell_library_->get_cell(to_ref.inst->module_name);
        if (cell) {
          const auto *out_pin = cell->get_pin(to_ref.port_name);
          if (out_pin) {
            // Find edge type and related_pin (with clk2q remap)
            const TimingEdge *edge = nullptr;
            for (const auto &e : from_ref.fanouts) {
              if (e.target_point == st.end_point) {
                edge = &e;
                break;
              }
            }

            std::string related_pin = from_ref.port_name;
            if (edge && edge->type == SEQ_ARC && cell->ff.has_value() &&
                cell->ff->clocked_on.has_value()) {
              related_pin = cell->ff->clocked_on.value();
            }

            // Print all matching arcs for this from->to step
            for (const auto &arc : out_pin->timing_arcs) {
              bool is_combinational =
                  (arc.timing_type == celllib::TimingType::COMBINATIONAL);
              bool is_c2q =
                  (arc.timing_type == celllib::TimingType::RISING_EDGE ||
                   arc.timing_type == celllib::TimingType::FALLING_EDGE);
              if ((!is_combinational && !is_c2q) ||
                  arc.related_pin != related_pin)
                continue;

              std::cout << "      arc: related_pin=" << arc.related_pin
                        << " type=" << timing_type_str(arc.timing_type)
                        << " sense=" << timing_sense_str(arc.timing_sense)
                        << " sdf_cond=";
              if (arc.sdf_cond.has_value())
                std::cout << "\"" << arc.sdf_cond.value() << "\"";
              else
                std::cout << "none";
              std::cout << "\n";
            }
          }
        }
      }
    }
  }

  AnalysisMode mode = get_analysis_mode();
  if (mode == AnalysisMode::MAX) {
    double required = static_cast<double>(get_effective_clock_period());
    double setup_ps = pr.library_setup_time.value_or(0.0);
    if (setup_ps > 0.0)
      required -= setup_ps;
    double slack = required - pr.data_arrival_time;
    std::cout << "  required=" << required << "ps  slack=" << slack << "ps\n";
  } else {
    double required = pr.library_hold_time.value_or(0.0);
    double slack = pr.data_arrival_time - required;
    std::cout << "  required=" << required << "ps  slack=" << slack << "ps\n";
  }
  std::cout << "  ---\n";
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
  const PathGroup group = PathGroup::IN2REG;
  const AnalysisMode target_mode = AnalysisMode::MIN;

  if (mode != target_mode)
    return;

  size_t entries_size = worker.get_entries_size(group, mode);

  std::cout << "[" << group_type_str(group) << " "
            << (mode == AnalysisMode::MAX ? "max" : "min")
            << " entries size: " << entries_size << "]\n";
  for (std::size_t i = 0; i < entries_size && i < 5; ++i) {
    const PathEntry *e = nullptr;
    if (mode == AnalysisMode::MAX) {
      e = worker.get_top_k(group, mode, i); // MAX：从前往后取最差若干条
    } else {
      e = worker.get_top_k(group, mode, i); // MIN：从后往前取最差若干条
    }

    if (!e || !e->path)
      break;
    worker.display_result_path_detail(*e->path);
    display_points_fanout(worker, 2137);
  }
}

} // namespace sta
