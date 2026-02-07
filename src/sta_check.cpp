#include "sta/sta_data_structures.hpp"
#include "sta/sta_report.hpp" 

namespace sta  {

void STAWorker::sta_check() {
  int clock_period = cfg.clk_period;

  // 更新配置中的时钟周期（如果传入）
  if (clock_period > 0) {
    cfg.clk_period = clock_period;
  }

  // 计算有效时钟周期（考虑 uncertainty 等时序参数），单位 ps
  int effective_period_ps = get_effective_clock_period();

  // 如果最大的路径时序长度小于有效时钟周期，那么直接返回没有时序违规
  if (max_arrival_time <= effective_period_ps) {
    std::cout << "\n✓ No timing violations found.\n";
    std::cout << "  Max arrival time: " << max_arrival_time << "ps ("
              << (max_arrival_time / 1000.0) << "ps)\n";
    std::cout << "  Clock period: " << cfg.clk_period << "ps ("
              << (cfg.clk_period / 1000.0) << "ps)";
    if (cfg.clock_uncertain > 0) {
      std::cout << " (effective: " << effective_period_ps
                << "ps after uncertainty)";
    }
    std::cout << "\n";
    std::cout << "  Slack: " << (effective_period_ps - max_arrival_time)
              << "ps\n";
    return;
  }

  // 否则找到所有大于有效时钟周期的路径（时序违规）
  std::cout << "\n⚠ Timing violations detected!\n";
  std::cout << "  Clock period: " << cfg.clk_period << "ps";
  if (cfg.clock_uncertain > 0) {
    std::cout << " (effective: " << effective_period_ps
              << "ps after uncertainty)";
  }
  std::cout << "\n";
  std::cout << "  Max arrival time: " << max_arrival_time << "ps ("
            << (max_arrival_time / 1000.0) << "ps)\n";
  std::cout << "  Worst slack: " << (effective_period_ps - max_arrival_time)
            << "ps\n\n";

  std::cout << "Violating paths:\n";
  std::cout << "----------------------------------------\n";

  int violation_count = 0;
  for (const auto &[bit, eps] : endpoints) {
    SignalBit canonical = sigmap.find(bit);

    // 只检查有 arrival time 的 endpoint
    if (!arrival_time.count(canonical)) {
      continue;
    }

    double arrival = arrival_time.at(canonical);
    for (const auto &endpoint : eps) {
      double setup_time = endpoint.Setup_req.value();
      // 计算考虑所有时序参数后的 data required time
      double data_required_time = calculate_data_required_time(setup_time);

      // 检查是否超过有效时钟周期（使用 data_required_time 进行比较）
      if (arrival > data_required_time) {
        violation_count++;
        double slack = data_required_time - arrival;

        std::cout << "\n[" << violation_count
                  << "] Violation at: " << canonical.wire_name << "["
                  << canonical.bit_offset << "]\n";
        std::cout << "  Arrival time: " << arrival << "ps ("
                  << (arrival / 1000.0) << "ps)\n";
        std::cout << "  Setup time: " << setup_time << "ps\n";
        std::cout << "  Data required time: " << data_required_time << "ps";
        if (cfg.clock_uncertain > 0) {
          std::cout << " (clock period " << cfg.clk_period << "ps - setup "
                    << setup_time << "ps - uncertainty " << cfg.clock_uncertain
                    << "ps)";
        }
        std::cout << "\n";
        std::cout << "  Slack: " << slack << "ps (violation: " << (-slack)
                  << "ps)\n";

        // 显示 endpoint 信息
        if (endpoint.sink) {
          std::cout << "  Endpoint: " << endpoint.sink->instance_name << " ("
                    << endpoint.sink->module_name << "." << endpoint.port
                    << ")\n";
        } else {
          std::cout << "  Endpoint: Top module output (" << endpoint.port
                    << ")\n";
        }

        // 回溯并打印完整路径
        std::cout << "\n  Path trace (from input to endpoint):\n";
        trace_path(canonical);
      }
    }
  }

  std::cout << "\n----------------------------------------\n";
  std::cout << "Total violations: " << violation_count << "\n";

  if (violation_count == 0) {
    std::cout << "Note: No violations found in endpoints, but max_arrival_time "
                 "exceeds clock period.\n";
    std::cout
        << "This may indicate an issue with the critical path calculation.\n";
  }
}

void STAWorker::trace_path(const SignalBit &endpoint_bit) {
  // 从 endpoint 回溯到输入端口
  std::vector<std::pair<SignalBit, int>> path; // (signal, arrival_time)
  SignalBit current = endpoint_bit;
  std::unordered_set<SignalBit, SignalBitHash> visited;

  while (true) {
    SignalBit canonical = sigmap.find(current);

    // 防止循环
    if (visited.count(canonical)) {
      break;
    }
    visited.insert(canonical);

    // 获取 arrival time
    int arrival = -1;
    if (arrival_time.count(canonical)) {
      arrival = arrival_time.at(canonical);
    }

    path.push_back({canonical, arrival});

    // 检查是否是输入端口（在 driven_signals 中且 arrival_time 为 0）
    if (driven_signals.count(canonical) && arrival == 0) {
      // 输入端口或虚拟时钟，回溯结束
      break;
    }

    // 检查是否有 backtrack 信息
    if (!timing_data.count(canonical)) {
      // 没有时序数据，无法继续回溯
      break;
    }

    const auto &timing = timing_data.at(canonical);

    // 检查 backtrack 是否有效
    if (timing.backtrack.wire_name == "" ||
        (timing.backtrack.wire_name == canonical.wire_name &&
         timing.backtrack.bit_offset == canonical.bit_offset)) {
      break;
    }

    // 继续回溯
    current = timing.backtrack;
  }

  // 反转路径（从输入到输出）
  std::reverse(path.begin(), path.end());

  // 打印路径
  int path_index = 0;
  for (size_t i = 0; i < path.size(); ++i) {
    const auto &[bit, arrival] = path[i];

    // 获取时序数据
    bool has_timing = timing_data.count(bit);
    const auto *timing = has_timing ? &timing_data.at(bit) : nullptr;

    std::cout << "    [" << path_index++ << "] ";

    // 打印信号信息
    std::cout << bit.wire_name << "[" << bit.bit_offset << "]";
    if (arrival >= 0) {
      std::cout << " (arrival: " << arrival << "ps)";
    }

    // 如果是输入端口（arrival_time 为 0 且在 driven_signals 中）
    if (driven_signals.count(bit) && arrival == 0) {
      if (bit.wire_name == "__clk__") {
        std::cout << " [Virtual Clock]\n";
      } else {
        std::cout << " [Primary Input]\n";
      }
      continue;
    }

    // 如果有驱动单元
    if (timing && timing->driver) {
      std::cout << "\n        → ";
      std::cout << timing->driver->instance_name << " ("
                << timing->driver->module_name << ")";
      if (!timing->source_port.empty()) {
        std::cout << " via port " << timing->source_port;
      }
      std::cout << "\n";
    }

    // 如果是最后一个节点（endpoint）
    if (i == path.size() - 1) {
      std::cout << " [Endpoint]\n";
    } else {
      std::cout << "\n";
    }
  }
}

};