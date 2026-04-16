#include "cell/cell_data_structure.hpp"
#include "sta/sta_data_structures.hpp"
#include "sta/sta_logger.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace verilog;

namespace sta {

std::size_t STAWorker::get_or_create_point_node(Instance *inst,
                                                const SignalBit &bit,
                                                const std::string &port_name) {
  return get_or_create_point(inst, port_name, bit, COMB_PIN);
}

std::size_t STAWorker::get_or_create_point(Instance *inst,
                                           const std::string &port_name,
                                           std::optional<SignalBit> bit,
                                           PointType type) {
  TimingPointRefKey key{inst, port_name, bit};
  auto it = res.point_index.find(key);
  if (it != res.point_index.end()) {
    return it->second;
  }

  std::size_t id = res.points.size();
  res.point_index.emplace(key, id);

  TimingPointRef point;
  point.id = id;
  point.inst = inst;
  point.std_cell = nullptr;
  point.port_name = port_name;
  point.bit = bit;
  point.type = type;
  point.fanouts = {};

  res.points.push_back(std::move(point));
  return id;
}

static inline void check_cell_lib(const celllib::CellLibrary *lib) {
  if (!lib) {
    assert(false && "CellLibrary is required, no hardcoded fallback");
  }
  return;
}

void STAWorker::fanout_check_preconditions() const {
  assert(has_clock && "clock must be specified before build_fanouts");
  check_cell_lib(cell_library_);
}

void STAWorker::fanout_seed_clk_input_drivers(
    FanoutBitDriverMap &bit_to_driver) const {
  for (std::size_t pt_id : input_clk_point_ids) {
    if (pt_id < res.points.size() && res.points[pt_id].bit.has_value())
      // Use current sigmap canonical for robustness (union-find rep may
      // change after parsing assigns/connections).
      bit_to_driver[sigmap.find(res.points[pt_id].bit.value())] = pt_id;
  }
}

SignalBit STAWorker::fanout_resolve_sequential_clock_bit(
    Instance *inst, const std::string &clock_pin_name) const {
  if (inst->connections.count(clock_pin_name)) {
    const SignalSpec &clock_signals = inst->connections.at(clock_pin_name);
    if (!clock_signals.empty())
      return sigmap.find(clock_signals[0]);
  }

  LOG_ERROR << "Sequential \"" << inst->instance_name << "\" ("
            << inst->module_name << "): clock pin \"" << clock_pin_name
            << "\" has no net (netlist issue); trying cfg.clk_name=\""
            << cfg.clk_name << "\"";
  auto it = signal_registry.find(cfg.clk_name);
  if (it == signal_registry.end() || it->second.empty()) {
    LOG_ERROR << "Cannot fall back: top port \"" << cfg.clk_name
              << "\" not in signal_registry";
    std::exit(1);
  }
  return sigmap.find(it->second[0]);
}

void STAWorker::fanout_process_sequential_instance(
    Instance *inst, const celllib::StandardCell &cell,
    FanoutBitDriverMap &bit_to_driver,
    std::vector<FanoutPendingEdge> &pending) {
  const auto &ff_def = cell.ff.value();
  const std::string clock_pin_name = ff_def.clocked_on.value_or("CK");

  const SignalBit clock_canonical =
      fanout_resolve_sequential_clock_bit(inst, clock_pin_name);

  std::size_t clk_pt =
      get_or_create_point(inst, clock_pin_name, clock_canonical, CLK_PIN);
  if (clk_pt < res.points.size())
    res.points[clk_pt].type = CLK_PIN;
  res.points[clk_pt].std_cell = &cell;
  input_clk_point_ids.push_back(clk_pt);

  for (const auto &output_pin_name : cell.get_output_pins()) {
    const auto *output_pin = cell.get_pin(output_pin_name);
    if (!output_pin || !inst->connections.count(output_pin_name))
      continue;
    const SignalSpec &output_signals = inst->connections[output_pin_name];

    for (const auto &arc : output_pin->timing_arcs) {
      if ((arc.timing_type == celllib::TimingType::RISING_EDGE ||
           arc.timing_type == celllib::TimingType::FALLING_EDGE) &&
          arc.related_pin == clock_pin_name) {
        if (analysis_granularity_ != AnalysisGranularity::COARSE)
          get_or_create_candidate_node(clk_pt);
        for (size_t i = 0; i < output_signals.size(); ++i) {
          SignalBit output_canonical = sigmap.find(output_signals[i]);
          std::size_t regq_pt = get_or_create_point(inst, output_pin_name,
                                                    output_canonical, REGQ);
          res.points[regq_pt].std_cell = &cell;
          pending.push_back({clk_pt, regq_pt, SEQ_ARC});
          bit_to_driver[output_canonical] = regq_pt;
        }
        break;
      }
    }
  }

  for (const auto &input_pin_name : cell.get_input_pins()) {
    if (input_pin_name == clock_pin_name)
      continue;
    if (!inst->connections.count(input_pin_name))
      continue;
    const SignalSpec &input_signals = inst->connections[input_pin_name];
    for (size_t i = 0; i < input_signals.size(); ++i) {
      SignalBit input_canonical = sigmap.find(input_signals[i]);
      std::size_t regd_pt =
          get_or_create_point(inst, input_pin_name, input_canonical, REGD);
      res.points[regd_pt].std_cell = &cell;
      auto it = bit_to_driver.find(input_canonical);
      if (it != bit_to_driver.end())
        pending.push_back({it->second, regd_pt, WIRE});
    }
  }
}

void STAWorker::fanout_process_combinational_instance(
    Instance *inst, const celllib::StandardCell &cell,
    FanoutBitDriverMap &bit_to_driver,
    std::vector<FanoutPendingEdge> &pending) {
  for (const auto &output_pin_name : cell.get_output_pins()) {
    const auto *output_pin = cell.get_pin(output_pin_name);
    if (!output_pin || !inst->connections.count(output_pin_name))
      continue;
    const SignalSpec &output_signals = inst->connections[output_pin_name];

    for (const auto &arc : output_pin->timing_arcs) {
      if (arc.timing_type != celllib::TimingType::COMBINATIONAL)
        continue;
      const std::string &input_pin_name = arc.related_pin;
      if (!inst->connections.count(input_pin_name))
        continue;
      const SignalSpec &input_signals = inst->connections[input_pin_name];

      for (size_t i = 0; i < input_signals.size() && i < output_signals.size();
           ++i) {
        SignalBit input_canonical = sigmap.find(input_signals[i]);
        SignalBit output_canonical = sigmap.find(output_signals[i]);

        std::size_t input_pt = get_or_create_point(inst, input_pin_name,
                                                   input_canonical, COMB_PIN);
        std::size_t output_pt = get_or_create_point(inst, output_pin_name,
                                                    output_canonical, COMB_PIN);
        res.points[input_pt].std_cell = &cell;
        res.points[output_pt].std_cell = &cell;

        if (arc.timing_sense == celllib::TimingSense::NON_UNATE) {
          std::size_t input_node_id = get_or_create_candidate_node(input_pt);
          std::size_t output_node_id = get_or_create_candidate_node(output_pt);

          if (input_node_id != SIZE_MAX && output_node_id != SIZE_MAX &&
              input_node_id < candidate_graphy_.nodes.size() &&
              output_node_id < candidate_graphy_.nodes.size())
            candidate_graphy_.nodes[input_node_id]
                .relate_candidate_point.push_back(output_node_id);

          res.points[input_pt].candidate_idx = input_node_id;
          res.points[output_pt].candidate_idx = output_node_id;
        }

        pending.push_back({input_pt, output_pt, COMB_ARC});
        auto driver_it = bit_to_driver.find(input_canonical);
        if (driver_it != bit_to_driver.end())
          pending.push_back({driver_it->second, input_pt, WIRE});

        auto existing = bit_to_driver.find(output_canonical);
        if (existing == bit_to_driver.end() ||
            res.points[existing->second].type != REGQ) {
          bit_to_driver[output_canonical] = output_pt;
        }
      }
    }
  }
}

void STAWorker::fanout_first_pass_instances(
    FanoutBitDriverMap &bit_to_driver,
    std::vector<FanoutPendingEdge> &pending) {
  for (auto &instance : instances) {
    const auto *cell = cell_library_->get_cell(instance->module_name);
    if (!cell) {
      std::cerr << "Warning: Cannot find cell '" << instance->module_name
                << "' in CellLibrary, skipping." << std::endl;
      continue;
    }
    if (cell->ff.has_value())
      fanout_process_sequential_instance(instance.get(), *cell, bit_to_driver,
                                         pending);
    else
      fanout_process_combinational_instance(instance.get(), *cell,
                                            bit_to_driver, pending);
  }
}

bool STAWorker::fanout_pending_has(
    const std::vector<FanoutPendingEdge> &pending, std::size_t from_pt,
    std::size_t to_pt) {
  for (const auto &e : pending)
    if (e.from_pt == from_pt && e.to_pt == to_pt)
      return true;
  return false;
}

void STAWorker::fanout_patch_wires_driver_to_loads(
    const FanoutBitDriverMap &bit_to_driver,
    std::vector<FanoutPendingEdge> &pending) {
  for (const auto &pt : res.points) {
    if (!pt.bit.has_value())
      continue;
    // Use current sigmap canonical when looking up driver.
    auto it = bit_to_driver.find(sigmap.find(pt.bit.value()));
    if (it == bit_to_driver.end() || it->second == pt.id)
      continue;
    std::size_t driver_pt_id = it->second;
    const TimingPointRef &driver = res.points[driver_pt_id];
    if (driver.inst != nullptr && driver.inst == pt.inst)
      continue;
    if (!fanout_pending_has(pending, driver_pt_id, pt.id))
      pending.push_back({driver_pt_id, pt.id, WIRE});
  }
}

void STAWorker::fanout_patch_regd_secondary_pass(
    const FanoutBitDriverMap &bit_to_driver,
    std::vector<FanoutPendingEdge> &pending) {
  for (auto &instance : instances) {
    const auto *cell = cell_library_->get_cell(instance->module_name);
    if (!cell || !cell->ff.has_value())
      continue;
    const auto &ff_def = cell->ff.value();
    std::string clock_pin_name = ff_def.clocked_on.value_or("CK");
    for (const auto &input_pin_name : cell->get_input_pins()) {
      if (input_pin_name == clock_pin_name ||
          !instance->connections.count(input_pin_name))
        continue;
      const SignalSpec &input_signals = instance->connections[input_pin_name];
      for (size_t i = 0; i < input_signals.size(); ++i) {
        SignalBit input_canonical = sigmap.find(input_signals[i]);
        TimingPointRefKey key{instance.get(), input_pin_name, input_canonical};
        auto kit = res.point_index.find(key);
        if (kit == res.point_index.end())
          continue;
        std::size_t regd_pt = kit->second;
        auto bit_it = bit_to_driver.find(input_canonical);
        if (bit_it != bit_to_driver.end()) {
          std::size_t from_pt = bit_it->second;
          if (!fanout_pending_has(pending, from_pt, regd_pt))
            pending.push_back({from_pt, regd_pt, WIRE});
        }
      }
    }
  }
}

void STAWorker::fanout_wire_primary_outputs(
    const FanoutBitDriverMap &bit_to_driver,
    std::vector<FanoutPendingEdge> &pending) {
  for (const auto &pt : res.points) {
    if (pt.type != OUTPUT || !pt.bit.has_value())
      continue;
    SignalBit out_bit = sigmap.find(pt.bit.value());
    auto it = bit_to_driver.find(out_bit);
    if (it != bit_to_driver.end())
      pending.push_back({it->second, pt.id, WIRE});
  }
}

void STAWorker::fanout_apply_pending_edges(
    const std::vector<FanoutPendingEdge> &pending) {
  for (const auto &e : pending) {
    auto &fanouts = res.points[e.from_pt].fanouts;
    bool already = false;
    for (const auto &f : fanouts)
      if (f.target_point == e.to_pt) {
        already = true;
        break;
      }
    if (!already)
      fanouts.push_back(TimingEdge{e.type, e.from_pt, e.to_pt});
  }
}

void STAWorker::build_fanouts() {
  fanout_check_preconditions();

  FanoutBitDriverMap bit_to_driver;
  fanout_seed_clk_input_drivers(bit_to_driver);

  std::vector<FanoutPendingEdge> pending;
  fanout_first_pass_instances(bit_to_driver, pending);

  fanout_patch_wires_driver_to_loads(bit_to_driver, pending);
  fanout_patch_regd_secondary_pass(bit_to_driver, pending);
  fanout_wire_primary_outputs(bit_to_driver, pending);

  fanout_apply_pending_edges(pending);
  build_res_edges();
}

namespace {

// 与 STAWorker::calculate_load_cap 一致：按 MAX/MIN 分析模式解析 Liberty
// 电容回退链。
inline double pin_rise_cap_for_analysis(const celllib::Pin &pin, bool use_max) {
  if (use_max) {
    if (pin.rise_capacitance_max.has_value())
      return pin.rise_capacitance_max.value();
    if (pin.capacitance.has_value())
      return pin.capacitance.value();
    return 0;
  }
  if (pin.rise_capacitance_min.has_value())
    return pin.rise_capacitance_min.value();
  if (pin.rise_capacitance_max.has_value())
    return pin.rise_capacitance_max.value();
  if (pin.capacitance.has_value())
    return pin.capacitance.value();
  return 0;
}

inline double pin_fall_cap_for_analysis(const celllib::Pin &pin, bool use_max) {
  if (use_max) {
    if (pin.fall_capacitance_max.has_value())
      return pin.fall_capacitance_max.value();
    if (pin.capacitance.has_value())
      return pin.capacitance.value();
    return 0;
  }
  if (pin.fall_capacitance_min.has_value())
    return pin.fall_capacitance_min.value();
  if (pin.fall_capacitance_max.has_value())
    return pin.fall_capacitance_max.value();
  if (pin.capacitance.has_value())
    return pin.capacitance.value();
  return 0;
}

inline void accumulate_pin_corner_caps(const celllib::Pin &pin,
                                       TimingPointRef &pt) {
  if (pin.rise_capacitance_max.has_value())
    pt.rise_max_cap += pin.rise_capacitance_max.value();
  else if (pin.capacitance.has_value())
    pt.rise_max_cap += pin.capacitance.value();

  if (pin.rise_capacitance_min.has_value())
    pt.rise_min_cap += pin.rise_capacitance_min.value();
  else if (pin.capacitance.has_value())
    pt.rise_min_cap += pin.capacitance.value();

  if (pin.fall_capacitance_max.has_value())
    pt.fall_max_cap += pin.fall_capacitance_max.value();
  else if (pin.capacitance.has_value())
    pt.fall_max_cap += pin.capacitance.value();

  if (pin.fall_capacitance_min.has_value())
    pt.fall_min_cap += pin.fall_capacitance_min.value();
  else if (pin.capacitance.has_value())
    pt.fall_min_cap += pin.capacitance.value();
}

} // namespace

void STAWorker::build_res_edges() {
  res.edges.clear();
  for (std::size_t i = 0; i < res.points.size(); ++i) {
    for (const TimingEdge &e : res.points[i].fanouts) {
      res.edges.push_back(TimingEdge{e.type, i, e.target_point});
    }
  }
}

void TimingPointRef::calculate_capacitance(
    bool is_max, const TimingRunResult &res,
    const celllib::CellLibrary *cell_library) {
  if (type != COMB_PIN && type != REGQ && type != INPUT)
    return;
  if (!cell_library)
    return;

  for (const TimingEdge &e : fanouts) {
    if (e.target_point >= res.points.size()) {
      LOG_WARN << "target point out of range";
      continue;
    }

    const TimingPointRef &target = res.points[e.target_point];
    if (!target.inst) {
      LOG_WARN << "target is not an instance";
      continue;
    }

    const auto *fanout_cell = cell_library->get_cell(target.inst->module_name);
    if (!fanout_cell) {
      LOG_WARN << "fanout cell not found";
      continue;
    }

    const auto *input_pin = fanout_cell->get_pin(target.port_name);
    if (!input_pin) {
      LOG_WARN << "input pin not found";
      continue;
    }

    const double rise = pin_rise_cap_for_analysis(*input_pin, is_max);
    const double fall = pin_fall_cap_for_analysis(*input_pin, is_max);
    if (is_max)
      load_cap += rise + fall;
    else {
      rise_cap += rise;
      fall_cap += fall;
    }

    accumulate_pin_corner_caps(*input_pin, *this);
  }
}

void STAWorker::calculate_load_cap() {
  if (!cell_library_)
    return;

  std::deque<std::size_t> queue(input_clk_point_ids.begin(),
                                input_clk_point_ids.end());
  std::unordered_set<std::size_t> visited;
  while (!queue.empty()) {
    std::size_t pt_id = queue.front();
    queue.pop_front();
    if (visited.count(pt_id))
      continue;
    visited.insert(pt_id);
    TimingPointRef &pt = res.points[pt_id];
    if (pt.type == COMB_PIN || pt.type == REGQ || pt.type == INPUT) {
      for (const TimingEdge &e : pt.fanouts) {
        if (e.target_point >= res.points.size())
          continue;
        const TimingPointRef &target = res.points[e.target_point];
        if (!target.inst) // 输出是output
          continue;
        const auto *fanout_cell =
            cell_library_->get_cell(target.inst->module_name);
        if (!fanout_cell)
          continue;
        const auto *input_pin = fanout_cell->get_pin(target.port_name);
        if (!input_pin)
          continue;
        const bool use_max = (get_analysis_mode() == AnalysisMode::MAX);

        pt.rise_cap += pin_rise_cap_for_analysis(*input_pin, use_max);
        pt.fall_cap += pin_fall_cap_for_analysis(*input_pin, use_max);
        accumulate_pin_corner_caps(*input_pin, pt);
      }
    }
    for (const TimingEdge &e : pt.fanouts) {
      if (visited.count(e.target_point) == 0)
        queue.push_back(e.target_point);
    }
  }
}

} // namespace sta
