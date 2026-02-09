#ifndef STA_DATA_STRUCTURES_HPP
#define STA_DATA_STRUCTURES_HPP

#include "../cell/cell_data_structure.hpp"
#include "../parser-verilog/verilog_data.hpp"
#include "cassert"
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sta {

// ============================================================================
// 1. 信号表示（Signal Representation）
// ============================================================================

/**
 * 信号位：表示一个信号的一位
 * 由 (wire_name, bit_offset) 唯一标识
 */
struct SignalBit {
  std::string wire_name;
  int bit_offset;

  SignalBit() : bit_offset(0) {}
  SignalBit(const std::string &name, int offset)
      : wire_name(name), bit_offset(offset) {}

  bool operator==(const SignalBit &other) const {
    return wire_name == other.wire_name && bit_offset == other.bit_offset;
  }

  bool operator!=(const SignalBit &other) const { return !operator==(other); }

  bool operator<(const SignalBit &other) const {
    if (wire_name != other.wire_name)
      return wire_name < other.wire_name;
    return bit_offset < other.bit_offset;
  }
};

// 信号位哈希函数
struct SignalBitHash {
  std::size_t operator()(const SignalBit &bit) const {
    return std::hash<std::string>{}(bit.wire_name) ^
           (std::hash<int>{}(bit.bit_offset) << 1);
  }
};

// 信号集合
using SignalSpec = std::vector<SignalBit>;

// ============================================================================
// 2. 信号映射（Signal Mapping）
// ============================================================================

/**
 * 信号映射表：使用 Union-Find 算法将连接的信号映射到规范代表
 * 简化版实现，适合原型工具
 */
class SignalMap {
private:
  mutable std::unordered_map<SignalBit, SignalBit, SignalBitHash> parent;

  SignalBit find_root(const SignalBit &bit) const {
    auto it = parent.find(bit);
    if (it == parent.end() || it->second == bit) {
      return bit;
    }
    // 路径压缩
    SignalBit root = find_root(it->second);
    parent[bit] = root;
    return root;
  }

public:
  // 添加连接关系：from -> to
  void add_connection(const SignalSpec &from, const SignalSpec &to) {
    if (from.size() != to.size()) {
      assert(false && "the connnect size not equ");
      return;
    }

    for (size_t i = 0; i < from.size(); ++i) {
      SignalBit root_from = find_root(from[i]);
      SignalBit root_to = find_root(to[i]);

      if (root_from != root_to) {
        parent[root_from] = root_to;
      }
    }
  }

  // 查找信号的规范代表
  SignalBit find(const SignalBit &bit) const {
    if (parent.find(bit) == parent.end()) {
      return bit; // 未映射的信号返回自身
    }
    return find_root(bit);
  }

  // 应用映射
  SignalBit operator()(const SignalBit &bit) { return find(bit); }

  SignalSpec operator()(const SignalSpec &sig) {
    SignalSpec result;
    for (const auto &bit : sig) {
      result.push_back(find(bit));
    }
    return result;
  }
};

// ============================================================================
// 3. 单元实例（Instance）
// ============================================================================

// 前向声明
struct Instance;

/**
 * 单元实例：表示 Verilog 中的模块实例
 */
struct Instance {
  std::string module_name;   // 模块类型名
  std::string instance_name; // 实例名

  // 端口连接：端口名 -> 信号
  std::unordered_map<std::string, SignalSpec> connections;

  // 输出端口负载电容：output_pin -> 该输出端口上看到的负载总电容(ff)，即所有
  // fanout 的 input pin 电容之和
  std::unordered_map<std::string, double> load_capacitance;

  Instance(const std::string &mod, const std::string &inst)
      : module_name(mod), instance_name(inst) {}
};

// ============================================================================
// 4. 时序数据（Timing Data）
// ============================================================================

/**
 * 信号转换方向枚举：用于选择使用哪个查找表（rise/fall）
 */
enum class TransitionDirection {
  RISING,  // 上升沿：0 -> 1，使用 rise_transition / cell_rise
  FALLING, // 下降沿：1 -> 0，使用 fall_transition / cell_fall
  UNKNOWN  // 未知：如果没有指定，使用默认值（通常取最大值或使用上升沿）
};

// ============================================================================
// 4.5 统一“中间结果”数据结构（与报告格式解耦）
// ============================================================================
// 说明：
// - 这些结构只描述“路径上发生了什么 + 数值是什么”，不包含“怎么打印”
// - 报告层可按 (mode, group) 过滤/排序后渲染出任意格式（包括 8 类 rpt）
// - 沿方向允许 UNKNOWN；报告渲染时可打印为 'n'

enum class AnalysisMode { MAX, MIN };

enum class PathGroup { REG2REG, IN2REG, REG2OUT, IN2OUT };

enum PointType { COMB_PIN, INPUT, OUTPUT, CLK, REGQ, REGD };

/// 按起终点 PointType 得到 PathGroup，建 path 时设好，报告层直接用
inline PathGroup classify_path_group(PointType start_type, PointType end_type) {
  if (end_type == REGD)
    return (start_type == CLK) ? PathGroup::REG2REG : PathGroup::IN2REG;
  if (end_type == OUTPUT)
    return (start_type == CLK) ? PathGroup::REG2OUT : PathGroup::IN2OUT;
  return PathGroup::IN2OUT;
}

/**
 * 时序点引用：用于强索引/可追溯
 * - inst==nullptr 表示顶层端口或虚拟点（如虚拟 clock）
 * - port_name：顶层端口名或实例 pin 名
 * - bit：建议填充 sigmap 后的 canonical SignalBit，用于去重/对齐/回查
 * - start_step / end_step：可选，表示该点在某条路径中覆盖的 step
 *   区间下标（用于快速索引 / 统计），默认 SIZE_MAX 表示“未设置”
 */
enum EdgeType { WIRE, COMB_ARC, SEQ_ARC };

struct TimingPointRefKey {
  Instance *inst;
  std::string port_name;
  std::optional<SignalBit> bit;
};

struct TimingEdge {
  EdgeType type;
  size_t origin_point;
  size_t target_point;
};

struct TimingPointRef {
  std::size_t id;
  Instance *inst;
  std::string port_name;
  std::optional<SignalBit> bit;

  PointType type;
  std::vector<TimingEdge> fanouts;
};

inline bool operator==(const TimingPointRefKey &a, const TimingPointRefKey &b) {
  if (a.inst != b.inst)
    return false;
  if (a.port_name != b.port_name)
    return false;
  if (a.bit != b.bit)
    return false;
  return true;
}

// TimingPointRefKey 哈希
struct TimingPointRefHash {
  std::size_t operator()(const TimingPointRefKey &p) const {
    std::size_t h1 = std::hash<Instance *>{}(p.inst);
    std::size_t h2 = std::hash<std::string>{}(p.port_name);
    std::size_t h3 = 0;
    if (p.bit.has_value()) {
      h3 = SignalBitHash{}(p.bit.value());
    }

    std::size_t h = h1;
    h ^= (h2 << 1);
    h ^= (h3 << 2);
    return h;
  }
};

/**
 * 路径上的一步（报告表格中的一行）
 * - incr: 相对上一点的增量延迟（单位由全局约定，当前工程多按 ps）
 * - arrival: 累计到该点的到达时间
 * - dir: 沿方向（UNKNOWN 允许；报告渲染可输出 'n'）
 */
struct TimingStep {
  size_t start_point;
  size_t end_point;
  double incr = 0.0;
  double slew = 0.0;
  double cap_load = 0.0;
  double arrival = 0.0;
  TransitionDirection dir = TransitionDirection::UNKNOWN;
};

/**
 * 一条时序路径结果：summary + steps
 */
struct TimingPathResult {
  PathGroup group = PathGroup::REG2REG;
  size_t index;

  size_t startpoint;
  size_t endpoint;

  double data_arrival_time = 0.0;

  std::optional<double> library_setup_time;
  std::optional<double> library_hold_time;

  std::vector<TimingStep> steps;
};

/**
 * 一次 STA/PBA run 的统一输出：只存 paths，不绑定“8 个报告文件”
 * - edges: 全局边表，按 (points[0].fanouts, points[1].fanouts, ...)
 * 顺序排列，便于用 index 前后组合路径
 */
struct TimingRunResult {
  std::vector<TimingPathResult> paths;
  std::vector<TimingPointRef> points;

  /// 全局边表，由 build_res_edges() 填充；edges[i].origin_point / target_point
  /// 与 type 有效
  std::vector<TimingEdge> edges;

  std::unordered_map<TimingPointRefKey, std::size_t, TimingPointRefHash>
      point_index;

  /// 给定 from_pt -> to_pt，返回在 edges 中的下标；不存在则返回 SIZE_MAX
  std::size_t get_edge_index(std::size_t from_pt, std::size_t to_pt) const {
    for (std::size_t i = 0; i < edges.size(); ++i)
      if (edges[i].origin_point == from_pt && edges[i].target_point == to_pt)
        return i;
    return SIZE_MAX;
  }
};

/// 用于 path group 分类的起点类型：虚拟时钟 __clk__ 或 build_fanouts 中已标为
/// CLK 的 “连到 FF 时钟端的 input” 视为 CLK，其余顶层端口视为 INPUT
inline PointType effective_start_type_for_group(const TimingPointRef &p) {
  if (p.inst == nullptr) {
    if (p.type == CLK || p.port_name == "__clk__" ||
        (p.bit.has_value() && p.bit->wire_name == "__clk__"))
      return CLK;
    return INPUT;
  }
  return p.type;
}

// ============================================================================
// 5. 无知节点 (Candidate)
// ============================================================================
// 用于在 PBA/精细时序中只在“无法确定上升/下降”的节点之间建图。
// 这个数据结构用来存储无法确定为上升和下降的节点
// 然后使用这个无知节点构建出来的图来计算最后的 max 和 min
// 无知节点包含三种情况 clk2q、input、无单调cell的input
/**
 * 无知图的边：从一个 CandidateNode 到下一个 CandidateNode 的压缩段
 * - fanouts_edge 存 res.edges 的
 * index，便于前后路径组合；先不记录时序，后续再组合计算
 */
struct CandidatePath {
  std::size_t id;         // 在 CandidateGraphy::paths 中的下标
  std::size_t start_node; // 起点节点 id（下标）
  std::size_t end_node;
  std::optional<std::size_t> next_path;

  /// 起点到终点之间的边序列，为 res.edges 的下标（TimingEdge index）
  std::vector<size_t> fanouts_edge;
};

/**
 * 无知图中的节点：
 * 表示一个“关键端口”（clk2q段、input、无单调 cell 的 output）
 * 或者是一个关键路径的结束，一个寄存器的d端，或者是一个输出端口，或者是一个unnate
 * 原件的一个时序弧的输出端
 * 特别的时序弧的输出端应该是相互重合的，endpoint 和 startpoint是重合的
 */
struct CandidateNode {
  std::size_t id; // 在 CandidateGraphy::nodes 中的下标
  size_t point_idx;

  // 从该节点出发的所有 candidate path 的 id（索引到 CandidateGraphy::paths）
  std::vector<std::size_t> fanout_paths;
};

/// 单条 candidate path 在给定输入 (dir, slew) 下的计算结果，用于链式拼接
struct CandidatePathSegmentResult {
  double total_delay_ps = 0.0;
  double output_slew_ns = 0.0;
  TransitionDirection output_dir = TransitionDirection::UNKNOWN;
  std::vector<TimingStep> steps;
};

/**
 * 无知图：用于在 PBA/精细时序中只在“无法确定上升/下降”的节点之间建图
 *
 * 设计要点：
 * - 节点和边统一用下标（id）相互索引，便于 O(1) 访问和遍历
 * - 通过 point_to_node 实现 point_idx -> node_id 的 O(1) 查找，统一基于 Point
 * 抽象
 */
struct CandidateGraphy {
  std::vector<CandidateNode> nodes;
  std::vector<CandidatePath> paths;

  // point_idx -> node_id 的索引
  std::unordered_map<std::size_t, std::size_t> point_to_node;
};

struct PathEntry {
  const TimingPathResult *path;
};

enum class PathEntryType {
  REG2REG_MAX,
  IN2REG_MAX,
  REG2OUT_MAX,
  IN2OUT_MAX,
  REG2REG_MIN,
  IN2REG_MIN,
  REG2OUT_MIN,
  IN2OUT_MIN
};
// ============================================================================
// 6. STA 工作器（STA Worker）
// ============================================================================

/**
 * STA 工作器：执行静态时序分析的核心类
 */
class STAWorker {
private:
  // 核心数据结构
  SignalMap sigmap;
  std::deque<SignalBit> timing_queue;
  // 从 startpoint（clk / 顶层 input）出发的 path 的 id，供 PBA 传播用
  std::deque<std::size_t> candidate_timing_queue;
  std::unordered_set<SignalBit, SignalBitHash> driven_signals;

  // 无知节点图（用于处理clk2q段 / input / 非单调cell输入等“沿不确定”场景）
  CandidateGraphy candidate_graphy_;

  // 顶层模块端口信息
  std::unordered_set<SignalBit, SignalBitHash>
      top_module_inputs; // 顶层模块的输入端口

  std::unordered_map<std::string, std::vector<SignalBit>> signal_registry;

  // Arrival time for each signal bit
  std::unordered_map<SignalBit, double, SignalBitHash> arrival_time;

  // 分析结果
  double max_arrival_time;
  SignalBit critical_signal;

  // 单元实例管理
  std::vector<std::unique_ptr<Instance>> instances;

  const celllib::CellLibrary *cell_library_ = nullptr;

  // 时序配置
  struct sta_config {
    int clk_period = 0;
    int clock_uncertain = 0;
    int clock_transit_raise = 0;
    int clock_transit_fall = 0;
  };
  sta_config cfg;

  TimingRunResult res;

  // input 和 clk 的 point id（collect_port 填充 input，build_fanouts 填充 clk）
  std::vector<std::size_t> input_clk_point_ids;

public:
  /**
   * 仿真颗粒度：控制时序分析的精度级别
   */
  enum class AnalysisGranularity {
    COARSE, // 粗略模式：使用固定延迟，使用一个查找表之中的悲观值，不考虑负载电容和转换时间
    MEDIUM, // 中等模式：考虑负载电容，使用完成查找表插值
    FINE    // 精确模式：后续版本再考虑实现，遇到直接assert
  };

private:
  AnalysisGranularity analysis_granularity_ = AnalysisGranularity::MEDIUM;
  AnalysisMode analysis_mode = AnalysisMode::MAX;

  std::vector<PathEntry> reg2reg_max, in2reg_max, reg2out_max, in2out_max;
  std::vector<PathEntry> reg2reg_min, in2reg_min, reg2out_min, in2out_min;

public:
  STAWorker() : max_arrival_time(0) {}

  explicit STAWorker(const celllib::CellLibrary &lib)
      : max_arrival_time(0), cell_library_(&lib) {}

  // 仿真颗粒度访问器
  AnalysisGranularity get_analysis_granularity() const {
    return analysis_granularity_;
  }
  void set_analysis_granularity(AnalysisGranularity granularity) {
    analysis_granularity_ = granularity;
  }
  AnalysisMode get_analysis_mode() const { return analysis_mode; }
  void set_analysis_mode(AnalysisMode mode) { analysis_mode = mode; }

  // 和verilog parser相耦合的函数
  void collect_net(verilog::Net &net);
  void collect_port(verilog::Port &port);
  void collect_assign(verilog::Assignment &assign);
  void collect_instance(verilog::Instance &inst);

  // 核心工作函数
  void build_fanouts();
  /// 根据 res.points 的 fanouts 填充 res.edges，供 CandidatePath 使用 edge
  /// index
  void build_res_edges();
  void calculate_load_capacitance();
  void calculate_timing_arcs();

  /// DFS 时序分析：从 input_clk_point_ids 出发，沿 Point 图 DFS，产出 res.paths
  void run_timing_analysis_dfs();

  /// 基于 point_idx 创建/获取 candidate 节点（推荐）
  std::size_t get_or_create_candidate_node(std::size_t point_idx);
  std::size_t get_or_create_point_node(Instance *inst, const SignalBit &bit,
                                       const std::string &port_name);
  std::size_t get_or_create_point(Instance *inst, const std::string &port_name,
                                  std::optional<SignalBit> bit, PointType type);

  void build_candidate_graphy_dfs();
  /// 给定输入 (transition_dir, input_slew_ns) 计算一条 candidate path，返回
  /// delay/slew/dir/steps，便于链式算 result path
  CandidatePathSegmentResult
  compute_candidate_path_with_input(std::size_t path_id,
                                    TransitionDirection input_dir,
                                    double input_slew_ns) const;
  void run_candidate_graphy_dfs();

  // void print_all_timing_paths_bfs(); //
  // BFS/拓扑序：用队列按层枚举并打印每一条时序路径（不依赖 timing_queue）
  void sta_check();

  SignalSpec get_signal_bits(const std::string &signame) const;

  SignalSpec convert_to_signalspec(const verilog::RHS &rhs);
  SignalSpec convert_to_signalspec(const verilog::LHS &lhs);

  /// 打印一条 result path 的完整信息（steps、dir、slew、arrival、终点
  /// setup/hold），供计算流程中调用
  void display_result_path_detail(TimingPathResult &pr) const;

  std::vector<PathEntry> get_path_entry(PathGroup group_type,
                                        AnalysisMode mode);
  std::vector<PathEntry> get_path_entry(PathEntryType type);
  void respath_descending(PathGroup group_type, AnalysisMode mode);
  void respath_ascending(PathGroup group_type, AnalysisMode mode);
  void respath_ascending(PathEntryType type);
  void respath_descending(PathEntryType type);

  void divide_path_entry();

  // top module name
  std::string top_moudle;

  // 配置访问器
  const sta_config &get_config() const { return cfg; }
  sta_config &get_config() { return cfg; }

  /**
   * 计算考虑所有时序参数后的有效时钟周期（用于 setup 检查）
   * 考虑的因素：
   * - clock_uncertain: setup uncertainty（减少可用时间）
   * - clock_transit_raise/fall: 时钟转换时间（未来可能使用）
   *
   * @return 有效时钟周期（ps），已减去所有减少可用时间的因素
   */
  int get_effective_clock_period() const {
    int effective_period = cfg.clk_period;

    // 减去 setup uncertainty（减少可用时间）
    if (cfg.clock_uncertain > 0) {
      effective_period -= cfg.clock_uncertain;
    }

    // 未来可以在这里添加其他减少可用时间的因素
    // 例如：clock_transit_raise, clock_transit_fall 等

    return effective_period;
  }

  /**
   * 计算考虑所有时序参数后的 data required time
   * @param setup_time 库中定义的 setup time
   * @return 考虑所有时序参数后的 data required time
   */
  int calculate_data_required_time(int setup_time) const {
    int effective_period = get_effective_clock_period();
    return effective_period - setup_time;
  }

  double caculate_data_required_time(double setup_time) const {
    int effective_period = get_effective_clock_period();
    return (double)effective_period - setup_time; // 均以 ps 为单位
  }

  void set_cell_library(const celllib::CellLibrary &lib) {
    cell_library_ = &lib;
  }

  bool has_cell_library() const { return cell_library_ != nullptr; }

  const celllib::CellLibrary *get_cell_library() const { return cell_library_; }

  bool is_reg(std::string name);

  /**
   * 检查信号是否是顶层模块的输入端口
   * @param bit 要检查的信号位（可以是原始信号或规范代表）
   * @return 如果是顶层模块输入则返回true，否则返回false
   */
  bool is_top_module_input(const SignalBit &bit) const {
    SignalBit canonical = sigmap.find(bit);
    return top_module_inputs.count(canonical) > 0;
  }

  // 用于外部访问的接口
  const std::vector<std::unique_ptr<Instance>> &get_instances() const {
    return instances;
  }
  const std::unordered_map<std::string, std::vector<SignalBit>> &
  get_signal_registry() const {
    return signal_registry;
  }
  const std::unordered_set<SignalBit, SignalBitHash> &
  get_driven_signals() const {
    return driven_signals;
  }
  const std::deque<SignalBit> &get_timing_queue() const { return timing_queue; }
  SignalBit get_canonical_signal(const SignalBit &bit) const {
    return sigmap.find(bit);
  }

  // 无知图访问器（供PBA或调试使用）
  const CandidateGraphy &get_candidate_graphy() const {
    return candidate_graphy_;
  }

  // 用于调试的访问器
  const std::unordered_map<SignalBit, double, SignalBitHash> &
  get_arrival_time() const {
    return arrival_time;
  }
  double get_max_arrival_time() const { return max_arrival_time; }
  SignalBit get_critical_signal() const { return critical_signal; }

  TimingRunResult get_sta_res() const { return res; }
  const std::vector<std::size_t> &get_input_clk_point_ids() const {
    return input_clk_point_ids;
  }

private:
  // 内部辅助函数
  std::pair<double, double> compute_require_and_slack(size_t path_idx);
  double compare_slack(size_t a, size_t b);
  /// 根据 path 最后一步与 endpoint（REGD）计算并填充 library_setup_time /
  /// library_hold_time
  void compute_path_setup_hold(TimingPathResult &pr) const;
  /// 返回指定 (group_type, mode) / type 对应的 path entry
  /// 向量指针，用于原地排序
  std::vector<PathEntry> *get_path_entry_ptr(PathGroup group_type,
                                             AnalysisMode mode);
  std::vector<PathEntry> *get_path_entry_ptr(PathEntryType type);

  SignalBit *get_virtual_clock();
  void propagate_timing(const SignalBit &bit);
  void trace_critical_path();

  void trace_path(const SignalBit &endpoint_bit); // 回溯并打印路径
  SignalBit create_signal_bit(const std::string &name, int offset);
};

} // namespace sta

// 复用 sta.cpp 中的 LUT 计算与方向推断工具函数（在 namespace sta 中定义）
double get_lut_avg(std::optional<celllib::LookupTable> lut);
double caculate_delay_rise(const celllib::TimingArc arc,
                           const celllib::CellLibrary *lib,
                           double input_slew_rise, double load_cap);
double caculate_delay_fall(const celllib::TimingArc arc,
                           const celllib::CellLibrary *lib,
                           double input_slew_fall, double load_cap);
double caculate_transition_fall(const celllib::TimingArc arc,
                                const celllib::CellLibrary *lib,
                                double input_slew_fall, double load_cap);
double caculate_transition_rise(const celllib::TimingArc arc,
                                const celllib::CellLibrary *lib,
                                double input_slew_rise, double load_cap);
sta::TransitionDirection
speculate_transition_direction(bool is_clock_to_q, celllib::TimingArc arc,
                               sta::TransitionDirection input_direction);

double caculate_setup_fall(const celllib::TimingArc arc,
                           const celllib::CellLibrary *lib, double data_trans,
                           double clk_trans);
double caculate_hold_rise(const celllib::TimingArc arc,
                          const celllib::CellLibrary *lib, double data_trans,
                          double clk_trans);
double caculate_setup_rise(const celllib::TimingArc arc,
                           const celllib::CellLibrary *lib, double data_trans,
                           double clk_trans);
double caculate_hold_fall(const celllib::TimingArc arc,
                          const celllib::CellLibrary *lib, double data_trans,
                          double clk_trans);

#endif // STA_DATA_STRUCTURES_HPP
