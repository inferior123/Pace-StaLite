/**
 * Convert ista::LibLibrary to celllib::CellLibrary.
 */
#include "lib_parser/lib_to_celllib_converter.hpp"
#include "lib_parser/Lib.hh"
#include "lib_parser/ista_types.hpp"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <optional>

namespace celllib {

namespace {

std::string time_unit_to_string(ista::TimeUnit u) {
  if (u == ista::TimeUnit::kNS)
    return "1ns";
  if (u == ista::TimeUnit::kPS)
    return "1ps";
  if (u == ista::TimeUnit::kFS)
    return "1fs";
  return "1ns";
}

PinDirection port_type_to_direction(ista::LibPort::LibertyPortType t) {
  if (t == ista::LibPort::LibertyPortType::kInput)
    return PinDirection::INPUT;
  if (t == ista::LibPort::LibertyPortType::kOutput)
    return PinDirection::OUTPUT;
  if (t == ista::LibPort::LibertyPortType::kInOut)
    return PinDirection::INOUT;
  return PinDirection::INPUT;
}

TimingSense arc_sense_to_celllib(ista::LibArc::TimingSense s) {
  if (s == ista::LibArc::TimingSense::kPositiveUnate)
    return TimingSense::POSITIVE_UNATE;
  if (s == ista::LibArc::TimingSense::kNegativeUnate)
    return TimingSense::NEGATIVE_UNATE;
  if (s == ista::LibArc::TimingSense::kNonUnate)
    return TimingSense::NON_UNATE;
  return TimingSense::POSITIVE_UNATE;
}

TimingType arc_type_to_celllib(ista::LibArc::TimingType t) {
  using LT = ista::LibArc::TimingType;
  switch (t) {
  case LT::kComb:
  case LT::kCombRise:
  case LT::kCombFall:
    return TimingType::COMBINATIONAL;
  case LT::kSetupRising:
    return TimingType::SETUP_RISING;
  case LT::kSetupFalling:
    return TimingType::SETUP_FALLING;
  case LT::kHoldRising:
    return TimingType::HOLD_RISING;
  case LT::kHoldFalling:
    return TimingType::HOLD_FALLING;
  case LT::kRisingEdge:
    return TimingType::RISING_EDGE;
  case LT::kFallingEdge:
    return TimingType::FALLING_EDGE;
  case LT::kClear:
    return TimingType::CLEAR;
  case LT::kPreset:
    return TimingType::PRESET;
  case LT::kMinPulseWidth:
    return TimingType::MIN_PULSE_WIDTH;
  default:
    return TimingType::COMBINATIONAL;
  }
}

/** Convert LibTable to LookupTable (index_1, index_2, values). */
std::optional<LookupTable> lib_table_to_lut(ista::LibTable *table) {
  if (!table)
    return std::nullopt;
  auto &axes = table->get_axes();
  if (axes.size() < 2)
    return std::nullopt;
  LookupTable lut;
  std::size_t n1 = axes[0]->get_axis_size();
  std::size_t n2 = axes[1]->get_axis_size();
  for (std::size_t i = 0; i < n1; ++i)
    lut.index_1.push_back((*axes[0])[i]);
  for (std::size_t i = 0; i < n2; ++i)
    lut.index_2.push_back((*axes[1])[i]);
  auto &tv = table->get_table_values();
  if (tv.size() != n1 * n2)
    return std::nullopt;
  lut.values.resize(n1);
  for (std::size_t i = 0; i < n1; ++i) {
    lut.values[i].resize(n2);
    for (std::size_t j = 0; j < n2; ++j)
      lut.values[i][j] = tv[i * n2 + j]->getFloatValue();
  }
  auto *tmpl = table->get_table_template();
  if (tmpl)
    lut.template_name = tmpl->get_template_name();
  return lut;
}

/** Variable enum -> string for TableTemplate (STA LUT lookup needs
 * variable_1/variable_2). */
static std::string var_to_string(ista::LibLutTableTemplate::Variable v) {
  using V = ista::LibLutTableTemplate::Variable;
  switch (v) {
  case V::TOTAL_OUTPUT_NET_CAPACITANCE:
    return "total_output_net_capacitance";
  case V::INPUT_NET_TRANSITION:
    return "input_net_transition";
  case V::CONSTRAINED_PIN_TRANSITION:
    return "constrained_pin_transition";
  case V::RELATED_PIN_TRANSITION:
    return "related_pin_transition";
  case V::INPUT_TRANSITION_TIME:
    return "input_transition_time";
  case V::TIME:
    return "time";
  case V::INPUT_VOLTAGE:
    return "input_voltage";
  case V::OUTPUT_VOLTAGE:
    return "output_voltage";
  default:
    return "";
  }
}

/** Convert LibLutTableTemplate to TableTemplate. */
TableTemplate lib_template_to_celllib(ista::LibLutTableTemplate *t) {
  TableTemplate out(t->get_template_name());
  auto &axes = t->get_axes();
  if (axes.size() >= 1) {
    for (std::size_t i = 0; i < axes[0]->get_axis_size(); ++i)
      out.index_1.push_back((*axes[0])[i]);
  }
  if (axes.size() >= 2) {
    for (std::size_t i = 0; i < axes[1]->get_axis_size(); ++i)
      out.index_2.push_back((*axes[1])[i]);
  }
  auto v1 = t->get_template_variable1();
  auto v2 = t->get_template_variable2();
  if (v1)
    out.variable_1 = var_to_string(*v1);
  if (v2)
    out.variable_2 = var_to_string(*v2);
  return out;
}

void convert_port(ista::LibPort *lp, Pin &out) {
  if (!lp)
    return;
  const char *pname = lp->get_port_name();
  out.name = pname ? pname : "";
  out.direction = port_type_to_direction(lp->get_port_type());
  if (lp->get_port_cap() != 0.0)
    out.capacitance = lp->get_port_cap();
  if (lp->get_func_expr_str().size())
    out.function = lp->get_func_expr_str();
  out.is_clock = (lp->isClock() != 0);
  auto cap_opt =
      lp->get_port_cap(ista::AnalysisMode::kMax, ista::TransType::kRise);
  if (cap_opt)
    out.rise_capacitance = *cap_opt;
  cap_opt = lp->get_port_cap(ista::AnalysisMode::kMax, ista::TransType::kFall);
  if (cap_opt)
    out.fall_capacitance = *cap_opt;
  auto max_cap = lp->get_port_cap_limit(ista::AnalysisMode::kMax);
  if (max_cap)
    out.max_capacitance = *max_cap;
  for (auto &ip : lp->get_internal_powers()) {
    InternalPower ipp;
    if (!ip->get_when().empty())
      ipp.when = ip->get_when();
    if (!ip->get_related_pg_port().empty())
      ipp.related_pin = ip->get_related_pg_port();
    // TODO: 部分 DFF internal_power 的 LibTable 在 get_axes() 时
    // segfault，根因待查；先跳过 power 表
    out.internal_power.push_back(ipp);
  }
}

void convert_arc(ista::LibArc *la, ista::LibPort *snk_port,
                 StandardCell &cell) {
  TimingArc arc;
  arc.related_pin = la->get_src_port();
  arc.timing_type = arc_type_to_celllib(la->get_timing_type());
  arc.timing_sense = arc_sense_to_celllib(la->get_timing_sense());
  if (la->isDisableArc())
    return;
  ista::LibTableModel *model = la->get_table_model();
  if (!model)
    return;
  if (model->isDelayModel()) {
    auto *dm = dynamic_cast<ista::LibDelayTableModel *>(model);
    if (dm) {
      if (dm->getTable(0)) {
        auto lut = lib_table_to_lut(dm->getTable(0));
        if (lut)
          arc.cell_rise = *lut;
      }
      if (dm->getTable(1)) {
        auto lut = lib_table_to_lut(dm->getTable(1));
        if (lut)
          arc.cell_fall = *lut;
      }
      if (dm->getTable(2)) {
        auto lut = lib_table_to_lut(dm->getTable(2));
        if (lut)
          arc.rise_transition = *lut;
      }
      if (dm->getTable(3)) {
        auto lut = lib_table_to_lut(dm->getTable(3));
        if (lut)
          arc.fall_transition = *lut;
      }
    }
  } else if (model->isCheckModel()) {
    auto *cm = dynamic_cast<ista::LibCheckTableModel *>(model);
    if (cm) {
      if (cm->getTable(0)) {
        auto lut = lib_table_to_lut(cm->getTable(0));
        if (lut)
          arc.rise_constraint = *lut;
      }
      if (cm->getTable(1)) {
        auto lut = lib_table_to_lut(cm->getTable(1));
        if (lut)
          arc.fall_constraint = *lut;
      }
    }
  }
  Pin *pin = cell.get_pin(snk_port->get_port_name());
  if (pin)
    pin->timing_arcs.push_back(arc);
}

/** 从时序弧推断时序单元（ff 未解析时使用）：setup/hold 的
 * related_pin=时钟，clock-to-Q 的 related_pin=时钟 */
static void infer_sequential_from_arcs(StandardCell &out) {
  std::string clocked_on;
  std::string next_state;
  std::vector<std::string> seq_outputs;
  for (auto &[pname, pin] : out.pins) {
    for (const auto &arc : pin.timing_arcs) {
      using TT = TimingType;
      if (arc.timing_type == TT::SETUP_RISING ||
          arc.timing_type == TT::SETUP_FALLING ||
          arc.timing_type == TT::HOLD_RISING ||
          arc.timing_type == TT::HOLD_FALLING) {
        if (!clocked_on.empty() && clocked_on != arc.related_pin)
          continue; // 多个时钟罕见，取第一个
        clocked_on = arc.related_pin;
        if (next_state.empty())
          next_state = pname; // 有 setup/hold 的 pin 即数据输入
      } else if (arc.timing_type == TT::RISING_EDGE ||
                 arc.timing_type == TT::FALLING_EDGE) {
        if (!clocked_on.empty() && clocked_on != arc.related_pin)
          continue;
        clocked_on = arc.related_pin;
        if (std::find(seq_outputs.begin(), seq_outputs.end(), pname) ==
            seq_outputs.end())
          seq_outputs.push_back(pname);
      }
    }
  }
  if (clocked_on.empty())
    return;
  FFDefinition ff_def;
  ff_def.clocked_on = clocked_on;
  if (!next_state.empty())
    ff_def.next_state = next_state;
  if (seq_outputs.size() >= 1)
    ff_def.state_var = seq_outputs[0];
  if (seq_outputs.size() >= 2)
    ff_def.state_var_inv = seq_outputs[1];
  out.ff = ff_def;
  // 标记时钟 pin
  auto it = out.pins.find(clocked_on);
  if (it != out.pins.end())
    it->second.is_clock = true;
}

void convert_cell(ista::LibCell *lc, StandardCell &out) {
  const char *cname = lc->get_cell_name();
  out.name = cname ? cname : "";
  out.area = lc->get_cell_area();
  out.cell_leakage_power = lc->get_cell_leakage_power();
  for (auto &p : lc->get_cell_ports()) {
    Pin pin;
    convert_port(p.get(), pin);
    out.add_pin(pin);
  }
  for (auto &pb : lc->get_cell_port_buses()) {
    for (unsigned i = 0, n = pb->getBusSize(); i < n; ++i) {
      ista::LibPort *sub = (*pb)[i];
      if (sub) {
        Pin pin;
        convert_port(sub, pin);
        out.add_pin(pin);
      }
    }
  }
  for (auto &arc_set : lc->get_cell_arcs()) {
    for (auto &la_ptr : arc_set->get_arcs()) {
      ista::LibArc *la = la_ptr.get();
      if (!la)
        continue;
      const char *snk = la->get_snk_port();
      ista::LibPort *snk_port = lc->get_cell_port_or_port_bus(snk);
      if (snk_port)
        convert_arc(la, snk_port, out);
    }
  }
  for (auto &lp : lc->get_leakage_power_list()) {
    LeakagePower lpp;
    if (lp->get_when().size())
      lpp.when = lp->get_when();
    lpp.value = lp->get_value();
    out.leakage_power.push_back(lpp);
  }
  // ff 未解析时，从 setup/hold/clock-to-Q 弧推断时序单元 if
  if (!out.ff.has_value())
    infer_sequential_from_arcs(out);
}

} // namespace

void convert_lib_to_cell_library(ista::LibLibrary *lib_lib, CellLibrary &out) {
  if (!lib_lib)
    return;
  const std::string &lib_name = lib_lib->get_lib_name();
  if (out.get_library_name().empty() && !lib_name.empty())
    out.set_library_name(lib_name);
  if (out.get_time_unit().empty())
    out.set_time_unit(time_unit_to_string(lib_lib->get_time_unit()));
  else {
    if (out.get_time_unit() != time_unit_to_string(lib_lib->get_time_unit())) {
      std::cout << "lib time unit diff " << out.get_time_unit() << " and "
                << time_unit_to_string(lib_lib->get_time_unit()) << std::endl;
    }
  }
  for (const auto &name : lib_lib->get_lut_template_names()) {
    ista::LibLutTableTemplate *t = lib_lib->getLutTemplate(name.c_str());
    if (t)
      out.add_table_template(lib_template_to_celllib(t));
  }
  for (auto &lc : lib_lib->get_cells()) {
    if (!lc)
      continue;
    StandardCell sc;
    convert_cell(lc.get(), sc);
    if (!sc.name.empty())
      out.add_cell(sc);
  }
}

} // namespace celllib
