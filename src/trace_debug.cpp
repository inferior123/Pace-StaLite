#include "sta/sta_data_structures.hpp"
#include "parser-verilog/verilog_data.hpp"
#include <set>

#include "sta/debug.h"


void fanout_debuger(sta::STAWorker& worker) {
  std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
  std::cout << "║              Fanout Debug Information                    ║\n";
  std::cout << "╚══════════════════════════════════════════════════════════╝\n";
  
  std::cout << "\nTop Module: " << worker.top_moudle << "\n";
  std::cout << "----------------------------------------\n";

  // 打印端口信息
  std::cout << "\nPorts:\n";
  const auto& signal_registry = worker.get_signal_registry();
  const auto& endpoints = worker.get_endpoints();
  const auto& timing_queue = worker.get_timing_queue();
  
  // 收集输入端口：直接使用 timing_queue
  // timing_queue 中存储的就是 INPUT 端口（在 collect_port 时加入的）
  // 使用 set 来去重，因为多个信号可能映射到同一个规范代表
  std::set<sta::SignalBit> input_ports_set;
  for (const auto& bit : timing_queue) {
    sta::SignalBit canonical = worker.get_canonical_signal(bit);
    input_ports_set.insert(canonical);
  }
  
  // 收集输出端口：遍历 endpoints，使用规范代表去重
  std::set<sta::SignalBit> output_ports_set;
  for (const auto& [bit, endpoint] : endpoints) {
    sta::SignalBit canonical = worker.get_canonical_signal(bit);
    output_ports_set.insert(canonical);
  }
  
  // 转换为 vector 用于显示
  std::vector<sta::SignalBit> input_ports(input_ports_set.begin(), input_ports_set.end());
  std::vector<sta::SignalBit> output_ports(output_ports_set.begin(), output_ports_set.end());

  std::cout << "  INPUT ports:\n";
  for (const auto& bit : input_ports) {
    std::cout << "    " << bit.wire_name;
    // 检查是否需要显示索引：如果bit_offset不为0，或者signal_registry中存在且size>1
    bool need_index = (bit.bit_offset != 0);
    if (!need_index && signal_registry.count(bit.wire_name)) {
      need_index = (signal_registry.at(bit.wire_name).size() > 1);
    }
    if (need_index) {
      std::cout << "[" << bit.bit_offset << "]";
    }
    std::cout << "\n";
  }
  
  std::cout << "  OUTPUT ports:\n";
  for (const auto& endpoint : endpoints) {
    const auto &bit = endpoint.first;
    const auto &info = endpoint.second;
    if(info.sink != nullptr) {
      continue;
    }
    std::cout << "    " << bit.wire_name;
    // 检查是否需要显示索引：如果bit_offset不为0，或者signal_registry中存在且size>1
    bool need_index = (bit.bit_offset != 0);
    if (!need_index && signal_registry.count(bit.wire_name)) {
      need_index = (signal_registry.at(bit.wire_name).size() > 1);
    }
    if (need_index) {
      std::cout << "[" << bit.bit_offset << "]";
    }
    std::cout << "\n";
  }
  
  std::cout << "\n----------------------------------------\n";
  std::cout << "Endpoints:\n";
  std::cout << "----------------------------------------\n";
  for (const auto &endpoint : endpoints) {
    std::cout << "    " << endpoint.first.wire_name << std::endl;
  }
  std::cout << '\n';

  // 打印连接图（fanout 关系）
  std::cout << "\n----------------------------------------\n";
  std::cout << "Connection Graph (Fanouts):\n";
  std::cout << "----------------------------------------\n";
  
  const auto& timing_data = worker.get_timing_data();
  const auto& instances = worker.get_instances();
  
  // 打印所有实例的连接
  std::cout << "\nInstances and their connections:\n";
  for (size_t i = 0; i < instances.size(); ++i) {
    const auto& instance = instances[i];
    std::cout << "\n  Instance #" << i << ": " << instance->instance_name 
              << " (type: " << instance->module_name << ")\n";
    
    for (const auto& [port_name, signals] : instance->connections) {
      std::cout << "    " << port_name << ": ";
      for (size_t j = 0; j < signals.size(); ++j) {
        const auto& bit = signals[j];
        sta::SignalBit canonical = worker.get_canonical_signal(bit);
        std::cout << canonical.wire_name << "[" << canonical.bit_offset << "]";
        if (j < signals.size() - 1) {
          std::cout << ", ";
        }
      }
      std::cout << "\n";
    }
  }
  
  // 打印 fanout 关系
  std::cout << "\nFanout relationships:\n";
  size_t total_fanouts = 0;
  for (const auto& [bit, timing] : timing_data) {
    if (!timing.fanouts.empty()) {
      sta::SignalBit canonical = worker.get_canonical_signal(bit);
      std::cout << "\n  Signal: " << canonical.wire_name << "[" << canonical.bit_offset << "]\n";
      std::cout << "    Fanouts (" << timing.fanouts.size() << "):\n";
      
      for (const auto& fanout : timing.fanouts) {
        sta::SignalBit target_canonical = worker.get_canonical_signal(fanout.target_bit);
        std::cout << "      -> " << target_canonical.wire_name 
                  << "[" << target_canonical.bit_offset << "] "
                  << "(delay: " << fanout.delay << "ps, "
                  << "port: " << fanout.port_name;
        if (fanout.cell) {
          std::cout << ", cell: " << fanout.cell->instance_name;
        }
        std::cout << ")\n";
        total_fanouts++;
      }
    }
  }
  
  std::cout << "\n----------------------------------------\n";
  std::cout << "Summary:\n";
  std::cout << "  Total instances: " << instances.size() << "\n";
  std::cout << "  Total signals with fanouts: " << timing_data.size() << "\n";
  std::cout << "  Total fanout arcs: " << total_fanouts << "\n";
  std::cout << "╔══════════════════════════════════════════════════════════╗\n";
  std::cout << "║              End of Debug Information                    ║\n";
  std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
}

void run_debuger(sta::STAWorker& worker, bool verbose) {
  std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
  std::cout << "║              Timing Propagation Trace                    ║\n";
  std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
  
  // 显示初始状态
  const auto& timing_queue = worker.get_timing_queue();
  const auto& arrival_time = worker.get_arrival_time();
  const auto& endpoints = worker.get_endpoints();
  
  std::cout << "Initial State:\n";
  std::cout << "----------------------------------------\n";
  std::cout << "Queue size: " << timing_queue.size() << "\n";
  if (timing_queue.size() > 0) {
    std::cout << "Signals in queue:\n";
    for (const auto& bit : timing_queue) {
      sta::SignalBit canonical = worker.get_canonical_signal(bit);
      std::cout << "  - " << canonical.wire_name << "[" << canonical.bit_offset << "]";
      if (arrival_time.count(canonical)) {
        std::cout << " (arrival: " << arrival_time.at(canonical) << "ps)";
      }
      std::cout << "\n";
    }
  }
  std::cout << "\n";
  
  // 执行时序传播（调用实际的 run() 函数）
  std::cout << "Executing timing propagation...\n";
  std::cout << "----------------------------------------\n";
  worker.run();
  std::cout << "✓ Timing propagation completed\n\n";
  
  // 显示最终结果
  const auto& final_arrival_time = worker.get_arrival_time();
  int max_arrival = worker.get_max_arrival_time();
  sta::SignalBit critical_sig = worker.get_critical_signal();
  
  std::cout << "----------------------------------------\n";
  std::cout << "Final Results:\n";
  std::cout << "----------------------------------------\n";
  std::cout << "Max arrival time: " << max_arrival << "ps\n";
  if (critical_sig.wire_name != "") {
    std::cout << "Critical signal: " << critical_sig.wire_name 
              << "[" << critical_sig.bit_offset << "]\n";
    
    // 显示关键路径信息
    if (final_arrival_time.count(critical_sig)) {
      int critical_arrival = final_arrival_time.at(critical_sig);
      std::cout << "Critical signal arrival time: " << critical_arrival << "ps\n";
      if (endpoints.count(critical_sig)) {
        int required = endpoints.at(critical_sig).required_time;
        std::cout << "Required time: " << required << "ps\n";
        std::cout << "Total time: " << (critical_arrival + required) << "ps\n";
      }
    }
  }
  
  if (verbose) {
    std::cout << "\nAll arrival times:\n";
    std::cout << "----------------------------------------\n";
    for (const auto& [bit, time] : final_arrival_time) {
      if (time >= 0) {
        std::cout << "  " << bit.wire_name << "[" << bit.bit_offset << "]: " 
                  << time << "ps";
        
        // 显示是否是 endpoint
        if (endpoints.count(bit)) {
          int required = endpoints.at(bit).required_time;
          std::cout << " [endpoint, required: " << required << "ps, total: " 
                    << (time + required) << "ps]";
        }
        std::cout << "\n";
      }
    }
  }
  
  std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
  std::cout << "║              End of Timing Propagation Trace              ║\n";
  std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
}
