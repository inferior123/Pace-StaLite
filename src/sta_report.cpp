#include "sta/sta_report.hpp"
#include "sta/sta_data_structures.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace sta {

// 格式化时间显示（ps 转 ns，保留2位小数）
std::string STAReportGenerator::format_time(int ps) {
  double ns = ps / 1000.0;
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << ns;
  return oss.str();
}

// 获取信号显示名称
std::string STAReportGenerator::get_signal_name(const SignalBit &bit) {
  if (bit.bit_offset == 0) {
    return bit.wire_name;
  }
  return bit.wire_name + "[" + std::to_string(bit.bit_offset) + "]";
}

// 判断信号是否是起点（时钟或输入端口）
bool STAReportGenerator::is_startpoint(const STAWorker &worker,
                                       const SignalBit &bit) {
  const auto &driven_signals = worker.get_driven_signals();
  const auto &arrival_time = worker.get_arrival_time();

  SignalBit canonical = worker.get_canonical_signal(bit);

  // 虚拟时钟或输入端口（arrival_time 为 0 且在 driven_signals 中）
  if (driven_signals.count(canonical)) {
    auto it = arrival_time.find(canonical);
    if (it != arrival_time.end() && it->second == 0) {
      return true;
    }
  }
  return false;
}

// 获取单元类型名称
std::string STAReportGenerator::get_cell_type(Instance *inst) {
  if (!inst)
    return "";
  return inst->module_name;
}

// 构建路径节点列表
TimingPath STAReportGenerator::build_timing_path(
    const STAWorker &worker, const SignalBit &endpoint_bit,
    const std::string &clock_name, const TimingEndpoint *endpoint_override) {
  TimingPath path;
  path.endpoint = endpoint_bit;
  path.path_group = clock_name;
  path.path_type = "max"; // Setup check

  const auto &timing_data = worker.get_timing_data();
  const auto &arrival_time = worker.get_arrival_time();
  const auto &endpoints = worker.get_endpoints();

  SignalBit canonical_endpoint = worker.get_canonical_signal(endpoint_bit);

  // 获取 endpoint 信息
  const TimingEndpoint *ep = endpoint_override;
  if (!ep && endpoints.count(canonical_endpoint) &&
      !endpoints.at(canonical_endpoint).empty()) {
    ep = &endpoints.at(canonical_endpoint).front();
  }
  if (ep) {
    path.data_arrival_time = arrival_time.count(canonical_endpoint)
                                 ? arrival_time.at(canonical_endpoint)
                                 : 0;
    int setup_time = ep->Setup_req.value_or(0);
    path.setup_time = setup_time;
    path.endpoint_sink = ep->sink;
    path.endpoint_port = ep->port;
    path.data_required_time = worker.calculate_data_required_time(setup_time);
    path.slack = path.data_required_time - path.data_arrival_time;
    path.met = path.slack >= 0;
  }

  // 回溯路径
  SignalBit current = canonical_endpoint;
  std::vector<PathNode> path_nodes;
  std::unordered_set<SignalBit, SignalBitHash> visited;

  int prev_arrival = 0;

  while (true) {
    SignalBit canonical = worker.get_canonical_signal(current);

    // 防止循环
    if (visited.count(canonical)) {
      break;
    }
    visited.insert(canonical);

    PathNode node;
    node.signal = canonical;
    node.arrival_time =
        arrival_time.count(canonical) ? arrival_time.at(canonical) : 0;
    node.incremental_delay = node.arrival_time - prev_arrival;
    node.edge_type = "n"; // 默认未知沿（none）
    node.is_endpoint = (canonical.wire_name == canonical_endpoint.wire_name &&
                        canonical.bit_offset == canonical_endpoint.bit_offset);
    node.is_startpoint = is_startpoint(worker, canonical);

    // 获取驱动单元信息
    if (timing_data.count(canonical)) {
      const auto &timing = timing_data.at(canonical);
      node.driver = timing.driver;
      node.driver_port = timing.source_port;
      if (timing.driver) {
        node.cell_type = get_cell_type(timing.driver);
      }

      // 根据记录的转换方向设置沿类型
      switch (timing.transition_direction) {
      case TransitionDirection::RISING:
        node.edge_type = "r";
        break;
      case TransitionDirection::FALLING:
        node.edge_type = "f";
        break;
      case TransitionDirection::UNKNOWN:
      default:
        node.edge_type = "n";
        break;
      }
    }

    path_nodes.push_back(node);
    prev_arrival = node.arrival_time;

    // 检查是否是起点
    if (node.is_startpoint) {
      path.startpoint = canonical;
      break;
    }

    // 继续回溯
    if (!timing_data.count(canonical)) {
      break;
    }

    const auto &timing = timing_data.at(canonical);
    if (timing.backtrack.wire_name == "" ||
        (timing.backtrack.wire_name == canonical.wire_name &&
         timing.backtrack.bit_offset == canonical.bit_offset)) {
      break;
    }

    current = timing.backtrack;
  }

  // 反转路径（从起点到终点）
  std::reverse(path_nodes.begin(), path_nodes.end());

  // 重新计算增量延迟（反转后从起点到终点）
  for (size_t i = 0; i < path_nodes.size(); ++i) {
    if (i == 0) {
      path_nodes[i].incremental_delay = path_nodes[i].arrival_time;
    } else {
      path_nodes[i].incremental_delay =
          path_nodes[i].arrival_time - path_nodes[i - 1].arrival_time;
    }
  }

  path.path_nodes = path_nodes;

  return path;
}

// 打印路径头部信息
void STAReportGenerator::print_path_header(const TimingPath &path) {
  std::cout << "\n";
  std::cout << "Startpoint: ";

  // 显示起点信息
  if (path.startpoint.wire_name == "__clk__") {
    std::cout << "Virtual Clock (rising edge-triggered flip-flop clocked by "
              << path.path_group << ")\n";
  } else {
    std::cout << get_signal_name(path.startpoint) << " (primary input)\n";
  }

  std::cout << "Endpoint: ";

  // 显示终点信息：优先使用 endpoint_sink（寄存器 D 端或顶层输出）
  if (path.endpoint_sink) {
    std::cout << path.endpoint_sink->instance_name
              << " (rising edge-triggered flip-flop clocked by "
              << path.path_group << ")\n";
  } else if (!path.endpoint_port.empty()) {
    std::cout << get_signal_name(path.endpoint) << " (primary output)\n";
  } else {
    const auto &endpoint_node = path.path_nodes.back();
    if (endpoint_node.driver) {
      std::cout << endpoint_node.driver->instance_name
                << " (rising edge-triggered flip-flop clocked by "
                << path.path_group << ")\n";
    } else {
      std::cout << get_signal_name(path.endpoint) << " (primary output)\n";
    }
  }

  std::cout << "Path Group: " << path.path_group << "\n";
  std::cout << "Path Type: " << path.path_type << "\n";
  std::cout << "Point                                    Incr       Path\n";
  std::cout
      << "---------------------------------------------------------------\n";
}

// 打印数据到达时间部分
void STAReportGenerator::print_data_arrival(const TimingPath &path) {
  for (size_t i = 0; i < path.path_nodes.size(); ++i) {
    const auto &node = path.path_nodes[i];

    std::string point_desc = "";
    if (node.is_startpoint) {
      // 起点：时钟或输入端口
      if (node.signal.wire_name == "__clk__") {
        point_desc = "clock " + path.path_group + " (rise edge)";
      } else {
        point_desc = get_signal_name(node.signal) + " (primary input)";
      }
    } else if (node.driver) {
      // 驱动单元
      std::string port_info = "";
      if (!node.driver_port.empty()) {
        port_info = node.driver_port + "/";
      }

      // 确定输出端口
      std::string out_port = "";
      if (node.driver->connections.count("o")) {
        out_port = "o";
      } else if (node.driver->connections.count("q")) {
        out_port = "q";
      }

      if (!out_port.empty()) {
        point_desc = node.driver->instance_name + "/" + port_info + out_port +
                     " (" + node.cell_type + ")";
      } else {
        point_desc = node.driver->instance_name + " (" + node.cell_type + ")";
      }

      // point_desc += " " + node.edge_type;
    } else {
      // 其他节点
      // point_desc = get_signal_name(node.signal);
    }

    std::cout << std::left << std::setw(40) << point_desc;
    std::cout << std::right << std::setw(10)
              << format_time(node.incremental_delay);
    std::cout << std::right << std::setw(10) << format_time(node.arrival_time)
              << " " << node.edge_type << "\n";
  }

  std::cout << std::left << std::setw(40) << "data arrival time";
  std::cout << std::right << std::setw(20)
            << format_time(path.data_arrival_time) << "\n";
}

// 打印数据要求时间部分
void STAReportGenerator::print_data_required(const TimingPath &path,
                                             const STAWorker &worker) {
  const auto &cfg = worker.get_config();
  int clock_period = cfg.clk_period;

  std::cout << "\n";
  std::cout << std::left << std::setw(40)
            << ("clock " + path.path_group + " (rise edge)");
  std::cout << std::right << std::setw(10) << format_time(clock_period);
  std::cout << std::right << std::setw(10) << format_time(clock_period) << "\n";

  std::cout << std::left << std::setw(40) << "clock network delay (ideal)";
  std::cout << std::right << std::setw(10) << format_time(0);
  std::cout << std::right << std::setw(10) << format_time(clock_period) << "\n";

  // 如果 clock uncertainty 不为零，显示
  if (cfg.clock_uncertain != 0) {
    std::cout << std::left << std::setw(40) << "clock uncertainty";
    std::cout << std::right << std::setw(10)
              << format_time(-cfg.clock_uncertain);
    std::cout << std::right << std::setw(10)
              << format_time(clock_period - cfg.clock_uncertain) << "\n";
  }

  // 如果有 setup time，显示（从 path 中获取）
  if (path.setup_time > 0) {
    std::cout << std::left << std::setw(40) << "library setup time";
    std::cout << std::right << std::setw(10) << format_time(-path.setup_time);
    std::cout << std::right << std::setw(10)
              << format_time(path.data_required_time) << "\n";
  }

  std::cout << std::left << std::setw(40) << "data required time";
  std::cout << std::right << std::setw(20)
            << format_time(path.data_required_time) << "\n";
}

// 打印 Slack 总结
void STAReportGenerator::print_slack_summary(const TimingPath &path) {
  std::cout
      << "---------------------------------------------------------------\n";
  std::cout << std::left << std::setw(40) << "data required time";
  std::cout << std::right << std::setw(20)
            << format_time(path.data_required_time) << "\n";
  std::cout << std::left << std::setw(40) << "data arrival time";
  std::cout << std::right << std::setw(20)
            << format_time(path.data_arrival_time) << "\n";
  std::cout
      << "---------------------------------------------------------------\n";
  std::cout << std::left << std::setw(40) << "slack";
  std::cout << std::right << std::setw(10)
            << (path.met ? "(MET)" : "(VIOLATED)");
  std::cout << std::right << std::setw(10) << format_time(path.slack) << "\n";
}

// 生成单个路径的详细报告（标准格式）
void STAReportGenerator::generate_path_report(
    const STAWorker &worker, const SignalBit &endpoint_bit,
    const std::string &clock_name, const TimingEndpoint *endpoint_override) {
  TimingPath path =
      build_timing_path(worker, endpoint_bit, clock_name, endpoint_override);

  print_path_header(path);
  print_data_arrival(path);
  print_data_required(path, worker);
  print_slack_summary(path);
}

// 生成标准格式的时序报告
void STAReportGenerator::generate_report(const STAWorker &worker,
                                         const std::string &clock_name) {
  const auto &cfg = worker.get_config();
  int clock_period = cfg.clk_period;

  std::cout
      << "\n╔══════════════════════════════════════════════════════════╗\n";
  std::cout << "║              Static Timing Analysis Report               ║\n";
  std::cout
      << "╚══════════════════════════════════════════════════════════╝\n\n";

  std::cout << "Module: " << worker.top_moudle << "\n";
  std::cout << "Clock Period: " << clock_period << "ps ("
            << format_time(clock_period) << "ns)\n";
  std::cout
      << "===========================================================\n\n";

  // 收集所有 endpoint 路径，按 total_time (arrival + setup_req)
  // 降序排序，取最长的前 3 条
  const auto &endpoints = worker.get_endpoints();
  const auto &arrival_time_map = worker.get_arrival_time();

  struct PathCandidate {
    SignalBit bit;
    TimingEndpoint endpoint;
    int total_time;
  };
  std::vector<PathCandidate> all_paths;

  for (const auto &[bit, eps] : endpoints) {
    SignalBit canonical = worker.get_canonical_signal(bit);
    if (!arrival_time_map.count(canonical))
      continue;
    int arrival = arrival_time_map.at(canonical);
    for (const auto &ep : eps) {
      int setup = ep.Setup_req.value_or(0);
      int total = arrival + setup;
      all_paths.push_back({canonical, ep, total});
    }
  }

  std::sort(all_paths.begin(), all_paths.end(),
            [](const PathCandidate &a, const PathCandidate &b) {
              return a.total_time > b.total_time;
            });

  const int top_n = 3;
  int paths_to_show = std::min(top_n, static_cast<int>(all_paths.size()));

  if (paths_to_show == 0) {
    std::cout << "No timing paths found.\n";
    return;
  }

  // 默认打印最长的前 3 条路径
  std::cout << "Top " << paths_to_show << " Longest Path(s):\n";
  for (int i = 0; i < paths_to_show; ++i) {
    std::cout << "\n--- Path #" << (i + 1)
              << " (total time: " << all_paths[i].total_time << "ps) ---\n";
    generate_path_report(worker, all_paths[i].bit, clock_name,
                         &all_paths[i].endpoint);
  }

  // 统计所有违规路径（每个 (bit, endpoint) 单独计数）
  int violation_count = 0;
  std::vector<std::pair<SignalBit, TimingEndpoint>> violations;

  for (const auto &[bit, eps] : endpoints) {
    SignalBit canonical = worker.get_canonical_signal(bit);

    if (!arrival_time_map.count(canonical)) {
      continue;
    }

    int arrival = arrival_time_map.at(canonical);
    for (const auto &endpoint : eps) {
      int setup_time = endpoint.Setup_req.value_or(0);
      int data_required_time = worker.calculate_data_required_time(setup_time);

      if (arrival > data_required_time) {
        violation_count++;
        violations.push_back({canonical, endpoint});
      }
    }
  }

  // 显示违规路径
  if (violation_count > 0) {
    std::cout
        << "\n\n╔══════════════════════════════════════════════════════════╗\n";
    std::cout
        << "║              Timing Violations Summary                   ║\n";
    std::cout
        << "╚══════════════════════════════════════════════════════════╝\n\n";
    std::cout << "Total Violations: " << violation_count << "\n\n";

    // 显示前几个违规路径
    int max_violations_to_show = 5;
    for (size_t i = 0; i < violations.size() &&
                       i < static_cast<size_t>(max_violations_to_show);
         ++i) {
      std::cout << "Violation #" << (i + 1) << ":\n";
      generate_path_report(worker, violations[i].first, clock_name,
                           &violations[i].second);
      std::cout << "\n";
    }

    if (static_cast<int>(violations.size()) > max_violations_to_show) {
      std::cout << "... and " << (violations.size() - max_violations_to_show)
                << " more violations.\n";
    }
  } else {
    std::cout << "\n\n✓ No timing violations found. All paths meet timing "
                 "requirements.\n";
  }

  std::cout
      << "\n╔══════════════════════════════════════════════════════════╗\n";
  std::cout << "║              End of Report                               ║\n";
  std::cout
      << "╚══════════════════════════════════════════════════════════╝\n\n";
}

} // namespace sta
