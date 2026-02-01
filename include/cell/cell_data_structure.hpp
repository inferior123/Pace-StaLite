#ifndef CELL_DATA_STRUCTURE_HPP
#define CELL_DATA_STRUCTURE_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

namespace celllib {

// Forward declarations
struct Pin;
struct TimingArc;
struct LookupTable;

/**
 * Pin direction enumeration
 */
enum class PinDirection {
    INPUT,
    OUTPUT,
    INOUT,
    INTERNAL
};

/**
 * Timing sense enumeration
 */
enum class TimingSense {
    POSITIVE_UNATE,    // 正单边（同向）
    NEGATIVE_UNATE,    // 负单边（反向）
    NON_UNATE          // 非单边（双向）
};

/**
 * Timing type enumeration
 */
enum class TimingType {
    COMBINATIONAL,     // 组合逻辑
    SETUP_RISING,      // Setup上升沿
    SETUP_FALLING,     // Setup下降沿
    HOLD_RISING,       // Hold上升沿
    HOLD_FALLING,      // Hold下降沿
    RISING_EDGE,       // 上升沿触发
    FALLING_EDGE,      // 下降沿触发
    CLEAR,             // 清除
    PRESET,            // 预置
    MIN_PULSE_WIDTH    // 最小脉冲宽度
};

/**
 * Lookup Table: 用于存储时序查找表数据
 * 支持2D查找表（index_1 x index_2）
 */
struct LookupTable {
    std::vector<double> index_1;      // 第一个索引（通常是输入转换时间）
    std::vector<double> index_2;      // 第二个索引（通常是输出负载电容）
    std::vector<std::vector<double>> values;  // 查找表的值矩阵
    std::optional<std::string> template_name; // 引用的table_template名称（如 "Timing_7_7", "Hold_3_3"）
    
    /**
     * 查找值（使用最近邻或线性插值）
     * @param idx1 第一个索引值
     * @param idx2 第二个索引值
     * @return 查找得到的值，如果索引超出范围返回nullopt
     */
    std::optional<double> lookup(double idx1, double idx2) const;
    
    /**
     * 查找值（简化版：使用最近邻）
     */
    std::optional<double> lookup_nearest(double idx1, double idx2) const;
    
    LookupTable() = default;
};

/**
 * Timing Arc: 时序弧信息
 * 表示从一个pin到另一个pin的时序关系
 */
struct TimingArc {
    std::string related_pin;          // 相关输入pin名称
    TimingType timing_type;            // 时序类型
    TimingSense timing_sense;          // 时序方向
    
    // 固定延迟值（简化版，当没有查找表时使用）
    std::optional<double> intrinsic_rise;   // 固有上升延迟
    std::optional<double> intrinsic_fall;  // 固有下降延迟
    
    // 查找表（完整版）
    std::optional<LookupTable> cell_rise;      // 单元上升延迟表
    std::optional<LookupTable> cell_fall;     // 单元下降延迟表
    std::optional<LookupTable> rise_transition; // 上升转换时间表
    std::optional<LookupTable> fall_transition; // 下降转换时间表
    
    // Setup/Hold约束查找表
    std::optional<LookupTable> fall_constraint; // 下降约束表（用于setup/hold）
    std::optional<LookupTable> rise_constraint; // 上升约束表（用于setup/hold）
    
    TimingArc() 
        : timing_type(TimingType::COMBINATIONAL)
        , timing_sense(TimingSense::POSITIVE_UNATE) {}
};

/**
 * InternalPower: 内部功耗信息
 */
struct InternalPower {
    std::optional<std::string> when;           // 条件表达式
    std::optional<std::string> related_pin;    // 相关pin
    std::optional<LookupTable> rise_power;     // 上升功耗查找表
    std::optional<LookupTable> fall_power;     // 下降功耗查找表
    
    InternalPower() = default;
};

/**
 * Pin: 引脚信息
 */
struct Pin {
    std::string name;                  // Pin名称
    PinDirection direction;            // 方向
    
    // 电容信息
    std::optional<double> capacitance;      // 总电容
    std::optional<double> rise_capacitance; // 上升电容
    std::optional<double> fall_capacitance; // 下降电容
    std::optional<double> max_capacitance;  // 最大电容
    
    // 功能描述
    std::optional<std::string> function;    // 逻辑功能表达式
    
    // 时钟相关
    bool is_clock;                     // 是否为时钟pin
    
    // 电源相关
    std::optional<std::string> related_power_pin;   // 相关电源pin
    std::optional<std::string> related_ground_pin;   // 相关地pin
    
    // 时序弧列表（对于output pin和input pin）
    std::vector<TimingArc> timing_arcs;
    
    // 内部功耗信息
    std::vector<InternalPower> internal_power;
    
    // Setup/Hold约束（对于input pin，相对于时钟）- 已废弃，使用timing_arcs中的constraint
    std::optional<double> setup_rise;
    std::optional<double> setup_fall;
    std::optional<double> hold_rise;
    std::optional<double> hold_fall;
    
    Pin() 
        : direction(PinDirection::INPUT)
        , is_clock(false) {}
};

/**
 * FFDefinition: 触发器定义（用于时序单元）
 */
struct FFDefinition {
    std::string state_var;             // 状态变量名（如 "IQ"）
    std::string state_var_inv;         // 反相状态变量名（如 "IQN"）
    std::optional<std::string> next_state;    // 下一状态表达式（如 "D"）
    std::optional<std::string> clocked_on;     // 时钟pin（如 "CK"）
    std::optional<std::string> clear;          // 清除信号
    std::optional<std::string> preset;         // 预置信号
    
    FFDefinition() = default;
};

/**
 * LeakagePower: 漏电功耗信息
 */
struct LeakagePower {
    std::optional<std::string> when;   // 条件表达式
    std::optional<double> value;       // 漏电功耗值
    
    LeakagePower() = default;
};

/**
 * PGPin: 电源/地pin信息
 */
struct PGPin {
    std::string name;                  // pin名称（如 "VDD", "VSS"）
    std::optional<std::string> voltage_name;  // 电压名称
    std::optional<std::string> pg_type;        // 类型（primary_power/primary_ground）
    
    PGPin() = default;
    explicit PGPin(const std::string& pin_name) : name(pin_name) {}
};

/**
 * StandardCell: 标准单元
 * 管理一个标准单元的所有信息
 */
struct StandardCell {
    std::string name;                  // 单元名称
    
    // 基本属性
    std::optional<double> area;       // 单元面积
    std::optional<double> drive_strength; // 驱动强度
    std::optional<double> cell_leakage_power; // 单元漏电功耗
    
    // FF定义（时序单元）
    std::optional<FFDefinition> ff;
    
    // 电源/地pin
    std::vector<PGPin> pg_pins;
    
    // 漏电功耗信息
    std::vector<LeakagePower> leakage_power;
    
    // Pin映射：pin名称 -> Pin信息
    std::unordered_map<std::string, Pin> pins;
    
    // 便利访问器
    /**
     * 获取输入pin列表
     */
    std::vector<std::string> get_input_pins() const;
    
    /**
     * 获取输出pin列表
     */
    std::vector<std::string> get_output_pins() const;
    
    /**
     * 获取指定pin
     */
    const Pin* get_pin(const std::string& pin_name) const;
    Pin* get_pin(const std::string& pin_name);
    
    /**
     * 添加pin
     */
    void add_pin(const Pin& pin);
    
    /**
     * 检查是否为时序单元（有clock pin或ff定义）
     */
    bool is_sequential() const;
    
    StandardCell() = default;
    explicit StandardCell(const std::string& cell_name) : name(cell_name) {}
};

/**
 * TableTemplate: 查找表模板定义
 */
struct TableTemplate {
    std::string name;                  // 模板名称（如 "Timing_7_7", "Hold_3_3"）
    std::optional<std::string> variable_1;  // 第一个变量类型
    std::optional<std::string> variable_2;  // 第二个变量类型
    std::vector<double> index_1;       // 第一个索引值
    std::vector<double> index_2;       // 第二个索引值
    
    TableTemplate() = default;
    explicit TableTemplate(const std::string& template_name) : name(template_name) {}
};

/**
 * CellLibrary: 标准单元库
 * 使用unordered_map管理所有标准单元
 */
class CellLibrary {
private:
    std::unordered_map<std::string, StandardCell> cells;
    
    // 库级别属性
    std::string library_name;
    std::string time_unit;             // 时间单位（如 "1ps", "1ns"）
    double capacitance_unit;            // 电容单位（转换为ff）
    
    // 查找表模板
    std::unordered_map<std::string, TableTemplate> table_templates;
    
public:
    CellLibrary() : capacitance_unit(1.0) {}
    
    /**
     * 添加标准单元
     */
    void add_cell(const StandardCell& cell);
    
    /**
     * 获取标准单元
     */
    const StandardCell* get_cell(const std::string& cell_name) const;
    StandardCell* get_cell(const std::string& cell_name);
    
    /**
     * 检查单元是否存在
     */
    bool has_cell(const std::string& cell_name) const;
    
    /**
     * 获取所有单元名称
     */
    std::vector<std::string> get_cell_names() const;
    
    /**
     * 设置库名称
     */
    void set_library_name(const std::string& name) { library_name = name; }
    const std::string& get_library_name() const { return library_name; }
    
    /**
     * 设置时间单位
     */
    void set_time_unit(const std::string& unit) { time_unit = unit; }
    const std::string& get_time_unit() const { return time_unit; }
    
    /**
     * 设置电容单位
     */
    void set_capacitance_unit(double unit) { capacitance_unit = unit; }
    double get_capacitance_unit() const { return capacitance_unit; }
    
    /**
     * 清空库
     */
    void clear();
    
    /**
     * 获取单元数量
     */
    size_t size() const { return cells.size(); }
    
    /**
     * 检查是否为空
     */
    bool empty() const { return cells.empty(); }
    
    /**
     * 添加查找表模板
     */
    void add_table_template(const TableTemplate& templ);
    
    /**
     * 获取查找表模板
     */
    const TableTemplate* get_table_template(const std::string& name) const;
    
    /**
     * 获取所有模板名称
     */
    std::vector<std::string> get_table_template_names() const;

    /**
    * 对 lookuptable 进行插值
    */
    double caculate_lookuptable(const LookupTable& tb, double x0, double y0, const std::string &va_name_1, const std::string &va_name_2) const;
};

} // namespace celllib

#endif // CELL_DATA_STRUCTURE_HPP
