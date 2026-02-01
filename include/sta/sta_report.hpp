#ifndef STA_REPORT_HPP
#define STA_REPORT_HPP

#include "sta_data_structures.hpp"
#include <string>
#include <vector>

namespace sta {

// 路径节点信息
struct PathNode {
    SignalBit signal;
    int arrival_time;      // 累计到达时间
    int incremental_delay; // 增量延迟
    std::string edge_type; // "r" (rising) or "f" (falling)
    Instance* driver;       // 驱动单元（如果有）
    std::string driver_port; // 驱动端口
    std::string cell_type;  // 单元类型
    bool is_startpoint;     // 是否是起点（时钟或输入）
    bool is_endpoint;       // 是否是终点
};

// 时序路径信息
struct TimingPath {
    SignalBit startpoint;
    SignalBit endpoint;
    std::string path_group;  // 时钟域名称
    std::string path_type;   // "max" (setup) or "min" (hold)
    std::vector<PathNode> path_nodes;
    int data_arrival_time;
    int data_required_time;
    int slack;
    bool met;  // 是否满足时序要求
    int setup_time = 0;  // 库中 setup time，用于报告显示
    // 用于报告显示：endpoint 的 sink 和 port（nullptr = 顶层输出）
    Instance* endpoint_sink = nullptr;
    std::string endpoint_port;
};

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
    static void generate_report(
        const STAWorker& worker,
        const std::string& clock_name = "__clk__"
    );

    /**
     * 生成单个路径的详细报告（标准格式）
     * @param worker STAWorker 实例的引用（从 worker.cfg 获取时钟周期等参数）
     * @param endpoint_bit 终点信号
     * @param clock_name 时钟名称
     * @param endpoint_override 可选，指定使用哪个 endpoint（当同一 signal 有多个 endpoint 时）
     */
    static void generate_path_report(
        const STAWorker& worker,
        const SignalBit& endpoint_bit,
        const std::string& clock_name = "__clk__",
        const TimingEndpoint* endpoint_override = nullptr
    );

private:
    /**
     * 构建路径节点列表
     * @param endpoint_override 可选，指定使用哪个 endpoint 的 Setup_req（当同一 signal 有多个 endpoint 时）
     */
    static TimingPath build_timing_path(
        const STAWorker& worker,
        const SignalBit& endpoint_bit,
        const std::string& clock_name,
        const TimingEndpoint* endpoint_override = nullptr
    );

    /**
     * 打印路径头部信息
     */
    static void print_path_header(const TimingPath& path);

    /**
     * 打印数据到达时间部分
     */
    static void print_data_arrival(const TimingPath& path);

    /**
     * 打印数据要求时间部分
     */
    static void print_data_required(const TimingPath& path, const STAWorker& worker);

    /**
     * 打印 Slack 总结
     */
    static void print_slack_summary(const TimingPath& path);

    /**
     * 格式化时间显示（ps 转 ns，保留2位小数）
     */
    static std::string format_time(int ps);

    /**
     * 获取信号显示名称
     */
    static std::string get_signal_name(const SignalBit& bit);

    /**
     * 判断信号是否是起点（时钟或输入端口）
     */
    static bool is_startpoint(const STAWorker& worker, const SignalBit& bit);

    /**
     * 获取单元类型名称
     */
    static std::string get_cell_type(Instance* inst);
};

} // namespace sta

#endif // STA_REPORT_HPP
