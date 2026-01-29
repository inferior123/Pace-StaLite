# Cell Data Structure Documentation

## 概述

`cell_data_structure.hpp` 定义了用于存储和操作标准单元库（Liberty格式）数据的核心数据结构。这些数据结构位于 `celllib` 命名空间中，与现有的 `sta` 数据结构分离，提供了完整的标准单元库信息表示。

## 命名空间

所有数据结构都在 `celllib` 命名空间中：

```cpp
namespace celllib {
    // 所有数据结构定义
}
```

## 核心数据结构

### 1. 枚举类型

#### PinDirection
引脚方向枚举：
- `INPUT` - 输入引脚
- `OUTPUT` - 输出引脚
- `INOUT` - 双向引脚
- `INTERNAL` - 内部引脚

#### TimingSense
时序方向枚举：
- `POSITIVE_UNATE` - 正单边（同向）
- `NEGATIVE_UNATE` - 负单边（反向）
- `NON_UNATE` - 非单边（双向）

#### TimingType
时序类型枚举：
- `COMBINATIONAL` - 组合逻辑
- `SETUP_RISING` - Setup上升沿
- `SETUP_FALLING` - Setup下降沿
- `HOLD_RISING` - Hold上升沿
- `HOLD_FALLING` - Hold下降沿
- `RISING_EDGE` - 上升沿触发
- `FALLING_EDGE` - 下降沿触发
- `CLEAR` - 清除
- `PRESET` - 预置
- `MIN_PULSE_WIDTH` - 最小脉冲宽度

### 2. LookupTable（查找表）

用于存储时序查找表数据，支持2D查找表（index_1 x index_2）。

**成员变量：**
- `std::vector<double> index_1` - 第一个索引（通常是输入转换时间）
- `std::vector<double> index_2` - 第二个索引（通常是输出负载电容）
- `std::vector<std::vector<double>> values` - 查找表的值矩阵
- `std::optional<std::string> template_name` - 引用的table_template名称（如 "Timing_7_7", "Hold_3_3"）

**方法：**
- `lookup(double idx1, double idx2)` - 查找值（使用最近邻或线性插值）
- `lookup_nearest(double idx1, double idx2)` - 查找值（简化版：使用最近邻）

**示例：**
```cpp
LookupTable lut;
lut.index_1 = {0.001, 0.002, 0.003};
lut.index_2 = {0.1, 0.2, 0.3};
lut.values = {{1.0, 1.1, 1.2}, {1.5, 1.6, 1.7}, {2.0, 2.1, 2.2}};
lut.template_name = "Timing_7_7";

auto value = lut.lookup(0.0015, 0.15); // 查找值
```

### 3. TimingArc（时序弧）

表示从一个pin到另一个pin的时序关系。

**成员变量：**
- `std::string related_pin` - 相关输入pin名称
- `TimingType timing_type` - 时序类型
- `TimingSense timing_sense` - 时序方向
- `std::optional<double> intrinsic_rise` - 固有上升延迟
- `std::optional<double> intrinsic_fall` - 固有下降延迟
- `std::optional<LookupTable> cell_rise` - 单元上升延迟表
- `std::optional<LookupTable> cell_fall` - 单元下降延迟表
- `std::optional<LookupTable> rise_transition` - 上升转换时间表
- `std::optional<LookupTable> fall_transition` - 下降转换时间表
- `std::optional<LookupTable> fall_constraint` - 下降约束表（用于setup/hold）
- `std::optional<LookupTable> rise_constraint` - 上升约束表（用于setup/hold）

**示例：**
```cpp
TimingArc arc;
arc.related_pin = "A1";
arc.timing_type = TimingType::COMBINATIONAL;
arc.timing_sense = TimingSense::POSITIVE_UNATE;
arc.cell_rise = lut; // 设置查找表
```

### 4. InternalPower（内部功耗）

存储内部功耗信息。

**成员变量：**
- `std::optional<std::string> when` - 条件表达式
- `std::optional<std::string> related_pin` - 相关pin
- `std::optional<LookupTable> rise_power` - 上升功耗查找表
- `std::optional<LookupTable> fall_power` - 下降功耗查找表

### 5. Pin（引脚）

存储引脚的所有信息。

**成员变量：**
- `std::string name` - Pin名称
- `PinDirection direction` - 方向
- `std::optional<double> capacitance` - 总电容
- `std::optional<double> rise_capacitance` - 上升电容
- `std::optional<double> fall_capacitance` - 下降电容
- `std::optional<double> max_capacitance` - 最大电容
- `std::optional<std::string> function` - 逻辑功能表达式
- `bool is_clock` - 是否为时钟pin
- `std::optional<std::string> related_power_pin` - 相关电源pin
- `std::optional<std::string> related_ground_pin` - 相关地pin
- `std::vector<TimingArc> timing_arcs` - 时序弧列表
- `std::vector<InternalPower> internal_power` - 内部功耗信息

**方法：**
- 通过 `StandardCell::get_pin()` 访问

### 6. FFDefinition（触发器定义）

用于时序单元，定义触发器的行为。

**成员变量：**
- `std::string state_var` - 状态变量名（如 "IQ"）
- `std::string state_var_inv` - 反相状态变量名（如 "IQN"）
- `std::optional<std::string> next_state` - 下一状态表达式（如 "D"）
- `std::optional<std::string> clocked_on` - 时钟pin（如 "CK"）
- `std::optional<std::string> clear` - 清除信号
- `std::optional<std::string> preset` - 预置信号

**示例：**
```cpp
FFDefinition ff;
ff.state_var = "IQ";
ff.state_var_inv = "IQN";
ff.next_state = "D";
ff.clocked_on = "CK";
```

### 7. LeakagePower（漏电功耗）

存储漏电功耗信息。

**成员变量：**
- `std::optional<std::string> when` - 条件表达式
- `std::optional<double> value` - 漏电功耗值

### 8. PGPin（电源/地pin）

存储电源/地pin信息。

**成员变量：**
- `std::string name` - pin名称（如 "VDD", "VSS"）
- `std::optional<std::string> voltage_name` - 电压名称
- `std::optional<std::string> pg_type` - 类型（primary_power/primary_ground）

### 9. StandardCell（标准单元）

管理一个标准单元的所有信息。

**成员变量：**
- `std::string name` - 单元名称
- `std::optional<double> area` - 单元面积
- `std::optional<double> drive_strength` - 驱动强度
- `std::optional<double> cell_leakage_power` - 单元漏电功耗
- `std::optional<FFDefinition> ff` - FF定义（时序单元）
- `std::vector<PGPin> pg_pins` - 电源/地pin列表
- `std::vector<LeakagePower> leakage_power` - 漏电功耗信息列表
- `std::unordered_map<std::string, Pin> pins` - Pin映射：pin名称 -> Pin信息

**方法：**
- `get_input_pins()` - 获取输入pin列表
- `get_output_pins()` - 获取输出pin列表
- `get_pin(const std::string& pin_name)` - 获取指定pin
- `add_pin(const Pin& pin)` - 添加pin
- `is_sequential()` - 检查是否为时序单元（有clock pin或ff定义）

**示例：**
```cpp
StandardCell cell("AND2_X1");
cell.area = 1.0;
cell.drive_strength = 1;

Pin pin_a;
pin_a.name = "a";
pin_a.direction = PinDirection::INPUT;
pin_a.capacitance = 0.9;
cell.add_pin(pin_a);

auto input_pins = cell.get_input_pins(); // 获取所有输入pin
const Pin* pin = cell.get_pin("a"); // 查找pin
```

### 10. TableTemplate（查找表模板）

存储查找表模板定义，用于定义查找表的索引变量和索引值。

**成员变量：**
- `std::string name` - 模板名称（如 "Timing_7_7", "Hold_3_3"）
- `std::optional<std::string> variable_1` - 第一个变量类型（如 "input_net_transition"）
- `std::optional<std::string> variable_2` - 第二个变量类型（如 "total_output_net_capacitance"）
- `std::vector<double> index_1` - 第一个索引值
- `std::vector<double> index_2` - 第二个索引值

**示例：**
```cpp
TableTemplate templ("Timing_7_7");
templ.variable_1 = "input_net_transition";
templ.variable_2 = "total_output_net_capacitance";
templ.index_1 = {0.001, 0.002, 0.003, 0.004, 0.005, 0.006, 0.007};
templ.index_2 = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7};
```

### 11. CellLibrary（标准单元库）

使用 `unordered_map` 管理所有标准单元和查找表模板。

**私有成员：**
- `std::unordered_map<std::string, StandardCell> cells` - 单元映射
- `std::string library_name` - 库名称
- `std::string time_unit` - 时间单位（如 "1ps", "1ns"）
- `double capacitance_unit` - 电容单位（转换为ff）
- `std::unordered_map<std::string, TableTemplate> table_templates` - 查找表模板映射

**公共方法：**

**单元管理：**
- `add_cell(const StandardCell& cell)` - 添加标准单元
- `get_cell(const std::string& cell_name)` - 获取标准单元（const和非const版本）
- `has_cell(const std::string& cell_name)` - 检查单元是否存在
- `get_cell_names()` - 获取所有单元名称
- `size()` - 获取单元数量
- `empty()` - 检查是否为空

**库属性：**
- `set_library_name(const std::string& name)` / `get_library_name()` - 设置/获取库名称
- `set_time_unit(const std::string& unit)` / `get_time_unit()` - 设置/获取时间单位
- `set_capacitance_unit(double unit)` / `get_capacitance_unit()` - 设置/获取电容单位

**查找表模板管理：**
- `add_table_template(const TableTemplate& templ)` - 添加查找表模板
- `get_table_template(const std::string& name)` - 获取查找表模板
- `get_table_template_names()` - 获取所有模板名称

**其他：**
- `clear()` - 清空库

**示例：**
```cpp
CellLibrary library;
library.set_library_name("NangateOpenCellLibrary_fast");
library.set_time_unit("1ps");
library.set_capacitance_unit(1.0);

// 添加单元
StandardCell cell("AND2_X1");
library.add_cell(cell);

// 查找单元
const StandardCell* found_cell = library.get_cell("AND2_X1");
if (found_cell) {
    // 使用单元
}

// 添加查找表模板
TableTemplate templ("Timing_7_7");
library.add_table_template(templ);

// 查找模板
const TableTemplate* found_templ = library.get_table_template("Timing_7_7");
if (found_templ) {
    // 使用模板
}
```

## 数据结构关系图

```
CellLibrary
├── table_templates: unordered_map<string, TableTemplate>
│   └── TableTemplate
│       ├── name
│       ├── variable_1, variable_2
│       └── index_1, index_2
│
└── cells: unordered_map<string, StandardCell>
    └── StandardCell
        ├── name, area, drive_strength, cell_leakage_power
        ├── ff: optional<FFDefinition>
        │   └── state_var, state_var_inv, next_state, clocked_on
        ├── pg_pins: vector<PGPin>
        │   └── name, voltage_name, pg_type
        ├── leakage_power: vector<LeakagePower>
        │   └── when, value
        └── pins: unordered_map<string, Pin>
            └── Pin
                ├── name, direction, capacitance, function, is_clock
                ├── timing_arcs: vector<TimingArc>
                │   └── TimingArc
                │       ├── related_pin, timing_type, timing_sense
                │       └── LookupTable (cell_rise, cell_fall, etc.)
                │           ├── index_1, index_2, values
                │           └── template_name -> TableTemplate
                └── internal_power: vector<InternalPower>
                    └── when, related_pin, rise_power, fall_power
```

## 使用示例

### 解析Liberty文件

```cpp
#include "cell/liberty_parser.hpp"

celllib::LibertyParser parser("path/to/library.lib");
celllib::CellLibrary library = parser.parse();

if (!parser.is_valid()) {
    std::cerr << "Parse error: " << parser.get_error() << std::endl;
    return;
}
```

### 访问单元信息

```cpp
// 获取单元
const celllib::StandardCell* cell = library.get_cell("AND2_X1");
if (!cell) {
    std::cerr << "Cell not found" << std::endl;
    return;
}

// 访问单元属性
std::cout << "Cell: " << cell->name << std::endl;
if (cell->area.has_value()) {
    std::cout << "Area: " << cell->area.value() << std::endl;
}

// 获取输入pin
auto input_pins = cell->get_input_pins();
for (const auto& pin_name : input_pins) {
    const celllib::Pin* pin = cell->get_pin(pin_name);
    if (pin && pin->capacitance.has_value()) {
        std::cout << "Pin " << pin_name << " capacitance: " 
                  << pin->capacitance.value() << std::endl;
    }
}
```

### 查找时序信息

```cpp
// 获取输出pin的timing arcs
const celllib::Pin* output_pin = cell->get_pin("o");
if (output_pin) {
    for (const auto& arc : output_pin->timing_arcs) {
        std::cout << "Timing arc from " << arc.related_pin << std::endl;
        
        // 使用查找表计算延迟
        if (arc.cell_rise.has_value()) {
            const auto& lut = arc.cell_rise.value();
            // 查找表引用template
            if (lut.template_name.has_value()) {
                const auto* templ = library.get_table_template(lut.template_name.value());
                if (templ) {
                    std::cout << "Using template: " << templ->name << std::endl;
                }
            }
            
            // 查找延迟值
            double input_transition = 0.01;
            double output_load = 0.5;
            auto delay = lut.lookup(input_transition, output_load);
            if (delay.has_value()) {
                std::cout << "Delay: " << delay.value() << std::endl;
            }
        }
    }
}
```

### 访问查找表模板

```cpp
// 获取所有模板
auto template_names = library.get_table_template_names();
for (const auto& name : template_names) {
    const auto* templ = library.get_table_template(name);
    if (templ) {
        std::cout << "Template: " << name << std::endl;
        std::cout << "  Variable 1: " << templ->variable_1.value_or("N/A") << std::endl;
        std::cout << "  Variable 2: " << templ->variable_2.value_or("N/A") << std::endl;
        std::cout << "  Index 1 size: " << templ->index_1.size() << std::endl;
        std::cout << "  Index 2 size: " << templ->index_2.size() << std::endl;
    }
}
```

## 设计特点

1. **完整性**：数据结构存储了Liberty文件中的所有信息，包括：
   - 基本属性（area, drive_strength等）
   - 时序信息（timing arcs, constraints）
   - 功耗信息（leakage power, internal power）
   - 查找表模板（table templates）

2. **可扩展性**：使用 `optional` 和 `vector` 支持可选和多个值，便于扩展

3. **查找效率**：使用 `unordered_map` 实现O(1)查找

4. **模板关联**：`LookupTable` 中的 `template_name` 字段可以关联到 `TableTemplate`，便于理解查找表的含义

5. **命名空间隔离**：所有数据结构在 `celllib` 命名空间中，与 `sta` 数据结构分离

## 注意事项

1. **内存管理**：所有数据结构使用值语义，不需要手动管理内存

2. **可选字段**：大部分字段使用 `std::optional`，访问前需要检查 `has_value()`

3. **查找表查找**：当前 `lookup()` 方法使用最近邻查找，未来可以实现线性插值以提高精度

4. **时序单元识别**：`is_sequential()` 方法检查clock pin，也可以检查 `ff` 定义

5. **模板引用**：`LookupTable` 的 `template_name` 字段存储模板名称，可以通过 `CellLibrary::get_table_template()` 查找对应的模板定义
