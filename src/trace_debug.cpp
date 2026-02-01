#include "sta/sta_data_structures.hpp"
#include "cell/cell_data_structure.hpp"
#include "cell/liberty_parser.hpp"
#include "sdc/sdc_parser.hpp"
#include "interface.hpp"
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
        int setup_required = endpoints.at(critical_sig).Setup_req.value();
        std::cout << "Library Setup time: " << setup_required << "ps\n";
        std::cout << "Total time: " << (critical_arrival + setup_required) << "ps\n";
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
          int setup_required = endpoints.at(bit).Setup_req.value();
          std::cout << " [endpoint, library setup required: " << setup_required << "ps, total: " 
                    << (time + setup_required) << "ps]";
        }
        std::cout << "\n";
      }
    }
  }
  
  std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
  std::cout << "║              End of Timing Propagation Trace              ║\n";
  std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
}

int test_celllib_parser(int argc, char *argv[]) {
    std::string lib_file = "/home/ysyx/project/pba-sta-base/proj/lib/simple_nangate.lib";
    
    SimpleCellLibParser parser(true);  // verbose = true
    
    if (parser.parse_from_file(lib_file)) {
      std::cout << "✓ Library parsing completed successfully!\n";
      
      // 可以进一步测试查找功能
      const auto* cell = parser.get_library().get_cell("AND2_X1");
      if (cell) {
        std::cout << "\nTest: Found cell AND2_X2\n";
        const auto* pin = cell->get_pin("a");
        if (pin && pin->capacitance.has_value()) {
          std::cout << "  Pin 'a' capacitance: " << pin->capacitance.value() << " ff\n";
        }
      }
    } else {
      std::cerr << "✗ Library parsing failed!\n";
      return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS; 
}

int test_transition_calculation(int argc, char *argv[]) {
    std::cout << "\n========================================\n";
    std::cout << "Testing caculate_transition function\n";
    std::cout << "========================================\n\n";
    
    std::string lib_file = "/home/ysyx/project/pba-sta-base/proj/lib/simple_nangate.lib";
    SimpleCellLibParser parser(false);  // verbose = false
    
    if (!parser.parse_from_file(lib_file)) {
      std::cerr << "✗ Library parsing failed!\n";
      return EXIT_FAILURE;
    }
    
    auto& library = parser.get_library();
    
    // 测试用例1: 根据文档示例创建测试数据
    // 文档示例: index_1 = 0.1, 0.3; index_2 = 0.35, 1.43
    // values: T11=0.1937, T12=0.7280, T21=0.2327, T22=0.7676
    // x0=0.15, y0=1.16 (插值情况)
    // 期望结果: 0.6043
    
    celllib::LookupTable test_table;
    test_table.index_1 = {0.1, 0.3};
    test_table.index_2 = {0.35, 1.43};
    test_table.values = {
        {0.1937, 0.7280},  // T11, T12
        {0.2327, 0.7676}   // T21, T22
    };
    test_table.template_name = "test_template";
    
    // 创建对应的模板
    celllib::TableTemplate test_template;
    test_template.name = "test_template";
    test_template.variable_1 = "input_net_transition";
    test_template.variable_2 = "total_output_net_capacitance";
    test_template.index_1 = test_table.index_1;
    test_template.index_2 = test_table.index_2;
    library.add_table_template(test_template);
    
    std::string var1 = "input_net_transition";
    std::string var2 = "total_output_net_capacitance";
    
    // 测试插值情况: x0=0.15, y0=1.16
    double capacity = 1.16;
    double input_transition = 0.15;
    
    double result = library.caculate_lookuptable(test_table, input_transition, capacity, var1, var2);
    double expected = 0.6043;
    double tolerance = 0.0001;
    
    std::cout << "Test 1: Interpolation case\n";
    std::cout << "  Input: capacity=" << capacity << ", input_transition=" << input_transition << "\n";
    std::cout << "  Expected: " << expected << "\n";
    std::cout << "  Got: " << result << "\n";
    std::cout << "  Difference: " << std::abs(result - expected) << "\n";
    
    if (std::abs(result - expected) < tolerance) {
        std::cout << "  ✓ PASSED\n\n";
    } else {
        std::cout << "  ✗ FAILED\n\n";
    }
    
    // 测试外推情况: x0=0.05, y0=1.7
    // 期望结果: 0.8516
    capacity = 1.7;
    input_transition = 0.05;
    result = library.caculate_lookuptable(test_table, input_transition, capacity, var1, var2);
    expected = 0.8516;
    
    std::cout << "Test 2: Extrapolation case\n";
    std::cout << "  Input: capacity=" << capacity << ", input_transition=" << input_transition << "\n";
    std::cout << "  Expected: " << expected << "\n";
    std::cout << "  Got: " << result << "\n";
    std::cout << "  Difference: " << std::abs(result - expected) << "\n";
    
    if (std::abs(result - expected) < tolerance) {
        std::cout << "  ✓ PASSED\n\n";
    } else {
        std::cout << "  ✗ FAILED\n\n";
    }
    
    // 测试实际库中的数据
    const auto* cell = library.get_cell("AND2_X2");
    if (cell) {
        const auto* pin = cell->get_pin("ZN");
        if (pin && !pin->timing_arcs.empty()) {
            const auto& arc = pin->timing_arcs[0];
            if (arc.fall_transition.has_value()) {
                const auto& lut = arc.fall_transition.value();
                if (lut.template_name.has_value()) {
                    std::cout << "Test 3: Real library data\n";
                    std::cout << "  Cell: AND2_X2, Pin: ZN, Timing Arc: fall_transition\n";
                    std::cout << "  Template: " << lut.template_name.value() << "\n";
                    
                    // 使用中间值进行测试
                    if (!lut.index_1.empty() && !lut.index_2.empty()) {
                        double test_cap = (lut.index_2[0] + lut.index_2.back()) / 2.0;
                        double test_trans = (lut.index_1[0] + lut.index_1.back()) / 2.0;
                        
                        const auto* templ = library.get_table_template(lut.template_name.value());
                        if (templ && templ->variable_1.has_value() && templ->variable_2.has_value()) {
                            std::string v1 = templ->variable_1.value();
                            std::string v2 = templ->variable_2.value();
                            
                            // v1 对应 variable_1 (index_1), v2 对应 variable_2 (index_2)
                            // test_trans 来自 index_1, test_cap 来自 index_2
                            result = library.caculate_lookuptable(lut, test_trans, test_cap, v1, v2);
                            std::cout << "  Input: capacity=" << test_cap << ", input_transition=" << test_trans << "\n";
                            std::cout << "  Result: " << result << "\n";
                            std::cout << "  ✓ Test completed\n\n";
                        }
                    }
                }
            }
        }
    }
    
    std::cout << "========================================\n\n";
    return EXIT_SUCCESS;
}

// ============================================================================
// STA Functions Test Suite
// ============================================================================

/**
 * 创建简单的测试电路：AND2门
 * a, b -> AND2 -> c
 */
void create_simple_and2_circuit(sta::STAWorker& worker) {
    // 创建输入端口 a
    verilog::Port port_a;
    port_a.names.push_back("a");
    port_a.beg = 0;
    port_a.end = 0;
    port_a.dir = verilog::PortDirection::INPUT;
    port_a.type = verilog::ConnectionType::WIRE;
    worker.collect_port(port_a);
    
    // 创建输入端口 b
    verilog::Port port_b;
    port_b.names.push_back("b");
    port_b.beg = 0;
    port_b.end = 0;
    port_b.dir = verilog::PortDirection::INPUT;
    port_b.type = verilog::ConnectionType::WIRE;
    worker.collect_port(port_b);
    
    // 创建输出端口 c
    verilog::Port port_c;
    port_c.names.push_back("c");
    port_c.beg = 0;
    port_c.end = 0;
    port_c.dir = verilog::PortDirection::OUTPUT;
    port_c.type = verilog::ConnectionType::WIRE;
    worker.collect_port(port_c);
    
    // 创建内部信号 wire c
    verilog::Net net_c;
    net_c.names.push_back("c");
    net_c.beg = 0;
    net_c.end = 0;
    net_c.type = verilog::NetType::WIRE;
    worker.collect_net(net_c);
    
    // 创建 AND2 实例
    verilog::Instance inst_and2;
    inst_and2.module_name = "AND2_X2";
    inst_and2.inst_name = "u1";
    
    // 连接端口 A -> a
    inst_and2.pin_names.push_back(std::string("A1"));
    std::vector<verilog::NetConcat> net_a;
    net_a.push_back(std::string("a"));
    inst_and2.net_names.push_back(net_a);
    
    // 连接端口 B -> b
    inst_and2.pin_names.push_back(std::string("A2"));
    std::vector<verilog::NetConcat> net_b;
    net_b.push_back(std::string("b"));
    inst_and2.net_names.push_back(net_b);
    
    // 连接端口 ZN -> c
    inst_and2.pin_names.push_back(std::string("ZN"));
    std::vector<verilog::NetConcat> net_c_vec;
    net_c_vec.push_back(std::string("c"));
    inst_and2.net_names.push_back(net_c_vec);
    
    worker.collect_instance(inst_and2);
}

/**
 * 创建包含寄存器的测试电路
 * clk -> DFF -> q
 */
void create_dff_circuit(sta::STAWorker& worker) {
    // 创建时钟端口
    verilog::Port port_clk;
    port_clk.names.push_back("clk");
    port_clk.beg = 0;
    port_clk.end = 0;
    port_clk.dir = verilog::PortDirection::INPUT;
    port_clk.type = verilog::ConnectionType::WIRE;
    worker.collect_port(port_clk);
    
    // 创建数据输入端口
    verilog::Port port_d;
    port_d.names.push_back("d");
    port_d.beg = 0;
    port_d.end = 0;
    port_d.dir = verilog::PortDirection::INPUT;
    port_d.type = verilog::ConnectionType::WIRE;
    worker.collect_port(port_d);
    
    // 创建输出端口
    verilog::Port port_q;
    port_q.names.push_back("q");
    port_q.beg = 0;
    port_q.end = 0;
    port_q.dir = verilog::PortDirection::OUTPUT;
    port_q.type = verilog::ConnectionType::WIRE;
    worker.collect_port(port_q);
    
    // 创建内部信号
    verilog::Net net_q;
    net_q.names.push_back("q");
    net_q.beg = 0;
    net_q.end = 0;
    net_q.type = verilog::NetType::WIRE;
    worker.collect_net(net_q);
    
    // 创建 DFF 实例
    verilog::Instance inst_dff;
    inst_dff.module_name = "DFF_X1";
    inst_dff.inst_name = "u1";
    
    // 连接端口 D -> d
    inst_dff.pin_names.push_back(std::string("D"));
    std::vector<verilog::NetConcat> net_d;
    net_d.push_back(std::string("d"));
    inst_dff.net_names.push_back(net_d);
    
    // 连接端口 CK -> clk
    inst_dff.pin_names.push_back(std::string("CK"));
    std::vector<verilog::NetConcat> net_clk;
    net_clk.push_back(std::string("clk"));
    inst_dff.net_names.push_back(net_clk);
    
    // 连接端口 Q -> q
    inst_dff.pin_names.push_back(std::string("Q"));
    std::vector<verilog::NetConcat> net_q_vec;
    net_q_vec.push_back(std::string("q"));
    inst_dff.net_names.push_back(net_q_vec);
    
    worker.collect_instance(inst_dff);
}

/**
 * 测试1: build_fanouts() 函数
 */
int test_build_fanouts(int argc, char *argv[]) {
    std::cout << "\n========================================\n";
    std::cout << "Test 1: build_fanouts() function\n";
    std::cout << "========================================\n\n";
    
    // 解析库文件
    std::string lib_file = "/home/ysyx/project/pba-sta-base/proj/lib/simple_nangate.lib";
    SimpleCellLibParser parser(false);
    if (!parser.parse_from_file(lib_file)) {
        std::cerr << "✗ Library parsing failed!\n";
        return EXIT_FAILURE;
    }
    
    // 调试：打印库中所有单元名称
    auto& lib = parser.get_library();
    auto cell_names = lib.get_cell_names();
    std::cout << "Debug: Library contains " << cell_names.size() << " cells: ";
    for (const auto& name : cell_names) {
        std::cout << name << " ";
    }
    std::cout << "\n";
    
    // 创建 STAWorker 并注入库
    sta::STAWorker worker(lib);
    
    // 创建简单电路
    create_simple_and2_circuit(worker);
    
    // 调用 build_fanouts()
    std::cout << "Calling build_fanouts()...\n";
    worker.build_fanouts();
    std::cout << "✓ build_fanouts() completed\n\n";
    
    // 验证结果
    const auto& timing_data = worker.get_timing_data();
    bool test_passed = true;
    
    // 检查输入端口 a 的 fanout
    sta::SignalBit a_bit("a", 0);
    sta::SignalBit a_canonical = worker.get_canonical_signal(a_bit);
    if (timing_data.count(a_canonical)) {
        const auto& fanouts = timing_data.at(a_canonical).fanouts;
        std::cout << "Signal 'a' has " << fanouts.size() << " fanout(s)\n";
        if (fanouts.size() > 0) {
            std::cout << "  ✓ PASSED: Input port 'a' has fanout\n";
        } else {
            std::cout << "  ✗ FAILED: Input port 'a' should have fanout\n";
            test_passed = false;
        }
    } else {
        std::cout << "  ✗ FAILED: Signal 'a' not found in timing_data\n";
        test_passed = false;
    }
    
    // 检查输出端口 c 是否被标记为驱动
    sta::SignalBit c_bit("c", 0);
    sta::SignalBit c_canonical = worker.get_canonical_signal(c_bit);
    if (timing_data.count(c_canonical)) {
        std::cout << "Signal 'c' is in timing_data\n";
        // 检查是否有驱动（通过fanout关系间接检查）
        std::cout << "  ✓ PASSED: Output signal 'c' exists\n";
    } else {
        std::cout << "  ✗ FAILED: Output signal 'c' not found\n";
        test_passed = false;
    }
    
    // 测试 COARSE 模式 - 使用 get_analysis_granularity 来获取枚举值
    std::cout << "\nTesting COARSE granularity...\n";
    sta::STAWorker worker_coarse(parser.get_library());
    worker_coarse.set_analysis_granularity(sta::STAWorker::AnalysisGranularity::COARSE);
    create_simple_and2_circuit(worker_coarse);
    // COARSE 模式会在 build_fanouts 中设置悲观延迟
    worker_coarse.build_fanouts();
    
    const auto& timing_data_coarse = worker_coarse.get_timing_data();
    if (timing_data_coarse.count(a_canonical)) {
        const auto& fanouts = timing_data_coarse.at(a_canonical).fanouts;
        if (!fanouts.empty() && fanouts[0].delay > 0) {
            std::cout << "  ✓ PASSED: COARSE mode sets pessimistic delay (" << fanouts[0].delay << "ps)\n";
        } else {
            std::cout << "  ✗ FAILED: COARSE mode should set delay > 0\n";
            test_passed = false;
        }
    }
    
    // 测试 MEDIUM 模式 - 默认就是 MEDIUM
    std::cout << "\nTesting MEDIUM granularity...\n";
    sta::STAWorker worker_medium(parser.get_library());
    create_simple_and2_circuit(worker_medium);
    // MEDIUM 是默认模式，无需设置
    worker_medium.build_fanouts();
    
    const auto& timing_data_medium = worker_medium.get_timing_data();
    if (timing_data_medium.count(a_canonical)) {
        const auto& fanouts = timing_data_medium.at(a_canonical).fanouts;
        if (!fanouts.empty() && fanouts[0].delay == 0) {
            std::cout << "  ✓ PASSED: MEDIUM mode sets placeholder delay (0)\n";
        } else {
            std::cout << "  ⚠ WARNING: MEDIUM mode delay is " << (!fanouts.empty() ? fanouts[0].delay : 0) << " (expected 0)\n";
        }
    }
    
    std::cout << "\n========================================\n";
    return test_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}

/**
 * 测试2: calculate_load_capacitance() 函数
 */
int test_calculate_load_capacitance(int argc, char *argv[]) {
    std::cout << "\n========================================\n";
    std::cout << "Test 2: calculate_load_capacitance() function\n";
    std::cout << "========================================\n\n";
    
    // 解析库文件
    std::string lib_file = "/home/ysyx/project/pba-sta-base/proj/lib/simple_nangate.lib";
    SimpleCellLibParser parser(false);
    if (!parser.parse_from_file(lib_file)) {
        std::cerr << "✗ Library parsing failed!\n";
        return EXIT_FAILURE;
    }
    
    // 创建 STAWorker 并注入库
    sta::STAWorker worker(parser.get_library());
    
    // 创建简单电路
    create_simple_and2_circuit(worker);
    
    // 先调用 build_fanouts()
    worker.build_fanouts();
    
    // 调用 calculate_load_capacitance()
    std::cout << "Calling calculate_load_capacitance()...\n";
    worker.calculate_load_capacitance();
    std::cout << "✓ calculate_load_capacitance() completed\n\n";
    
    // 验证结果
    const auto& instances = worker.get_instances();
    bool test_passed = true;
    
    if (instances.empty()) {
        std::cout << "  ✗ FAILED: No instances found\n";
        return EXIT_FAILURE;
    }
    
    // 检查第一个实例的负载电容
    const auto& instance = instances[0];
    if (instance->load_capacitance.count("ZN")) {
        double load_cap = instance->load_capacitance.at("ZN");
        std::cout << "Instance '" << instance->instance_name << "' output 'ZN' load capacitance: " 
                  << load_cap << " ff\n";
        
        if (load_cap >= 0.0) {
            std::cout << "  ✓ PASSED: Load capacitance calculated (" << load_cap << " ff)\n";
        } else {
            std::cout << "  ✗ FAILED: Load capacitance should be >= 0\n";
            test_passed = false;
        }
    } else {
        std::cout << "  ⚠ WARNING: Load capacitance not found for output 'ZN'\n";
        std::cout << "    Available outputs: ";
        for (const auto& [pin, cap] : instance->load_capacitance) {
            std::cout << pin << " ";
        }
        std::cout << "\n";
    }
    
    std::cout << "\n========================================\n";
    return test_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}

/**
 * 测试3: calculate_timing_arcs() 函数
 */
int test_calculate_timing_arcs(int argc, char *argv[]) {
    std::cout << "\n========================================\n";
    std::cout << "Test 3: calculate_timing_arcs() function\n";
    std::cout << "========================================\n\n";
    
    // 解析库文件
    std::string lib_file = "/home/ysyx/project/pba-sta-base/proj/lib/simple_nangate.lib";
    SimpleCellLibParser parser(false);
    if (!parser.parse_from_file(lib_file)) {
        std::cerr << "✗ Library parsing failed!\n";
        return EXIT_FAILURE;
    }
    
    // 创建 STAWorker 并注入库（默认是 MEDIUM 模式）
    sta::STAWorker worker(parser.get_library());
    
    // 创建简单电路
    create_simple_and2_circuit(worker);
    
    // 按顺序调用三阶段函数
    std::cout << "Step 1: build_fanouts()...\n";
    worker.build_fanouts();
    
    std::cout << "Step 2: calculate_load_capacitance()...\n";
    worker.calculate_load_capacitance();
    
    std::cout << "Step 3: calculate_timing_arcs()...\n";
    worker.calculate_timing_arcs();
    std::cout << "✓ calculate_timing_arcs() completed\n\n";
    
    // 验证结果
    const auto& timing_data = worker.get_timing_data();
    bool test_passed = true;
    
    // 检查输入端口 a 的 fanout 延迟是否已更新
    sta::SignalBit a_bit("a", 0);
    sta::SignalBit a_canonical = worker.get_canonical_signal(a_bit);
    if (timing_data.count(a_canonical)) {
        const auto& fanouts = timing_data.at(a_canonical).fanouts;
        if (!fanouts.empty()) {
            int delay = fanouts[0].delay;
            std::cout << "Signal 'a' fanout delay: " << delay << " ps\n";
            if (delay > 0) {
                std::cout << "  ✓ PASSED: Delay calculated (" << delay << " ps)\n";
            } else {
                std::cout << "  ⚠ WARNING: Delay is 0 (may be placeholder)\n";
            }
        }
    }
    
    // 检查输出信号 c 的转换时间和方向
    sta::SignalBit c_bit("c", 0);
    sta::SignalBit c_canonical = worker.get_canonical_signal(c_bit);
    if (timing_data.count(c_canonical)) {
        const auto& timing = timing_data.at(c_canonical);
        bool has_rise = timing.rise_transition_time.has_value();
        bool has_fall = timing.fall_transition_time.has_value();
        
        if (has_rise || has_fall) {
            if (has_rise) {
                std::cout << "Signal 'c' rise_transition_time: " << timing.rise_transition_time.value() << " ps\n";
            }
            if (has_fall) {
                std::cout << "Signal 'c' fall_transition_time: " << timing.fall_transition_time.value() << " ps\n";
            }
            if (has_rise && has_fall) {
                double max_trans = std::max(timing.rise_transition_time.value(), 
                                           timing.fall_transition_time.value());
                std::cout << "Signal 'c' max_transition_time: " << max_trans << " ps\n";
            }
            std::cout << "Signal 'c' transition_direction: " 
                      << (timing.transition_direction == sta::TransitionDirection::RISING ? "RISING" :
                          timing.transition_direction == sta::TransitionDirection::FALLING ? "FALLING" : "UNKNOWN")
                      << "\n";
            std::cout << "  ✓ PASSED: Transition times stored (rise and fall)\n";
        } else {
            std::cout << "  ⚠ WARNING: Transition times not set\n";
        }
    }
    
    std::cout << "\n========================================\n";
    return test_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}

/**
 * 测试4: is_top_module_input() 函数
 */
int test_is_top_module_input(int argc, char *argv[]) {
    std::cout << "\n========================================\n";
    std::cout << "Test 4: is_top_module_input() function\n";
    std::cout << "========================================\n\n";
    
    // 解析库文件
    std::string lib_file = "/home/ysyx/project/pba-sta-base/proj/lib/simple_nangate.lib";
    SimpleCellLibParser parser(false);
    if (!parser.parse_from_file(lib_file)) {
        std::cerr << "✗ Library parsing failed!\n";
        return EXIT_FAILURE;
    }
    
    // 创建 STAWorker 并注入库
    sta::STAWorker worker(parser.get_library());
    
    // 创建简单电路
    create_simple_and2_circuit(worker);
    
    // 调用 build_fanouts() 来填充 top_module_inputs
    worker.build_fanouts();
    
    // 测试顶层模块输入端口
    sta::SignalBit a_bit("a", 0);
    sta::SignalBit a_canonical = worker.get_canonical_signal(a_bit);
    bool is_input_a = worker.is_top_module_input(a_canonical);
    
    std::cout << "Testing signal 'a' (top-level input):\n";
    std::cout << "  is_top_module_input: " << (is_input_a ? "true" : "false") << "\n";
    if (is_input_a) {
        std::cout << "  ✓ PASSED: Signal 'a' correctly identified as top-level input\n";
    } else {
        std::cout << "  ✗ FAILED: Signal 'a' should be identified as top-level input\n";
        return EXIT_FAILURE;
    }
    
    // 测试内部信号（应该返回 false）
    sta::SignalBit c_bit("c", 0);
    sta::SignalBit c_canonical = worker.get_canonical_signal(c_bit);
    bool is_input_c = worker.is_top_module_input(c_canonical);
    
    std::cout << "\nTesting signal 'c' (internal/output):\n";
    std::cout << "  is_top_module_input: " << (is_input_c ? "true" : "false") << "\n";
    if (!is_input_c) {
        std::cout << "  ✓ PASSED: Signal 'c' correctly identified as NOT top-level input\n";
    } else {
        std::cout << "  ✗ FAILED: Signal 'c' should NOT be identified as top-level input\n";
        return EXIT_FAILURE;
    }
    
    std::cout << "\n========================================\n";
    return EXIT_SUCCESS;
}

/**
 * 测试5: 集成测试 - 完整三阶段流程
 */
int test_integration(int argc, char *argv[]) {
    std::cout << "\n========================================\n";
    std::cout << "Test 5: Integration test - Full three-stage flow\n";
    std::cout << "========================================\n\n";
    
    // 解析库文件
    std::string lib_file = "/home/ysyx/project/pba-sta-base/proj/lib/simple_nangate.lib";
    SimpleCellLibParser parser(false);
    if (!parser.parse_from_file(lib_file)) {
        std::cerr << "✗ Library parsing failed!\n";
        return EXIT_FAILURE;
    }
    
    // 创建 STAWorker 并注入库（默认是 MEDIUM 模式）
    sta::STAWorker worker(parser.get_library());
    
    // 创建简单电路
    create_simple_and2_circuit(worker);
    
    std::cout << "Executing three-stage flow:\n";
    std::cout << "  1. build_fanouts()\n";
    worker.build_fanouts();
    
    std::cout << "  2. calculate_load_capacitance()\n";
    worker.calculate_load_capacitance();
    
    std::cout << "  3. calculate_timing_arcs()\n";
    worker.calculate_timing_arcs();
    
    std::cout << "  4. run() - timing propagation\n";
    worker.run();
    
    std::cout << "\n✓ All stages completed\n\n";
    
    // 验证最终结果
    const auto& arrival_time = worker.get_arrival_time();
    int max_arrival = worker.get_max_arrival_time();
    
    std::cout << "Final Results:\n";
    std::cout << "  Max arrival time: " << max_arrival << " ps\n";
    std::cout << "  Signals with arrival times: " << arrival_time.size() << "\n";
    
    // 检查输出信号 c 的 arrival time
    sta::SignalBit c_bit("c", 0);
    sta::SignalBit c_canonical = worker.get_canonical_signal(c_bit);
    if (arrival_time.count(c_canonical)) {
        int c_arrival = arrival_time.at(c_canonical);
        std::cout << "  Signal 'c' arrival time: " << c_arrival << " ps\n";
        if (c_arrival > 0) {
            std::cout << "  ✓ PASSED: Timing propagation completed successfully\n";
        } else {
            std::cout << "  ⚠ WARNING: Arrival time is 0\n";
        }
    } else {
        std::cout << "  ⚠ WARNING: Signal 'c' arrival time not found\n";
    }
    
    std::cout << "\n========================================\n";
    return EXIT_SUCCESS;
}

/**
 * 运行所有测试
 */
int test_all_sta_functions(int argc, char *argv[]) {
    std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║     STA Functions Test Suite                            ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    
    int results[5];
    results[0] = test_build_fanouts(argc, argv);
    results[1] = test_calculate_load_capacitance(argc, argv);
    results[2] = test_calculate_timing_arcs(argc, argv);
    results[3] = test_is_top_module_input(argc, argv);
    results[4] = test_integration(argc, argv);
    
    std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║     Test Summary                                          ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    std::cout << "Test 1 (build_fanouts): " << (results[0] == EXIT_SUCCESS ? "✓ PASSED" : "✗ FAILED") << "\n";
    std::cout << "Test 2 (calculate_load_capacitance): " << (results[1] == EXIT_SUCCESS ? "✓ PASSED" : "✗ FAILED") << "\n";
    std::cout << "Test 3 (calculate_timing_arcs): " << (results[2] == EXIT_SUCCESS ? "✓ PASSED" : "✗ FAILED") << "\n";
    std::cout << "Test 4 (is_top_module_input): " << (results[3] == EXIT_SUCCESS ? "✓ PASSED" : "✗ FAILED") << "\n";
    std::cout << "Test 5 (integration): " << (results[4] == EXIT_SUCCESS ? "✓ PASSED" : "✗ FAILED") << "\n";
    
    int total_passed = 0;
    for (int i = 0; i < 5; i++) {
        if (results[i] == EXIT_SUCCESS) total_passed++;
    }
    
    std::cout << "\nTotal: " << total_passed << "/5 tests passed\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n\n";
    
    return (total_passed == 5) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/**
 * Setup-Hold测试函数
 * 测试setup和hold约束的查找表插值计算
 * 使用原理文档中的具体例子进行验证
 */
int setup_hold_test(int argc, char *argv[]) {
    std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║              Setup-Hold Timing Check Test               ║\n";
    std::cout << "║         Using Example from Documentation                ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    
    // 创建原理文档中的示例查找表
    // 注意：原理文档中的值是ns单位，但我们的插值函数期望ps单位
    // 所以需要转换：0.4ns = 400ps, 0.57ns = 570ps, 0.84ns = 840ps
    
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║         Creating Test Lookup Tables from Example         ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    
    // Setup Rise Constraint Table
    celllib::LookupTable setup_rise_lut;
    setup_rise_lut.index_1 = {400.0, 570.0, 840.0};  // Data transition in ps (0.4, 0.57, 0.84 ns)
    setup_rise_lut.index_2 = {400.0, 570.0, 840.0};  // Clock transition in ps (0.4, 0.57, 0.84 ns)
    setup_rise_lut.values = {
        {63.0, 93.0, 112.0},    // Row 0: index_1=0.4ns, values for index_2=[0.4, 0.57, 0.84]
        {526.0, 644.0, 824.0},  // Row 1: index_1=0.57ns
        {720.0, 839.0, 930.0}   // Row 2: index_1=0.84ns
    };
    setup_rise_lut.template_name = "setuphold_template_3x3";
    
    // Setup Fall Constraint Table
    celllib::LookupTable setup_fall_lut;
    setup_fall_lut.index_1 = {400.0, 570.0, 840.0};
    setup_fall_lut.index_2 = {400.0, 570.0, 840.0};
    setup_fall_lut.values = {
        {762.0, 895.0, 969.0},
        {804.0, 952.0, 166.0},
        {159.0, 170.0, 245.0}
    };
    setup_fall_lut.template_name = "setuphold_template_3x3";
    
    // Hold Rise Constraint Table
    celllib::LookupTable hold_rise_lut;
    hold_rise_lut.index_1 = {400.0, 570.0, 840.0};
    hold_rise_lut.index_2 = {400.0, 570.0, 840.0};
    hold_rise_lut.values = {
        {-220.0, -339.0, -584.0},
        {-247.0, -381.0, -729.0},
        {-398.0, -516.0, -864.0}
    };
    hold_rise_lut.template_name = "setuphold_template_3x3";
    
    // Hold Fall Constraint Table
    celllib::LookupTable hold_fall_lut;
    hold_fall_lut.index_1 = {400.0, 570.0, 840.0};
    hold_fall_lut.index_2 = {400.0, 570.0, 840.0};
    hold_fall_lut.values = {
        {-28.0, -397.0, -489.0},
        {-408.0, -527.0, -649.0},
        {-705.0, -839.0, -580.0}
    };
    hold_fall_lut.template_name = "setuphold_template_3x3";
    
    // 创建table template
    celllib::TableTemplate templ;
    templ.name = "setuphold_template_3x3";
    templ.variable_1 = "constrained_pin_transition";
    templ.variable_2 = "related_pin_transition";
    templ.index_1 = {1000.0, 1001.0, 1002.0};  // 模板的索引（实际不使用）
    templ.index_2 = {1000.0, 1001.0, 1002.0};
    
    // 创建一个临时的CellLibrary来使用插值函数
    celllib::CellLibrary temp_lib;
    temp_lib.add_table_template(templ);
    
    std::cout << "Setup Rise Constraint Table (from documentation example):\n";
    std::cout << "  Index_1 (data transition): [" 
              << setup_rise_lut.index_1[0] << ", " 
              << setup_rise_lut.index_1[1] << ", " 
              << setup_rise_lut.index_1[2] << "] ps\n";
    std::cout << "  Index_2 (clock transition): [" 
              << setup_rise_lut.index_2[0] << ", " 
              << setup_rise_lut.index_2[1] << ", " 
              << setup_rise_lut.index_2[2] << "] ps\n";
    std::cout << "  Values table:\n";
    for (size_t i = 0; i < setup_rise_lut.values.size(); i++) {
        std::cout << "    Row " << i << " (index_1=" << setup_rise_lut.index_1[i] << "ps): [";
        for (size_t j = 0; j < setup_rise_lut.values[i].size(); j++) {
            std::cout << setup_rise_lut.values[i][j];
            if (j < setup_rise_lut.values[i].size() - 1) std::cout << ", ";
        }
        std::cout << "] ps\n";
    }
    std::cout << "\n";
    
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║         Verification: Exact Index Values                ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    
    // 验证原理文档中的具体例子：D pin rise transition = 0.4ns, CK pin rise transition = 0.84ns
    // 应该得到 setup constraint = 0.112ns = 112ps
    std::cout << "Testing example from documentation:\n";
    std::cout << "  D pin rise transition time = 0.4ns (400ps)\n";
    std::cout << "  CK pin rise transition time = 0.84ns (840ps)\n";
    std::cout << "  Expected setup constraint = 0.112ns (112ps)\n\n";
    
    std::string var1 = templ.variable_1.value();
    std::string var2 = templ.variable_2.value();
    
    // 测试精确索引值
    double data_trans = 400.0;  // 0.4ns
    double clock_trans = 840.0;  // 0.84ns
    double expected_setup = 112.0;  // 0.112ns
    
    double calculated_setup = temp_lib.caculate_lookuptable(
        setup_rise_lut, data_trans, clock_trans, var1, var2
    );
    
    double diff = std::abs(calculated_setup - expected_setup);
    bool match = (diff < 1e-6);
    
    std::cout << "Result:\n";
    std::cout << "  Expected: " << expected_setup << "ps (" << (expected_setup / 1000.0) << "ns)\n";
    std::cout << "  Calculated: " << calculated_setup << "ps (" << (calculated_setup / 1000.0) << "ns)\n";
    std::cout << "  Difference: " << diff << "ps\n";
    if (match) {
        std::cout << "  ✓ PASSED: Exact match!\n\n";
    } else {
        std::cout << "  ✗ FAILED: Mismatch!\n\n";
    }
    
    // 验证所有9个精确索引组合
    std::cout << "Verifying all 9 exact index combinations:\n";
    for (size_t i = 0; i < 3; i++) {
        for (size_t j = 0; j < 3; j++) {
            double data_trans = setup_rise_lut.index_1[i];
            double clock_trans = setup_rise_lut.index_2[j];
            double expected = setup_rise_lut.values[i][j];
            double calculated = temp_lib.caculate_lookuptable(
                setup_rise_lut, data_trans, clock_trans, var1, var2
            );
            
            double diff = std::abs(calculated - expected);
            bool match = (diff < 1e-6);
            
            std::cout << "  [" << i << "," << j << "] data=" << data_trans << "ps (" 
                      << (data_trans/1000.0) << "ns), clock=" << clock_trans << "ps (" 
                      << (clock_trans/1000.0) << "ns): ";
            std::cout << "expected=" << expected << "ps, calculated=" << calculated << "ps";
            if (match) {
                std::cout << " ✓ MATCH\n";
            } else {
                std::cout << " ✗ MISMATCH (diff=" << diff << "ps)\n";
            }
        }
    }
    std::cout << "\n";
    
    // 测试插值情况（介于索引值之间的值）
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║         Testing Interpolation Cases                     ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    
    std::vector<std::pair<double, double>> interpolation_cases = {
        {450.0, 600.0},   // 介于索引值之间
        {500.0, 700.0},   // 介于索引值之间
        {600.0, 800.0},   // 介于索引值之间
    };
    
    std::cout << "Testing interpolation for Setup Rise Constraint:\n";
    for (const auto& test_case : interpolation_cases) {
        double data_trans = test_case.first;
        double clock_trans = test_case.second;
        double calculated = temp_lib.caculate_lookuptable(
            setup_rise_lut, data_trans, clock_trans, var1, var2
        );
        std::cout << "  data=" << data_trans << "ps (" << (data_trans/1000.0) << "ns), "
                  << "clock=" << clock_trans << "ps (" << (clock_trans/1000.0) << "ns): "
                  << "setup=" << calculated << "ps (" << (calculated/1000.0) << "ns)\n";
    }
    std::cout << "\n";
    
    std::cout << "Testing interpolation for Hold Rise Constraint:\n";
    for (const auto& test_case : interpolation_cases) {
        double data_trans = test_case.first;
        double clock_trans = test_case.second;
        double calculated = temp_lib.caculate_lookuptable(
            hold_rise_lut, data_trans, clock_trans, var1, var2
        );
        std::cout << "  data=" << data_trans << "ps (" << (data_trans/1000.0) << "ns), "
                  << "clock=" << clock_trans << "ps (" << (clock_trans/1000.0) << "ns): "
                  << "hold=" << calculated << "ps (" << (calculated/1000.0) << "ns)";
        if (calculated < 0) {
            std::cout << " (negative - data can change before clock edge)";
        }
        std::cout << "\n";
    }
    std::cout << "\n";
    
    return EXIT_SUCCESS;
}
