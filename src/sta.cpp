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
  point.port_name = port_name;
  point.bit = bit;
  point.type = type;
  point.fanouts = {};

  res.points.push_back(std::move(point));
  return id;
}

SignalBit *STAWorker::get_virtual_clock() {
  static SignalBit global_clk("__clk__", 0);
  return &global_clk;
}

static inline void check_cell_lib(const celllib::CellLibrary *lib) {
  if (!lib) {
    assert(false && "CellLibrary is required, no hardcoded fallback");
  }
  return;
}

void STAWorker::build_fanouts() {
  if (has_clock == false) {
    assert("must spec clock");
  }

  check_cell_lib(cell_library_);

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
      get_or_create_point(nullptr, "__clk__", virtual_clk, CLK_SOURCE);
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

      // 统一为每个寄存器创建一个“时钟端口”点 clk_pt（实例的 CLK pin），
      // 后续 SEQ_ARC 以及 path group
      // 都从该点出发，而不再直接从顶层时钟网驱动点出发。
      SignalBit clock_canonical = *get_virtual_clock();
      if (instance->connections.count(clock_pin_name)) {
        SignalSpec clock_signals = instance->connections[clock_pin_name];
        if (!clock_signals.empty()) {
          clock_canonical = sigmap.find(clock_signals[0]);
        }
      }
      // 为该寄存器实例的 CLK pin 创建/获取专用 TimingPointRef，类型标为 CLK
      std::size_t clk_pt = get_or_create_point(instance.get(), clock_pin_name,
                                               clock_canonical, CLK_PIN);
      if (clk_pt < res.points.size())
        res.points[clk_pt].type = CLK_PIN;
      input_clk_point_ids.push_back(clk_pt);

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
              // SEQ_ARC：从该寄存器的 CLK pin 到 Q 输出
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

            if (arc.timing_sense == celllib::TimingSense::NON_UNATE) {
              std::size_t input_node_id =
                  get_or_create_candidate_node(input_pt);
              std::size_t output_node_id =
                  get_or_create_candidate_node(output_pt);

              if (input_node_id != SIZE_MAX && output_node_id != SIZE_MAX &&
                  input_node_id < candidate_graphy_.nodes.size() &&
                  output_node_id < candidate_graphy_.nodes.size())
                candidate_graphy_.nodes[input_node_id]
                    .relate_candidate_point.push_back(output_node_id);

              res.points[input_pt].candidate_idx = input_node_id;
              res.points[output_pt].candidate_idx = output_node_id;
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

  // 2.5. 补充 REGD 的 WIRE 边（实例处理顺序可能导致 comb 在 seq 之后，首遍时
  // bit_to_driver 尚未就绪）
  auto pending_has = [&pending_edges](size_t from_pt, size_t to_pt) {
    for (const auto &e : pending_edges)
      if (e.from_pt == from_pt && e.to_pt == to_pt)
        return true;
    return false;
  };
  // 2.5b. 补充 REGQ（及任意 driver）到下游的 WIRE 边：若下游点先于 driver
  // 被创建，
  //       首遍时 bit_to_driver 尚无该 net，会漏掉 driver->consumer，这里按 bit
  //       统一补上
  for (const auto &pt : res.points) {
    if (!pt.bit.has_value())
      continue;
    auto it = bit_to_driver.find(pt.bit.value());
    if (it == bit_to_driver.end() || it->second == pt.id)
      continue;
    std::size_t driver_pt = it->second;
    if (!pending_has(driver_pt, pt.id))
      pending_edges.push_back({driver_pt, pt.id, WIRE});
  }

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
  // 注意：pt.bit 在 collect_port 时设置，可能在 collect_assign 之前，故需用
  // sigmap.find() 获取合并后的当前 canonical 才能与 bit_to_driver 的 key 匹配
  for (const auto &pt : res.points) {
    if (pt.type != OUTPUT || !pt.bit.has_value())
      continue;
    SignalBit out_bit = sigmap.find(pt.bit.value());
    auto it = bit_to_driver.find(out_bit);
    if (it != bit_to_driver.end())
      pending_edges.push_back({it->second, pt.id, WIRE});
  }

  // 第二遍：应用所有 pending edges（同一 (from_pt, to_pt) 只保留一条，避免
  // candidate DFS 产生重复 path）
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

  build_res_edges();
}

void STAWorker::build_res_edges() {
  res.edges.clear();
  for (std::size_t i = 0; i < res.points.size(); ++i) {
    for (const TimingEdge &e : res.points[i].fanouts) {
      res.edges.push_back(TimingEdge{e.type, i, e.target_point});
    }
  }
}

void STAWorker::caculate_load_cap() {
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

        if (use_max) {
          if (input_pin->rise_capacitance_max.has_value())
            pt.rise_cap += input_pin->rise_capacitance_max.value();
          else if (input_pin->capacitance.has_value())
            pt.rise_cap += input_pin->capacitance.value();
        } else {
          // min 模式：优先 min，若 min 无值则用 max
          if (input_pin->rise_capacitance_min.has_value())
            pt.rise_cap += input_pin->rise_capacitance_min.value();
          else if (input_pin->rise_capacitance_max.has_value())
            pt.rise_cap += input_pin->rise_capacitance_max.value();
          else if (input_pin->capacitance.has_value())
            pt.rise_cap += input_pin->capacitance.value();
        }

        if (use_max) {
          if (input_pin->fall_capacitance_max.has_value())
            pt.fall_cap += input_pin->fall_capacitance_max.value();
          else if (input_pin->capacitance.has_value())
            pt.fall_cap += input_pin->capacitance.value();
        } else {
          if (input_pin->fall_capacitance_min.has_value())
            pt.fall_cap += input_pin->fall_capacitance_min.value();
          else if (input_pin->fall_capacitance_max.has_value())
            pt.fall_cap += input_pin->fall_capacitance_max.value();
          else if (input_pin->capacitance.has_value())
            pt.fall_cap += input_pin->capacitance.value();
        }
      }
    }
    for (const TimingEdge &e : pt.fanouts) {
      if (visited.count(e.target_point) == 0)
        queue.push_back(e.target_point);
    }
  }
}
} // namespace sta
