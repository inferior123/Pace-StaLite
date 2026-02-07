#include "sta/sta_report.hpp"
#include "sta/sta_data_structures.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <vector>
#if __cplusplus >= 201703L
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace sta {

// 当前用于报告输出的 TimingRunResult（仅在 generate_report / generate_report_pt_files /
// generate_path_report 调用栈内有效）
static const TimingRunResult *g_current_timing_run = nullptr;

// 格式化时间显示（ps 转 ns，小数位数可指定）
std::string STAReportGenerator::format_time(double ps, int decimals) {
  double ns = ps / 1000.0;
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(decimals) << ns;
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

static char dir_to_char(TransitionDirection d) {
  switch (d) {
  case TransitionDirection::RISING:
    return 'r';
  case TransitionDirection::FALLING:
    return 'f';
  case TransitionDirection::UNKNOWN:
  default:
    return 'n';
  }
}

std::string
STAReportGenerator::get_point_name_for_report(const TimingPointRef &p,
                                              const std::string &clock_name,
                                              bool is_startpoint) {
  // 起点：时钟或输入端口
  if (is_startpoint) {
    if ((p.bit.has_value() && p.bit->wire_name == "__clk__") ||
        (!p.port_name.empty() && p.port_name == "__clk__")) {
      return "clock " + clock_name + " (rise edge)";
    }
    // 顶层输入端口
    if (p.inst == nullptr) {
      return p.port_name + " (in)";
    }
  }

  // 普通点：实例/端口（尽量不依赖 worker/timing_data）
  if (p.inst) {
    std::string name = p.inst->instance_name;
    if (!p.port_name.empty()) {
      name += "/" + p.port_name;
    }
    if (!p.inst->module_name.empty()) {
      name += " (" + p.inst->module_name + ")";
    }
    return name;
  }

  // 顶层输出端口或无法归类的虚拟点
  if (!p.port_name.empty()) {
    return p.port_name;
  }
  if (p.bit.has_value()) {
    return get_signal_name(*p.bit);
  }
  return "";
}

// 打印路径头部信息
void STAReportGenerator::print_path_header(const TimingPathResult &path,
                                           const std::string &clock_name,
                                           std::ostream &out) {
  const TimingPointRef *start_p = nullptr;
  const TimingPointRef *end_p = nullptr;

  if (g_current_timing_run) {
    if (path.startpoint < g_current_timing_run->points.size()) {
      start_p = &g_current_timing_run->points[path.startpoint];
    }
    if (path.endpoint < g_current_timing_run->points.size()) {
      end_p = &g_current_timing_run->points[path.endpoint];
    }
  }

  out << "\n";
  out << "  Startpoint: ";

  if (start_p && start_p->bit.has_value() &&
      start_p->bit->wire_name == "__clk__") {
    out << clock_name << " (rising edge-triggered flip-flop clocked by "
        << clock_name << ")\n";
  } else {
    if (start_p && start_p->bit.has_value()) {
      out << get_signal_name(*start_p->bit) << " (input port clocked by "
          << clock_name << ")\n";
    } else if (start_p) {
      out << start_p->port_name << " (input port clocked by "
          << clock_name << ")\n";
    } else {
      out << "(unknown startpoint clocked by " << clock_name << ")\n";
    }
  }

  out << "  Endpoint: ";

  if (end_p && end_p->inst) {
    out << end_p->inst->instance_name
        << " (rising edge-triggered flip-flop clocked by " << clock_name
        << ")\n";
  } else if (end_p && !end_p->port_name.empty()) {
    // 顶层输出端口
    if (end_p->bit.has_value()) {
      out << get_signal_name(*end_p->bit) << " (output port clocked by "
          << clock_name << ")\n";
    } else {
      out << end_p->port_name << " (output port clocked by "
          << clock_name << ")\n";
    }
  } else {
    out << "(unknown endpoint)\n";
  }

  out << "  Path Group: " << clock_name << "\n";
  out << "  Path Type: max\n";
  out << "\n";
  out << "  Point                                    Incr       Path\n";
  out << "  "
         "---------------------------------------------------------------\n";
}

// 打印数据到达时间部分
void STAReportGenerator::print_data_arrival(const TimingPathResult &path,
                                            const std::string &clock_name,
                                            std::ostream &out,
                                            int time_decimals) {
  for (size_t i = 0; i < path.steps.size(); ++i) {
    const auto &s = path.steps[i];
    bool is_start = (i == 0);

    if (!g_current_timing_run)
      continue;

    size_t point_idx = is_start ? path.startpoint : s.end_point;
    if (point_idx >= g_current_timing_run->points.size())
      continue;

    const auto &p = g_current_timing_run->points[point_idx];

    std::string point_desc =
        get_point_name_for_report(p, clock_name, is_start);
    if (point_desc.empty())
      continue;

    out << "  " << std::left << std::setw(38) << point_desc;
    out << std::right << std::setw(12) << format_time(s.incr, time_decimals);
    out << std::right << std::setw(12) << format_time(s.arrival, time_decimals)
        << " " << dir_to_char(s.dir) << "\n";
  }

  out << "  " << std::left << std::setw(38) << "data arrival time";
  out << std::right << std::setw(24)
      << format_time(path.data_arrival_time, time_decimals) << "\n";
}

// 打印数据要求时间部分
void STAReportGenerator::print_data_required(const TimingPathResult &path,
                                             const std::string &clock_name,
                                             const STAWorker &worker,
                                             std::ostream &out,
                                             int time_decimals) {
  const auto &cfg = worker.get_config();
  int clock_period = cfg.clk_period;

  out << "\n";
  out << "  " << std::left << std::setw(38)
      << ("clock " + clock_name + " (rise edge)");
  out << std::right << std::setw(12)
      << format_time(clock_period, time_decimals);
  out << std::right << std::setw(12) << format_time(clock_period, time_decimals)
      << "\n";

  out << "  " << std::left << std::setw(38) << "clock network delay (ideal)";
  out << std::right << std::setw(12) << format_time(0, time_decimals);
  out << std::right << std::setw(12) << format_time(clock_period, time_decimals)
      << "\n";

  if (cfg.clock_uncertain != 0) {
    out << "  " << std::left << std::setw(38) << "clock uncertainty";
    out << std::right << std::setw(12)
        << format_time(-cfg.clock_uncertain, time_decimals);
    out << std::right << std::setw(12)
        << format_time(clock_period - cfg.clock_uncertain, time_decimals)
        << "\n";
  }

  if (path.library_setup_time.has_value() &&
      path.library_setup_time.value() > 0) {
    out << "  " << std::left << std::setw(38) << "library setup time";
    out << std::right << std::setw(12)
        << format_time(-path.library_setup_time.value(), time_decimals);
    out << std::right << std::setw(12)
        << format_time(path.data_required_time, time_decimals) << "\n";
  }

  out << "  " << std::left << std::setw(38) << "data required time";
  out << std::right << std::setw(24)
      << format_time(path.data_required_time, time_decimals) << "\n";
}

// 打印 Slack 总结
void STAReportGenerator::print_slack_summary(const TimingPathResult &path,
                                             std::ostream &out,
                                             int time_decimals) {
  out << "  "
         "---------------------------------------------------------------\n";
  out << "  " << std::left << std::setw(38) << "data required time";
  out << std::right << std::setw(24)
      << format_time(path.data_required_time, time_decimals) << "\n";
  out << "  " << std::left << std::setw(38) << "data arrival time";
  out << std::right << std::setw(24)
      << format_time(path.data_arrival_time, time_decimals) << "\n";
  out << "  "
         "---------------------------------------------------------------\n";
  out << "  " << std::left << std::setw(38) << "slack";
  out << std::right << std::setw(10)
      << ((path.slack >= 0) ? "(MET)" : "(VIOLATED)");
  out << std::right << std::setw(12) << format_time(path.slack, time_decimals)
      << "\n";
}

// 生成单个路径的详细报告（标准格式）
void STAReportGenerator::generate_path_report(
    const STAWorker &worker, const SignalBit &endpoint_bit,
    const std::string &clock_name, const TimingEndpoint *endpoint_override) {

  (void)endpoint_bit;
  (void)endpoint_override;

  // FIXME: 目前简单打印第一条路径的详细信息；后续可根据 endpoint_bit 精确筛选
  TimingRunResult run = worker.get_sta_res();
  if (run.paths.empty()) {
    std::cout << "No timing paths found.\n";
    return;
  }

  g_current_timing_run = &run;
  const TimingPathResult &path = run.paths.front();

  print_path_header(path, clock_name, std::cout);
  print_data_arrival(path, clock_name, std::cout, 2);
  print_data_required(path, clock_name, worker, std::cout, 2);
  print_slack_summary(path, std::cout, 2);

  g_current_timing_run = nullptr;
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

  // 使用统一中间结果：从 TimingRunResult 中选出需要展示的路径
  TimingRunResult run = worker.get_sta_res();
  g_current_timing_run = &run;

  struct PathCandidate {
    const TimingPathResult *path;
    double total_time;
  };
  std::vector<PathCandidate> all_paths;

  for (const auto &p : run.paths) {
    // 这里只展示 MAX（setup）路径；MIN 可按需扩展
    if (p.mode != AnalysisMode::MAX)
      continue;
    double arrival = p.data_arrival_time;
    double setup = p.library_setup_time.value_or(0.0);
    double total = arrival + setup;
    all_paths.push_back(PathCandidate{&p, total});
  }

  std::sort(all_paths.begin(), all_paths.end(),
            [](const PathCandidate &a, const PathCandidate &b) {
              return a.total_time > b.total_time;
            });

  const int top_n = 3;
  int paths_to_show = std::min(top_n, static_cast<int>(all_paths.size()));

  if (paths_to_show == 0) {
    std::cout << "No timing paths found.\n";
    g_current_timing_run = nullptr;
    return;
  }

  // 默认打印最长的前 3 条路径
  std::cout << "Top " << paths_to_show << " Longest Path(s):\n";
  for (int i = 0; i < paths_to_show; ++i) {
    const TimingPathResult &path = *all_paths[i].path;
    std::cout << "\n--- Path #" << (i + 1)
              << " (total time: " << all_paths[i].total_time << "ps) ---\n";
    print_path_header(path, clock_name, std::cout);
    print_data_arrival(path, clock_name, std::cout, 2);
    print_data_required(path, clock_name, worker, std::cout, 2);
    print_slack_summary(path, std::cout, 2);
  }

  // 统计所有违规路径（每个 (bit, endpoint) 单独计数）
  int violation_count = 0;
  std::vector<const TimingPathResult *> violations;

  for (const auto &p : run.paths) {
    if (p.slack < 0) {
      violation_count++;
      violations.push_back(&p);
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
    std::sort(violations.begin(), violations.end(),
              [](const TimingPathResult *a, const TimingPathResult *b) {
                return a->slack < b->slack;
              });

    for (size_t i = 0; i < violations.size() &&
                       i < static_cast<size_t>(max_violations_to_show);
         ++i) {
      const TimingPathResult &path = *violations[i];
      std::cout << "Violation #" << (i + 1) << ":\n";
      print_path_header(path, clock_name, std::cout);
      print_data_arrival(path, clock_name, std::cout, 2);
      print_data_required(path, clock_name, worker, std::cout, 2);
      print_slack_summary(path, std::cout, 2);
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

  g_current_timing_run = nullptr;
}

STAReportGenerator::StartEndType
STAReportGenerator::classify_start_end_type(const TimingPathResult &path,
                                            const std::string &clock_name) {
  (void)clock_name; // 当前仅根据起终点类型分类，与具体时钟名无关

  if (!g_current_timing_run)
    return StartEndType::InToOut;

  if (path.startpoint >= g_current_timing_run->points.size() ||
      path.endpoint >= g_current_timing_run->points.size()) {
    return StartEndType::InToOut;
  }

  const auto &start_p = g_current_timing_run->points[path.startpoint];
  const auto &end_p = g_current_timing_run->points[path.endpoint];

  bool start_is_clk = (start_p.bit.has_value() &&
                       start_p.bit->wire_name == "__clk__") ||
                      (start_p.port_name == "__clk__");
  bool end_is_reg = (end_p.inst != nullptr);
  if (end_is_reg) {
    return start_is_clk ? StartEndType::RegToReg : StartEndType::InToReg;
  }
  return start_is_clk ? StartEndType::RegToOut : StartEndType::InToOut;
}

void STAReportGenerator::generate_report_pt_files(
    STAWorker &worker, const std::string &clock_name,
    const std::string &output_dir, const std::string &design_name) {
#if __cplusplus >= 201703L
  fs::create_directories(output_dir);
#else
  (void)output_dir;
  std::cerr
      << "generate_report_pt_files: C++17 required for mkdir, skipping.\n";
  return;
#endif

  // 统一中间结果：直接使用 STAWorker 内部构建好的 TimingRunResult
  const TimingRunResult run = worker.get_sta_res();
  g_current_timing_run = &run;

  struct PathEntry {
    const TimingPathResult *path;
  };
  std::vector<PathEntry> max_reg2reg, max_in2reg, max_reg2out, max_in2out;
  std::vector<PathEntry> min_reg2reg, min_in2reg, min_reg2out, min_in2out;

  // 按实际起终点类型分类到 8 个容器中（替代 PathGroup 字段）
  for (const auto &path : run.paths) {
    PathEntry e{&path};
    StartEndType t = classify_start_end_type(path, clock_name);

    if (path.mode == AnalysisMode::MAX) {
      switch (t) {
      case StartEndType::RegToReg:
        max_reg2reg.push_back(e);
        break;
      case StartEndType::InToReg:
        max_in2reg.push_back(e);
        break;
      case StartEndType::RegToOut:
        max_reg2out.push_back(e);
        break;
      case StartEndType::InToOut:
        max_in2out.push_back(e);
        break;
      }
    } else { // AnalysisMode::MIN
      switch (t) {
      case StartEndType::RegToReg:
        min_reg2reg.push_back(e);
        break;
      case StartEndType::InToReg:
        min_in2reg.push_back(e);
        break;
      case StartEndType::RegToOut:
        min_reg2out.push_back(e);
        break;
      case StartEndType::InToOut:
        min_in2out.push_back(e);
        break;
      }
    }
  }

  auto by_slack_worst_first = [](const PathEntry &a, const PathEntry &b) {
    return a.path->slack < b.path->slack;
  };
  std::sort(max_reg2reg.begin(), max_reg2reg.end(), by_slack_worst_first);
  std::sort(max_in2reg.begin(), max_in2reg.end(), by_slack_worst_first);
  std::sort(max_reg2out.begin(), max_reg2out.end(), by_slack_worst_first);
  std::sort(max_in2out.begin(), max_in2out.end(), by_slack_worst_first);
  std::sort(min_reg2reg.begin(), min_reg2reg.end(), by_slack_worst_first);
  std::sort(min_in2reg.begin(), min_in2reg.end(), by_slack_worst_first);
  std::sort(min_reg2out.begin(), min_reg2out.end(), by_slack_worst_first);
  std::sort(min_in2out.begin(), min_in2out.end(), by_slack_worst_first);

  auto write_file = [&](const std::string &filename, const char *delay_type,
                        const char *start_end_type,
                        const std::vector<PathEntry> &entries) {
    std::string path = output_dir + "/" + filename;
    std::ofstream f(path);
    if (!f) {
      std::cerr << "Cannot write " << path << "\n";
      return;
    }
    f << "****************************************\n";
    f << "Report : timing\n";
    f << "\t-path_type full\n";
    f << "\t-delay_type " << delay_type << "\n";
    f << "\t-slack_lesser_than 1000.0000000000\n";
    f << "\t-max_paths 1000\n";
    f << "\t-start_end_type " << start_end_type << "\n";
    f << "\t-sort_by slack\n";
    f << "Design : " << design_name << "\n";
    f << "Version: candidate\n";
    f << "****************************************\n\n";

    if (entries.empty()) {
      f << "No constrained paths.\n\n1\n";
      return;
    }

    for (const auto &e : entries) {
      const TimingPathResult &path = *e.path;
      print_path_header(path, clock_name, f);
      print_data_arrival(path, clock_name, f, 10);
      print_data_required(path, clock_name, worker, f, 10);
      print_slack_summary(path, f, 10);
    }
    f << "\n1\n";
  };

  write_file("timing_max_reg2reg.rpt", "max", "reg_to_reg", max_reg2reg);
  write_file("timing_max_in2reg.rpt", "max", "in_to_reg", max_in2reg);
  write_file("timing_max_reg2out.rpt", "max", "reg_to_out", max_reg2out);
  write_file("timing_max_in2out.rpt", "max", "in_to_out", max_in2out);
  write_file("timing_min_reg2reg.rpt", "min", "reg_to_reg", min_reg2reg);
  write_file("timing_min_in2reg.rpt", "min", "in_to_reg", min_in2reg);
  write_file("timing_min_reg2out.rpt", "min", "reg_to_out", min_reg2out);
  write_file("timing_min_in2out.rpt", "min", "in_to_out", min_in2out);

  g_current_timing_run = nullptr;
}

} // namespace sta
