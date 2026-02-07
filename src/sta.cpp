#include "cell/cell_data_structure.hpp"
#include "sta/sta_data_structures.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <unordered_set>
#include <vector>

using namespace verilog;

namespace sta {
SignalSpec STAWorker::get_signal_bits(const std::string &signame) const {
  auto it = signal_registry.find(signame);
  if (it != signal_registry.end()) {
    return it->second;
  }

  assert(false && "trans wire name to spec, find a unknown wire");
  return {};
}

bool STAWorker::is_reg(std::string name) {
  // 遍历 instances，查找是否有对应的 REG 实例
  // REG 实例的命名规则是 "reg_" + name
  std::string expected_instance_name = "reg_" + name;
  for (const auto &instance : instances) {
    if (instance->module_name == "REG" &&
        instance->instance_name == expected_instance_name) {
      return true;
    }
  }
  return false;
}

// Liberty LUT 返回 ns，STA 内部统一用 ps
static constexpr double NS_TO_PS = 1000.0;

// 辅助函数：从查找表中获取悲观值（最大值），返回 ps
double get_pessimistic_delay_from_lut(const celllib::LookupTable &lut) {
  double max_delay_ns = 0.0;
  for (const auto &row : lut.values) {
    for (double val : row) {
      if (val > max_delay_ns) {
        max_delay_ns = val;
      }
    }
  }
  return max_delay_ns * NS_TO_PS;
}

SignalBit *STAWorker::get_virtual_clock() {
  static SignalBit global_clk("__clk__", 0);
  return &global_clk;
}

void STAWorker::build_fanouts() {
  if (!cell_library_) {
    assert(false && "CellLibrary is required, no hardcoded fallback");
    return;
  }

  // bit -> driver point id（用于建立 WIRE 边）
  std::unordered_map<SignalBit, std::size_t, SignalBitHash> bit_to_driver;

  // 1. 预填 bit_to_driver：来自 collect_port 的 INPUT points
  for (std::size_t pt_id : input_clk_point_ids) {
    if (pt_id < res.points.size() && res.points[pt_id].bit.has_value()) {
      bit_to_driver[res.points[pt_id].bit.value()] = pt_id;
    }
  }

  // 2. 确保虚拟时钟 CLK point 存在
  SignalBit virtual_clk = *get_virtual_clock();
  std::size_t virtual_clk_pt =
      get_or_create_point(nullptr, "__clk__", virtual_clk, CLK);
  if (std::find(input_clk_point_ids.begin(), input_clk_point_ids.end(),
                virtual_clk_pt) == input_clk_point_ids.end()) {
    input_clk_point_ids.push_back(virtual_clk_pt);
  }
  bit_to_driver[virtual_clk] = virtual_clk_pt;

  // 第一遍：创建所有 point，填充 bit_to_driver
  struct PendingEdge {
    std::size_t from_pt, to_pt;
    EdgeType type;
  };
  std::vector<PendingEdge> pending_edges;

  for (auto &instance : instances) {
    const auto *cell = cell_library_->get_cell(instance->module_name);
    if (!cell) {
      std::cerr << "Warning: Cannot find cell '" << instance->module_name
                << "' in CellLibrary, skipping." << std::endl;
      continue;
    }

    bool is_sequential = cell->ff.has_value();

    if (is_sequential) {
      const auto &ff_def = cell->ff.value();
      std::string clock_pin_name = ff_def.clocked_on.value_or("CK");

      SignalBit clock_canonical = *get_virtual_clock();
      if (instance->connections.count(clock_pin_name)) {
        SignalSpec clock_signals = instance->connections[clock_pin_name];
        if (!clock_signals.empty()) {
          clock_canonical = sigmap.find(clock_signals[0]);
        }
      }

      std::size_t clk_pt;
      if (clock_canonical.wire_name == "__clk__") {
        clk_pt = virtual_clk_pt;
      } else {
        auto it = bit_to_driver.find(clock_canonical);
        if (it != bit_to_driver.end()) {
          clk_pt = it->second;
          // 将连接到 FF 时钟端的 input 标为 CLK，便于 path group 归为 REG2REG
          if (clk_pt < res.points.size())
            res.points[clk_pt].type = CLK;
        } else {
          clk_pt = get_or_create_point(nullptr, clock_canonical.wire_name,
                                       clock_canonical, CLK);
          bit_to_driver[clock_canonical] = clk_pt;
          // 确保从该 CLK 起点做 DFS，才能产生 reg2reg（CLK→REGQ→…→REGD）路径
          if (std::find(input_clk_point_ids.begin(), input_clk_point_ids.end(),
                       clk_pt) == input_clk_point_ids.end()) {
            input_clk_point_ids.push_back(clk_pt);
          }
        }
      }

      for (const auto &output_pin_name : cell->get_output_pins()) {
        const auto *output_pin = cell->get_pin(output_pin_name);
        if (!output_pin || !instance->connections.count(output_pin_name))
          continue;
        SignalSpec output_signals = instance->connections[output_pin_name];

        for (const auto &arc : output_pin->timing_arcs) {
          if ((arc.timing_type == celllib::TimingType::RISING_EDGE ||
               arc.timing_type == celllib::TimingType::FALLING_EDGE) &&
              arc.related_pin == clock_pin_name) {
            if (analysis_granularity_ != AnalysisGranularity::COARSE) {
              get_or_create_candidate_node(clk_pt);
            }
            for (size_t i = 0; i < output_signals.size(); ++i) {
              SignalBit output_canonical = sigmap.find(output_signals[i]);
              std::size_t regq_pt = get_or_create_point(
                  instance.get(), output_pin_name, output_canonical, REGQ);
              pending_edges.push_back({clk_pt, regq_pt, SEQ_ARC});
              bit_to_driver[output_canonical] = regq_pt;
            }
            break;
          }
        }
      }

      for (const auto &input_pin_name : cell->get_input_pins()) {
        if (input_pin_name == clock_pin_name)
          continue;
        if (!instance->connections.count(input_pin_name))
          continue;
        SignalSpec input_signals = instance->connections[input_pin_name];
        for (size_t i = 0; i < input_signals.size(); ++i) {
          SignalBit input_canonical = sigmap.find(input_signals[i]);
          std::size_t regd_pt = get_or_create_point(
              instance.get(), input_pin_name, input_canonical, REGD);
          auto it = bit_to_driver.find(input_canonical);
          if (it != bit_to_driver.end()) {
            pending_edges.push_back({it->second, regd_pt, WIRE});
          }
        }
      }
    } else {
      for (const auto &output_pin_name : cell->get_output_pins()) {
        const auto *output_pin = cell->get_pin(output_pin_name);
        if (!output_pin || !instance->connections.count(output_pin_name))
          continue;
        SignalSpec output_signals = instance->connections[output_pin_name];

        for (const auto &arc : output_pin->timing_arcs) {
          if (arc.timing_type != celllib::TimingType::COMBINATIONAL)
            continue;
          std::string input_pin_name = arc.related_pin;
          if (!instance->connections.count(input_pin_name))
            continue;
          SignalSpec input_signals = instance->connections[input_pin_name];

          for (size_t i = 0;
               i < input_signals.size() && i < output_signals.size(); ++i) {
            SignalBit input_canonical = sigmap.find(input_signals[i]);
            SignalBit output_canonical = sigmap.find(output_signals[i]);

            std::size_t input_pt = get_or_create_point(
                instance.get(), input_pin_name, input_canonical, COMB_PIN);
            std::size_t output_pt = get_or_create_point(
                instance.get(), output_pin_name, output_canonical, COMB_PIN);

            if (analysis_granularity_ != AnalysisGranularity::COARSE &&
                arc.timing_sense == celllib::TimingSense::NON_UNATE) {
              get_or_create_candidate_node(output_pt);
            }

            pending_edges.push_back({input_pt, output_pt, COMB_ARC});
            auto it = bit_to_driver.find(input_canonical);
            if (it != bit_to_driver.end()) {
              pending_edges.push_back({it->second, input_pt, WIRE});
            }
            bit_to_driver[output_canonical] = output_pt;
        }
      }
    }
  }
}

  // 2.5. 补充 REGD 的 WIRE 边（实例处理顺序可能导致 comb 在 seq 之后，首遍时 bit_to_driver 尚未就绪）
  auto pending_has = [&pending_edges](size_t from_pt, size_t to_pt) {
    for (const auto &e : pending_edges)
      if (e.from_pt == from_pt && e.to_pt == to_pt) return true;
    return false;
  };
  for (auto &instance : instances) {
    const auto *cell = cell_library_->get_cell(instance->module_name);
    if (!cell || !cell->ff.has_value())
      continue;
    const auto &ff_def = cell->ff.value();
    std::string clock_pin_name = ff_def.clocked_on.value_or("CK");
    for (const auto &input_pin_name : cell->get_input_pins()) {
      if (input_pin_name == clock_pin_name || !instance->connections.count(input_pin_name))
        continue;
      SignalSpec input_signals = instance->connections[input_pin_name];
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
          if (!pending_has(from_pt, regd_pt))
            pending_edges.push_back({from_pt, regd_pt, WIRE});
        }
      }
    }
  }

  // 3. 添加 WIRE 边：驱动顶层 OUTPUT 端口的 net 的 driver -> OUTPUT point
  for (const auto &pt : res.points) {
    if (pt.type != OUTPUT || !pt.bit.has_value())
      continue;
    const SignalBit &out_bit = pt.bit.value();
    auto it = bit_to_driver.find(out_bit);
    if (it != bit_to_driver.end())
      pending_edges.push_back({it->second, pt.id, WIRE});
  }

  // 第二遍：应用所有 pending edges（同一 (from_pt, to_pt) 只保留一条，避免 candidate DFS 产生重复 path）
  for (const auto &e : pending_edges) {
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

void STAWorker::build_res_edges() {
  res.edges.clear();
  for (std::size_t i = 0; i < res.points.size(); ++i) {
    for (const TimingEdge &e : res.points[i].fanouts) {
      res.edges.push_back(
          TimingEdge{e.type, i, e.target_point});
    }
  }
}

void STAWorker::calculate_load_capacitance() {
  assert(cell_library_);

  // 初始化每个 instance 的 output pin 负载为 0
  for (auto &instance : instances) {
    const auto *cell = cell_library_->get_cell(instance->module_name);
    if (!cell) {
      std::cerr << "could not find the standard cell " << instance->module_name
                << std::endl;
      assert(false);
    }
    for (const auto &out_pin : cell->get_output_pins()) {
      instance->load_capacitance[out_pin] = 0.0;
    }
  }

  // 从 input_clk_point_ids 出发，拓扑遍历
  std::deque<std::size_t> queue(input_clk_point_ids.begin(),
                                input_clk_point_ids.end());
  std::unordered_set<std::size_t> visited;

  while (!queue.empty()) {
    std::size_t pt_id = queue.front();
    queue.pop_front();
    if (visited.count(pt_id))
      continue;
    visited.insert(pt_id);

    const TimingPointRef &pt = res.points[pt_id];

    // 若为 cell 输出点（COMB_PIN 或 REGQ），将该输出端口的所有 fanout 的 input pin 电容累加
    if (pt.inst && (pt.type == COMB_PIN || pt.type == REGQ)) {
      for (const TimingEdge &e : pt.fanouts) {
        const TimingPointRef &target = res.points[e.target_point];
        if (!target.inst)
          continue; // 顶层端口，无电容
        const auto *fanout_cell =
            cell_library_->get_cell(target.inst->module_name);
        if (!fanout_cell) {
          std::cerr << "invalid cell " << target.inst->module_name << std::endl;
          assert(false);
        }
        const auto *input_pin = fanout_cell->get_pin(target.port_name);
        if (input_pin && input_pin->capacitance.has_value()) {
          pt.inst->load_capacitance[pt.port_name] +=
              input_pin->capacitance.value();
        }
      }
    }

    for (const TimingEdge &e : pt.fanouts) {
      if (visited.count(e.target_point) == 0) {
        queue.push_back(e.target_point);
      }
    }
  }
}

void STAWorker::run_timing_analysis_dfs() {
  assert(cell_library_);
  res.paths.clear();

  if (analysis_granularity_ == AnalysisGranularity::FINE) {
    assert(false && "FINE mode uses candidate path, not run_timing_analysis_dfs");
  }

  const double clk_slew_ns =
      std::max(static_cast<double>(cfg.clock_transit_raise),
               static_cast<double>(cfg.clock_transit_fall)) /
      NS_TO_PS;

  struct PathFrame {
    std::size_t point_id;
    double arrival;
    double slew_rise_ns;
    double slew_fall_ns;
    TransitionDirection dir;
    std::size_t fanout_idx;
  };
  struct StackFrame {
    std::size_t point_id;
    double arrival;
    double slew_rise_ns;
    double slew_fall_ns;
    TransitionDirection dir;
    std::size_t fanout_idx;
  };

  auto find_arc = [this](const TimingPointRef &origin,
                         const TimingPointRef &target,
                         EdgeType edge_type) -> const celllib::TimingArc * {
    if (!target.inst)
      return nullptr;
    const auto *cell = cell_library_->get_cell(target.inst->module_name);
    if (!cell)
      return nullptr;
    const auto *out_pin = cell->get_pin(target.port_name);
    if (!out_pin)
      return nullptr;

    if (edge_type == COMB_ARC) {
      for (const auto &a : out_pin->timing_arcs) {
        if (a.timing_type == celllib::TimingType::COMBINATIONAL &&
            a.related_pin == origin.port_name)
          return &a;
      }
    } else if (edge_type == SEQ_ARC) {
      std::string clk_pin = "CK";
      if (cell->ff.has_value() && cell->ff->clocked_on.has_value())
        clk_pin = cell->ff->clocked_on.value();
      for (const auto &a : out_pin->timing_arcs) {
        if ((a.timing_type == celllib::TimingType::RISING_EDGE ||
             a.timing_type == celllib::TimingType::FALLING_EDGE) &&
            a.related_pin == clk_pin)
          return &a;
      }
    }
    return nullptr;
  };

  auto compute_setup_hold = [this, clk_slew_ns](Instance *sink, const std::string &port,
                                   double data_arrival_ps,
                                   double data_slew_rise_ns,
                                   double data_slew_fall_ns,
                                   TransitionDirection data_dir)
      -> std::pair<double, double> {
    if (!sink || !cell_library_)
      return {0, 0};
    const auto *cell = cell_library_->get_cell(sink->module_name);
    if (!cell)
      return {0, 0};
    const auto *pin = cell->get_pin(port);
    if (!pin)
      return {0, 0};
    double data_trans =
        std::max(data_slew_rise_ns, data_slew_fall_ns);
    double setup_ps = 0, hold_ps = 0;
    for (const auto &arc : pin->timing_arcs) {
      if (arc.timing_type == celllib::TimingType::SETUP_RISING ||
          arc.timing_type == celllib::TimingType::SETUP_FALLING) {
        double sr = caculate_setup_rise(arc, cell_library_, data_trans, clk_slew_ns);
        double sf = caculate_setup_fall(arc, cell_library_, data_trans, clk_slew_ns);
        if (data_dir == TransitionDirection::RISING)
          setup_ps = sr * NS_TO_PS;
        else if (data_dir == TransitionDirection::FALLING)
          setup_ps = sf * NS_TO_PS;
        else
          setup_ps = std::max(sr, sf) * NS_TO_PS;
      }
      if (arc.timing_type == celllib::TimingType::HOLD_RISING ||
          arc.timing_type == celllib::TimingType::HOLD_FALLING) {
        double hr = caculate_hold_rise(arc, cell_library_, data_trans, clk_slew_ns);
        double hf = caculate_hold_fall(arc, cell_library_, data_trans, clk_slew_ns);
        if (data_dir == TransitionDirection::RISING)
          hold_ps = hr * NS_TO_PS;
        else if (data_dir == TransitionDirection::FALLING)
          hold_ps = hf * NS_TO_PS;
        else
          hold_ps = std::max(hr, hf) * NS_TO_PS;
      }
    }
    return {setup_ps, hold_ps};
  };

  int path_count = 0;
  std::unordered_set<std::string> path_printed;

  for (std::size_t start_id : input_clk_point_ids) {
    if (start_id >= res.points.size())
      continue;

    double slew_ns = clk_slew_ns;

    std::stack<StackFrame> stk;
    std::vector<PathFrame> path;
    stk.push({start_id, 0.0, slew_ns, slew_ns, TransitionDirection::UNKNOWN, 0});

    while (!stk.empty()) {
      StackFrame f = stk.top();
      stk.pop();

      if (f.fanout_idx == 0) {
        path.push_back({f.point_id, f.arrival, f.slew_rise_ns, f.slew_fall_ns,
                        f.dir, 0});
      }

      const TimingPointRef &cur = res.points[f.point_id];

      if (cur.type == REGD || cur.type == OUTPUT) {
        std::string fp;
        for (const auto &pf : path)
          fp += std::to_string(pf.point_id) + ":" + std::to_string(pf.arrival) + "->";
        if (path_printed.insert(fp).second) {
          path_count++;
          TimingPathResult pr;
          pr.startpoint = path.front().point_id;
          pr.endpoint = path.back().point_id;
          pr.data_arrival_time = f.arrival;
          pr.group = classify_path_group(
              effective_start_type_for_group(res.points[pr.startpoint]),
              cur.type);

          if (cur.type == REGD && cur.inst) {
            auto [setup_ps, hold_ps] = compute_setup_hold(
                cur.inst, cur.port_name, f.arrival, f.slew_rise_ns, f.slew_fall_ns, f.dir);
            pr.library_setup_time = setup_ps;
            pr.library_hold_time = hold_ps;
          }

          for (size_t i = 0; i < path.size(); ++i) {
            if (i == 0) {
              continue;
            }
            double incr = path[i].arrival - path[i - 1].arrival;
            double slew =
                std::max(path[i].slew_rise_ns, path[i].slew_fall_ns) * NS_TO_PS;
            TimingStep step{path[i - 1].point_id, path[i].point_id, incr, slew,
                            path[i].arrival, path[i].dir};
            pr.steps.push_back(step);
          }
          res.paths.push_back(pr);

          std::cout << "  [DEBUG] Path #" << path_count << " pt" << pr.startpoint
                    << "->pt" << pr.endpoint << " arrival=" << f.arrival << "ps"
                    << ", steps=" << pr.steps.size() << "\n";
        }
      }

      if (f.fanout_idx >= cur.fanouts.size()) {
        path.pop_back();
        continue;
      }

      const TimingEdge &e = cur.fanouts[f.fanout_idx];
      stk.push({f.point_id, f.arrival, f.slew_rise_ns, f.slew_fall_ns, f.dir,
                f.fanout_idx + 1});

      const TimingPointRef &target = res.points[e.target_point];

      double delay_ps = 0.0;
      double out_slew_rise_ns = f.slew_rise_ns;
      double out_slew_fall_ns = f.slew_fall_ns;
      TransitionDirection out_dir = f.dir;

      if (e.type == WIRE) {
        delay_ps = 0.0;
        out_slew_rise_ns = f.slew_rise_ns;
        out_slew_fall_ns = f.slew_fall_ns;
        out_dir = f.dir;
      } else {
        const celllib::TimingArc *arc = find_arc(cur, target, e.type);
        if (!arc) {
          std::cerr << "  [DEBUG] arc not found: pt " << f.point_id << " -> "
                    << e.target_point << " type "
                    << (e.type == COMB_ARC ? "COMB_ARC" : "SEQ_ARC") << "\n";
          // 不 pop_back：target 尚未加入 path，cur 仍应保留在 path 中
        continue;
      }

        double load_cap = 0.0;
        if (target.inst &&
            target.inst->load_capacitance.count(target.port_name))
          load_cap = target.inst->load_capacitance.at(target.port_name);

        double in_slew_rise = f.slew_rise_ns;
        double in_slew_fall = f.slew_fall_ns;
        bool is_start = (cur.type == INPUT || cur.type == CLK);
        if (is_start && in_slew_rise == 0)
          in_slew_rise = clk_slew_ns;
        if (is_start && in_slew_fall == 0)
          in_slew_fall = clk_slew_ns;

        bool is_ck2q = (e.type == SEQ_ARC);

        if (analysis_granularity_ == AnalysisGranularity::COARSE) {
          delay_ps = get_pessimistic_delay_from_lut(
              arc->cell_rise.has_value() ? arc->cell_rise.value()
                                         : celllib::LookupTable{});
          if (delay_ps == 0 && arc->cell_fall.has_value())
            delay_ps = get_pessimistic_delay_from_lut(arc->cell_fall.value());
          if (delay_ps == 0 && arc->intrinsic_rise.has_value())
            delay_ps = std::max(arc->intrinsic_rise.value_or(0),
                               arc->intrinsic_fall.value_or(0)) *
                      NS_TO_PS;
        } else {
          double dr = caculate_delay_rise(*arc, cell_library_, in_slew_rise, load_cap);
          double df = caculate_delay_fall(*arc, cell_library_, in_slew_fall, load_cap);
          out_slew_rise_ns =
              caculate_transition_rise(*arc, cell_library_, in_slew_rise, load_cap) /
              NS_TO_PS;
          out_slew_fall_ns =
              caculate_transition_fall(*arc, cell_library_, in_slew_fall, load_cap) /
              NS_TO_PS;
          out_dir = speculate_transition_direction(is_ck2q, *arc, f.dir);
          if (out_dir == TransitionDirection::RISING)
            delay_ps = dr;
          else if (out_dir == TransitionDirection::FALLING)
            delay_ps = df;
          else
            delay_ps = std::max(dr, df);

          std::string cell_name = target.inst ? target.inst->instance_name : "?";
          std::cout << "  [DEBUG] delay lut: " << cell_name << " "
                    << cur.port_name << "->" << target.port_name
                    << " slew_rise=" << in_slew_rise << " slew_fall=" << in_slew_fall
                    << " load_cap=" << load_cap << " delay_rise=" << dr
                    << " delay_fall=" << df << " -> " << delay_ps << "ps\n";
        }
      }

      double new_arrival = f.arrival + delay_ps;
      stk.push({e.target_point, new_arrival, out_slew_rise_ns, out_slew_fall_ns,
                out_dir, 0});
    }
  }

  std::cout << "  [DEBUG] run_timing_analysis_dfs: " << path_count
            << " paths found, res.paths.size()=" << res.paths.size() << "\n";
}
} 
