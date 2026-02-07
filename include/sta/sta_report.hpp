#ifndef STA_REPORT_HPP
#define STA_REPORT_HPP

#include "sta_data_structures.hpp"
#include <cstddef>
#include <ostream>
#include <string>
#include <utility>

namespace sta {

/**
 * STA 报告生成器 - 与 STAWorker 解耦
 * 通过访问器方法获取数据，不直接依赖 STAWorker 的内部实现
 */
class STAReportGenerator {
public:
  /**
   * 生成标准格式的时序报告
   * @param worker STAWorker 实例的引用（从 worker.cfg 获取时钟周期等参数）
   * @param clock_name 时钟名称（默认 "__clk__"）
   */
  static void generate_report(const STAWorker &worker,
                              const std::string &clock_name = "__clk__");

  /**
   * 生成单个路径的详细报告（标准格式）
   */
  static void
  generate_path_report(const STAWorker &worker, const SignalBit &endpoint_bit,
                       const std::string &clock_name = "__clk__",
                       const TimingEndpoint *endpoint_override = nullptr);

  /**
   * 生成与 PT 格式一致的 8 个 timing 报告文件到指定目录
   * 文件: timing_max_reg2reg, timing_max_in2reg, timing_max_reg2out,
   * timing_max_in2out, timing_min_reg2reg, timing_min_in2reg,
   * timing_min_reg2out, timing_min_in2out
   * @param output_dir 目录路径，如 "./result/candidate/simple"
   * @param design_name 设计名，写入报告头 Design :
   */
  static void generate_report_pt_files(STAWorker &worker,
                                       const std::string &clock_name,
                                       const std::string &output_dir,
                                       const std::string &design_name, size_t top_n);

private:
  static void print_path_header(const TimingPathResult &path,
                                const std::string &clock_name,
                                std::ostream &out);
  static void print_data_arrival(const TimingPathResult &path,
                                 const std::string &clock_name,
                                 std::ostream &out, int time_decimals = 2);
  static void print_data_required(const TimingPathResult &path,
                                  const std::string &clock_name,
                                  const STAWorker &worker, std::ostream &out,
                                  int time_decimals = 2);
  static void print_slack_summary(const TimingPathResult &path,
                                  const STAWorker &worker, std::ostream &out,
                                  int time_decimals = 2);

  /// required = clock - clock_uncertainty；若 setup 有效则再减 setup。slack = required - arrival。
  static std::pair<double, double> compute_required_and_slack(
      const TimingPathResult &path, const STAWorker &worker);

  /** 格式化时间：ps -> ns 字符串，小数位数可指定（默认2，PT 风格用10） */
  static std::string format_time(double ps, int decimals = 2);

  /** 路径起终点类型，用于 PT 报告分类 */
  enum class StartEndType { RegToReg, InToReg, RegToOut, InToOut };
  static StartEndType classify_start_end_type(const TimingPathResult &path,
                                              const std::string &clock_name);

  /**
   * 获取信号显示名称
   */
  static std::string get_signal_name(const SignalBit &bit);

  static std::string get_point_name_for_report(const TimingPointRef &p,
                                               const std::string &clock_name,
                                               bool is_startpoint);

  /**
   * 判断信号是否是起点（时钟或输入端口）
   */
  static bool is_startpoint(const STAWorker &worker, const SignalBit &bit);
};

} // namespace sta

#endif // STA_REPORT_HPP
