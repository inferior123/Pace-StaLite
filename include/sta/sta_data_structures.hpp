#ifndef STA_DATA_STRUCTURES_HPP
#define STA_DATA_STRUCTURES_HPP

#include "../parser-verilog/verilog_data.hpp"
#include "../cell/cell_data_structure.hpp"
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <functional>
#include <memory>
#include <cstddef>
#include "cassert"


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
    SignalBit(const std::string& name, int offset) 
        : wire_name(name), bit_offset(offset) {}
    
    bool operator==(const SignalBit& other) const {
        return wire_name == other.wire_name && bit_offset == other.bit_offset;
    }

    bool operator!=(const SignalBit& other) const {
        return !operator==(other);
    }
    
    bool operator<(const SignalBit& other) const {
        if (wire_name != other.wire_name)
            return wire_name < other.wire_name;
        return bit_offset < other.bit_offset;
    }
};

// 信号位哈希函数
struct SignalBitHash {
    std::size_t operator()(const SignalBit& bit) const {
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
    
    SignalBit find_root(const SignalBit& bit) const {
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
    void add_connection(const SignalSpec& from, const SignalSpec& to) {
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
    SignalBit find(const SignalBit& bit) const {
        if (parent.find(bit) == parent.end()) {
            return bit;  // 未映射的信号返回自身
        }
        return find_root(bit);
    }
    
    // 应用映射
    SignalBit operator()(const SignalBit& bit) {
        return find(bit);
    }
    
    SignalSpec operator()(const SignalSpec& sig) {
        SignalSpec result;
        for (const auto& bit : sig) {
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
 * 扇出信息：表示一个信号到另一个信号的连接
 */
struct Fanout {
    SignalBit target_bit;      // 目标信号位
    double delay;                 // 延迟值
    std::string port_name;     // 端口名称
    Instance* cell;            // 相关单元实例
    
    Fanout(const SignalBit& target, int d, const std::string& port, Instance* c = nullptr)
        : target_bit(target), delay(d), port_name(port), cell(c) {}
};

/**
 * 单元实例：表示 Verilog 中的模块实例
 */
struct Instance {
    std::string module_name;   // 模块类型名
    std::string instance_name; // 实例名
    
    // 端口连接：端口名 -> 信号
    std::unordered_map<std::string, SignalSpec> connections;
    
    // 负载电容：output_pin -> 电容值(ff)
    std::unordered_map<std::string, double> load_capacitance;
    
    Instance(const std::string& mod, const std::string& inst)
        : module_name(mod), instance_name(inst) {}
};

// ============================================================================
// 4. 时序数据（Timing Data）
// ============================================================================

/**
 * 信号转换方向枚举：用于选择使用哪个查找表（rise/fall）
 */
enum class TransitionDirection {
    RISING,     // 上升沿：0 -> 1，使用 rise_transition / cell_rise
    FALLING,    // 下降沿：1 -> 0，使用 fall_transition / cell_fall
    UNKNOWN     // 未知：如果没有指定，使用默认值（通常取最大值或使用上升沿）
};

/**
 * 信号时序数据：存储每个信号位的时序相关信息
 */
struct SignalTimingData {
    // 驱动信息
    Instance* driver;              // 驱动该信号的单元实例
    std::string driver_port;        // 驱动端口名称
    std::string source_port;        // 源端口名称（用于回溯）
    
    // 扇出列表
    std::vector<Fanout> fanouts;
    
    // 回溯信息（用于关键路径追踪）
    SignalBit backtrack;
    
    // 转换时间（Transition Time/Slew）：信号从0到1或1到0的转换时间
    // 单位：与时间单位一致（通常是ps或ns）
    // 用于作为下一个时序弧的输入转换时间（input_slew）
    // 在每一个时序弧之中同时计算上升和下降的值，然后对时序弧取一个最大值，但是同时记录上升和下降的transition time
    std::optional<double> rise_transition_time;  // 根据 transition_direction 选择的转换时间
    std::optional<double> fall_transition_time;
    
    // 转换方向：用于选择使用哪个查找表（rise_transition/fall_transition 或 cell_rise/cell_fall）
    // 如果没有指定，使用默认值（UNKNOWN，在计算时取最大值或使用上升沿）
    TransitionDirection transition_direction;
    
    SignalTimingData() 
        : driver(nullptr), 
          rise_transition_time(std::nullopt),
          fall_transition_time(std::nullopt),
          transition_direction(TransitionDirection::UNKNOWN) {}
};

/**
 * 时序端点：表示时序约束的端点（通常是寄存器的时钟/数据输入，或顶层输出）
 * 因 sigmap 合并，同一 canonical 可能既是顶层输出又是某 reg 的 D 端，需同时保留。
 */
struct TimingEndpoint {
    Instance* sink;                 // 接收信号的单元实例, 若为none，则为top module的output
    std::string port;               // 端口名称（sink 的端口或顶层输出端口名）
    std::optional<std::string> primary_output_port;  // 若该 net 同时为顶层输出，保留其端口名（如 "out"）
    std::optional<int> Setup_req;
    std::optional<int> Hold_req;
    
    TimingEndpoint() : sink(nullptr) {}
    TimingEndpoint(Instance* s, const std::string& p)
        : sink(s), port(p) {}
};

// ============================================================================
// 5. 单元时序信息（Cell Timing Information）
// ============================================================================

/**
 * 单元时序信息：存储标准单元库的时序信息
 */
struct CellTiming {
    // 组合逻辑延迟：(源端口, 目标端口) -> 延迟
    struct PortPair {
        std::string src;
        std::string dst;
        
        bool operator==(const PortPair& other) const {
            return src == other.src && dst == other.dst;
        }
    };
    
    struct PortPairHash {
        std::size_t operator()(const PortPair& p) const {
            return std::hash<std::string>{}(p.src) ^ 
                   (std::hash<std::string>{}(p.dst) << 1);
        }
    };
    
    std::unordered_map<PortPair, int, PortPairHash> comb_delays;
    
    // 时序延迟：输出端口 -> (延迟, 时钟端口)
    std::unordered_map<std::string, std::pair<int, std::string>> arrival_times;
    
    // 时序约束：输入端口 -> (约束值, 时钟端口)
    std::unordered_map<std::string, std::pair<int, std::string>> required_times;
    
    bool has_inputs;
    
    CellTiming() : has_inputs(false) {}
};

// ============================================================================
// 6. 关键路径（Critical Path）
// ============================================================================

/**
 * 关键路径节点
 */
struct CriticalPathNode {
    SignalBit signal;
    int arrival_time;
    Instance* cell;
    std::string cell_port;
    
    CriticalPathNode(const SignalBit& sig, int arrival, Instance* c = nullptr, 
                     const std::string& port = "")
        : signal(sig), arrival_time(arrival), cell(c), cell_port(port) {}
};

using CriticalPath = std::vector<CriticalPathNode>;

// ============================================================================
// 7. STA 工作器（STA Worker）
// ============================================================================

/**
 * STA 工作器：执行静态时序分析的核心类
 */
class STAWorker {
private:
    // 核心数据结构
    SignalMap sigmap;
    std::unordered_map<SignalBit, SignalTimingData, SignalBitHash> timing_data;
    std::unordered_map<SignalBit, TimingEndpoint, SignalBitHash> endpoints;
    std::deque<SignalBit> timing_queue;
    std::unordered_set<SignalBit, SignalBitHash> driven_signals;
    
    // 顶层模块端口信息
    std::unordered_set<SignalBit, SignalBitHash> top_module_inputs;  // 顶层模块的输入端口

    std::unordered_map<std::string, std::vector<SignalBit>> signal_registry;
    
    // Arrival time for each signal bit
    std::unordered_map<SignalBit, int, SignalBitHash> arrival_time;
    
    // 分析结果
    int max_arrival_time;
    SignalBit critical_signal;
    CriticalPath critical_path;
    
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
    
public:
    /**
     * 仿真颗粒度：控制时序分析的精度级别
     */
    enum class AnalysisGranularity {
        COARSE,     // 粗略模式：使用固定延迟，使用一个查找表之中的悲观值，不考虑负载电容和转换时间
        MEDIUM,     // 中等模式：考虑负载电容，使用完成查找表插值
        FINE        // 精确模式：后续版本再考虑实现，遇到直接assert
    };
    
private:
    AnalysisGranularity analysis_granularity_ = AnalysisGranularity::MEDIUM;
    
public:
    STAWorker() : max_arrival_time(0) {}

    explicit STAWorker(const celllib::CellLibrary& lib) 
        : max_arrival_time(0), cell_library_(&lib) {}
    
    // 仿真颗粒度访问器
    AnalysisGranularity get_analysis_granularity() const { return analysis_granularity_; }
    void set_analysis_granularity(AnalysisGranularity granularity) { 
        analysis_granularity_ = granularity; 
    }

    // 和verilog parser相耦合的函数
    void collect_net(verilog::Net &net); 
    void collect_port(verilog::Port &port);
    void collect_assign(verilog::Assignment &assign);
    void collect_instance(verilog::Instance &inst);
    
    // 核心工作函数
    void build_fanouts();
    void calculate_load_capacitance();
    void calculate_timing_arcs();
    void run();
    void sta_check(int clock_period);

    SignalSpec get_signal_bits(const std::string &signame) const;

    SignalSpec convert_to_signalspec(const verilog::RHS &rhs);
    SignalSpec convert_to_signalspec(const verilog::LHS &lhs);

    // top module name
    std::string top_moudle;
    
    // 配置访问器
    const sta_config& get_config() const { return cfg; }
    sta_config& get_config() { return cfg; }
    
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

    void set_cell_library(const celllib::CellLibrary& lib) {
        cell_library_ = &lib;
    }

    bool has_cell_library() const { return cell_library_ != nullptr; }

    const celllib::CellLibrary* get_cell_library() const { return cell_library_; }
    
    bool is_reg(std::string name);
    
    /**
     * 检查信号是否是顶层模块的输入端口
     * @param bit 要检查的信号位（可以是原始信号或规范代表）
     * @return 如果是顶层模块输入则返回true，否则返回false
     */
    bool is_top_module_input(const SignalBit& bit) const {
        SignalBit canonical = sigmap.find(bit);
        return top_module_inputs.count(canonical) > 0;
    }

    // 用于外部访问的接口
    const std::vector<std::unique_ptr<Instance>>& get_instances() const { return instances; }
    const std::unordered_map<std::string, std::vector<SignalBit>>& get_signal_registry() const { return signal_registry; }
    const std::unordered_map<SignalBit, SignalTimingData, SignalBitHash>& get_timing_data() const { return timing_data; }
    const std::unordered_map<SignalBit, TimingEndpoint, SignalBitHash>& get_endpoints() const { return endpoints; }
    const std::unordered_set<SignalBit, SignalBitHash>& get_driven_signals() const { return driven_signals; }
    const std::deque<SignalBit>& get_timing_queue() const { return timing_queue; }
    SignalBit get_canonical_signal(const SignalBit& bit) const { return sigmap.find(bit); }
    
    // 用于调试的访问器
    const std::unordered_map<SignalBit, int, SignalBitHash>& get_arrival_time() const { return arrival_time; }
    int get_max_arrival_time() const { return max_arrival_time; }
    SignalBit get_critical_signal() const { return critical_signal; }

private:
    // 内部辅助函数
    void propagate_timing(const SignalBit& bit);
    void trace_critical_path();
    void trace_path(const SignalBit& endpoint_bit);  // 回溯并打印路径
    SignalBit create_signal_bit(const std::string& name, int offset);
};

} // namespace sta

#endif // STA_DATA_STRUCTURES_HPP
