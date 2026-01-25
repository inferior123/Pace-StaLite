#include <cstdlib>
#include <iostream>
#include <set>
#include <vector>

#include "sta_data_structures.hpp"
#include "verilog_driver.hpp"

sta::STAWorker worker;

// Define your own parser by inheriting the ParserVerilogInterface
struct MyVerilogParser : public verilog::ParserVerilogInterface {

  virtual ~MyVerilogParser(){}

  // Function that will be called when encountering the top module name.
  void add_module(std::string&& name){
    worker.top_moudle = name;
  }

  // Function that will be called when encountering a port.
  void add_port(verilog::Port&& port) {
    worker.collect_port(port);
  }  

  // Function that will be called when encountering a net.
  void add_net(verilog::Net&& net) {
    worker.collect_net(net);    
  }  

  // Function that will be called when encountering a assignment statement.
  void add_assignment(verilog::Assignment&& ast) {
    worker.collect_assign(ast);
  }  

  // Function that will be called when encountering a module instance.
  void add_instance(verilog::Instance&& inst) {
    worker.collect_instance(inst);
  }
};

struct SampleParser : public verilog::ParserVerilogInterface {

  virtual ~SampleParser(){}

  // Function that will be called when encountering the top module name.
  void add_module(std::string&& name){
    std::cout << "Module: " << name << '\n';
  }

  // Function that will be called when encountering a port.
  void add_port(verilog::Port&& port) {
    std::cout << "Port: " << port << '\n';
  }  

  // Function that will be called when encountering a net.
  void add_net(verilog::Net&& net) {
    std::cout << "Net: " << net << '\n';
  }  

  // Function that will be called when encountering a assignment statement.
  void add_assignment(verilog::Assignment&& ast) {
    std::cout << "Assignment: " << ast << '\n';
  }  

  // Function that will be called when encountering a module instance.
  void add_instance(verilog::Instance&& inst) {
    std::cout << "Instance: " << inst << '\n';
  }
};

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
    if (bit.bit_offset != 0 || signal_registry.at(bit.wire_name).size() > 1) {
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
    if (bit.bit_offset != 0 || signal_registry.at(bit.wire_name).size() > 1) {
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


int main(){
  MyVerilogParser parser;
  parser.read("./Testing/reg.v");
  worker.build_fanouts();

  fanout_debuger(worker);

  return EXIT_SUCCESS;
}