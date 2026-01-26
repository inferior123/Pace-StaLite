# 静态时序分析（STA）数据结构规划

基于 Yosys 的实现经验，为 PBA-STA 原型工具设计的数据结构规划。

## 1. 核心数据结构概览

### 1.1 信号表示（Signal Representation）

```cpp
// 信号位（Signal Bit）表示
struct SignalBit {
    std::string wire_name;  // 线网名称
    int bit_offset;         // 位偏移（对于多比特信号）
    
    // 用于哈希和比较
    bool operator==(const SignalBit& other) const;
    size_t hash() const;
};

// 信号集合（Signal Spec）
using SignalSpec = std::vector<SignalBit>;
```

**设计要点：**
- 每个信号位由 `(wire_name, bit_offset)` 唯一标识
- 支持单比特和多比特信号
- 需要实现哈希函数用于字典查找

### 1.2 信号映射（Signal Mapping）

```cpp
// 信号映射表 - 将连接的信号映射到规范代表
// 使用 Union-Find 数据结构
class SignalMap {
private:
    // Union-Find 数据结构
    std::unordered_map<SignalBit, SignalBit> parent;
    
public:
    // 添加连接关系
    void add_connection(const SignalSpec& from, const SignalSpec& to);
    
    // 查找信号的规范代表
    SignalBit find(const SignalBit& bit);
    
    // 应用映射到信号
    SignalBit operator()(const SignalBit& bit);
    SignalSpec operator()(const SignalSpec& sig);
};
```

**设计要点：**
- 使用 Union-Find 算法处理信号连接
- 将物理上连接的所有信号位映射到同一个规范代表
- 简化时序分析中的信号查找

### 1.3 时序数据（Timing Data）

```cpp
// 每个信号位的时序信息
struct SignalTimingData {
    // 驱动信息
    Instance* driver;              // 驱动该信号的单元实例
    std::string driver_port;        // 驱动端口名称
    std::string source_port;        // 源端口名称（用于回溯）
    
    // 扇出信息：目标信号位、延迟、端口名
    struct Fanout {
        SignalBit target_bit;
        int delay;                  // 延迟值
        std::string port_name;      // 端口名称
    };
    std::vector<Fanout> fanouts;   // 所有扇出
    
    // 回溯信息（用于关键路径追踪）
    SignalBit backtrack;            // 前一个信号位
    
    SignalTimingData() : driver(nullptr) {}
};
```

**设计要点：**
- 存储每个信号位的驱动和扇出信息
- 支持延迟传播和关键路径回溯
- 使用 `vector` 存储扇出列表（通常扇出数量不大）

### 1.4 端点信息（Endpoint Information）

```cpp
// 时序端点（Timing Endpoint）
struct TimingEndpoint {
    Instance* sink;                 // 接收信号的单元实例, 若这个部分为nullptr这说明这个是top module的output
    std::string port;               // 端口名称
    int required_time;              // 所需时间（setup/hold constraint）
    
    TimingEndpoint() : sink(nullptr), required_time(0) {}
};
```

**设计要点：**
- 端点通常是寄存器的时钟输入或数据输入
- `required_time` 表示 setup/hold 约束
- 用于计算时序裕量（slack）

## 2. 主要数据结构容器

### 2.1 时序数据字典

```cpp
// 信号位 -> 时序数据
std::unordered_map<SignalBit, SignalTimingData> timing_data;

// 信号位 -> 端点信息
std::unordered_map<SignalBit, TimingEndpoint> endpoints;
```

**设计要点：**
- 使用 `unordered_map` 实现 O(1) 查找
- 需要为 `SignalBit` 实现哈希函数

### 2.2 拓扑排序队列

```cpp
// 用于拓扑排序的队列
std::deque<SignalBit> timing_queue;

// 已驱动的信号集合（用于验证）
std::unordered_set<SignalBit> driven_signals;
```

**设计要点：**
- 使用 `deque` 支持前端和后端操作
- 用于实现广度优先的时序传播

### 2.3 单元时序信息（Cell Timing Information）

```cpp
// 单元时序弧（Timing Arc）
struct TimingArc {
    std::string src_port;          // 源端口
    std::string dst_port;           // 目标端口
    int delay;                      // 延迟值
};

// 单元时序信息
struct CellTiming {
    // 组合逻辑延迟：输入端口 -> 输出端口
    std::unordered_map<std::pair<std::string, std::string>, int> comb_delays;
    
    // 时序延迟：时钟端口 -> 数据端口（arrival time）
    std::unordered_map<std::string, std::pair<int, std::string>> arrival_times;
    
    // 时序约束：数据端口 -> 时钟端口（required time）
    std::unordered_map<std::string, std::pair<int, std::string>> required_times;
    
    bool has_inputs;                // 是否有输入端口
};

// 单元类型 -> 时序信息
std::unordered_map<std::string, CellTiming> cell_timing_library;
```

**设计要点：**
- 存储标准单元库的时序信息
- 支持组合逻辑延迟和时序约束
- 可以从 Liberty 文件或手动定义加载

## 3. 与现有 Verilog 解析器的集成

### 3.1 从 Verilog 数据到 STA 数据结构的转换

```cpp
class STAEngine {
private:
    // 从 verilog::Instance 构建时序图
    void build_timing_graph(const verilog::Instance& inst);
    
    // 从 verilog::Net 创建 SignalBit
    SignalBit create_signal_bit(const verilog::Net& net, int bit_offset);
    
    // 从 verilog::Port 创建端点
    void create_endpoints(const verilog::Port& port);
    
public:
    // 从 Verilog 解析结果构建 STA 数据结构
    void build_from_verilog(const std::vector<verilog::Instance>& instances,
                           const std::vector<verilog::Net>& nets,
                           const std::vector<verilog::Port>& ports);
};
```

### 3.2 数据结构映射关系

```
Verilog 解析器输出          ->  STA 数据结构
─────────────────────────────────────────────
verilog::Net               ->  SignalBit
verilog::Instance          ->  Cell/Instance (驱动信息)
verilog::Port (input)      ->  时序起点（arrival = 0）
verilog::Port (output)     ->  时序端点
verilog::Assignment        ->  组合逻辑连接
```

## 4. 时序分析算法所需的数据结构

### 4.1 到达时间（Arrival Time）存储

```cpp
// 方案1：存储在信号位属性中（类似 Yosys）
// 每个 Wire 对象存储 arrival time 数组
struct Wire {
    std::string name;
    int width;
    std::vector<int> arrival_times;  // 每个位的到达时间
};

// 方案2：使用独立的数据结构
std::unordered_map<SignalBit, int> arrival_times;
```

**推荐方案1**：与信号位紧密耦合，便于访问和更新

### 4.2 关键路径追踪

```cpp
// 关键路径节点
struct CriticalPathNode {
    SignalBit signal;
    int arrival_time;
    Instance* cell;                 // 相关单元
    std::string cell_port;          // 相关端口
};

// 关键路径
using CriticalPath = std::vector<CriticalPathNode>;
```

## 5. 完整的数据结构组织

```cpp
class STAWorker {
private:
    // 核心数据结构
    SignalMap sigmap;                                    // 信号映射
    std::unordered_map<SignalBit, SignalTimingData> data;  // 时序数据
    std::unordered_map<SignalBit, TimingEndpoint> endpoints; // 端点
    std::deque<SignalBit> queue;                        // 拓扑排序队列
    std::unordered_set<SignalBit> driven;              // 已驱动信号
    
    // 单元时序库
    std::unordered_map<std::string, CellTiming> timing_lib;
    
    // 分析结果
    int max_arrival_time;
    SignalBit critical_signal;
    CriticalPath critical_path;
    
public:
    // 构建时序图
    void build_timing_graph(/* verilog data */);
    
    // 执行时序分析
    void run_analysis();
    
    // 报告结果
    void report_timing();
};
```

## 6. 数据结构选择建议

### 6.1 哈希表 vs 有序映射

| 用途 | 推荐 | 原因 |
|------|------|------|
| 信号位查找 | `unordered_map` | O(1) 查找，不需要排序 |
| 时序数据存储 | `unordered_map` | 频繁查找，不需要有序 |
| 端点管理 | `unordered_map` | 快速查找端点信息 |

### 6.2 容器选择

- **`std::vector`**: 扇出列表（通常数量不大，顺序不重要）
- **`std::deque`**: 拓扑排序队列（需要前后端操作）
- **`std::unordered_set`**: 已驱动信号集合（快速查找）
- **`std::unordered_map`**: 各种字典（快速查找）

## 7. 性能考虑

1. **内存效率**：
   - 使用 `unordered_map` 而非 `map`（避免排序开销）
   - 信号位使用引用或指针避免复制

2. **查找效率**：
   - 为 `SignalBit` 实现高效的哈希函数
   - 使用 `reserve()` 预分配容器空间

3. **算法效率**：
   - 拓扑排序使用队列避免递归
   - 延迟传播时只更新变化的信号

## 8. 扩展性考虑

1. **支持上升/下降沿**：
   ```cpp
   struct SignalTimingData {
       int arrival_rise;
       int arrival_fall;
       // ...
   };
   ```

2. **支持多时钟域**：
   ```cpp
   std::unordered_map<std::string, /* clock domain name */, 
                     std::unordered_map<SignalBit, SignalTimingData>> timing_data_by_clock;
   ```

3. **支持时序约束文件（SDC）**：
   ```cpp
   struct TimingConstraints {
       std::vector<ClockConstraint> clocks;
       std::vector<PathConstraint> paths;
       // ...
   };
   ```

## 9. 实现优先级

### Phase 1: 基础数据结构
1. `SignalBit` 和 `SignalSpec`
2. `SignalMap`（简化版 Union-Find）
3. `SignalTimingData` 和 `TimingEndpoint`

### Phase 2: 时序图构建
1. 从 Verilog 数据构建时序图
2. 单元时序库加载
3. 信号映射建立

### Phase 3: 时序分析算法
1. 拓扑排序
2. 延迟传播
3. 关键路径追踪

### Phase 4: 扩展功能
1. 上升/下降沿支持
2. 多时钟域
3. 时序约束文件支持

## 10. 参考实现

参考 Yosys 中的实现：
- `yosys/kernel/sigtools.h`: `SigMap` 实现
- `yosys/passes/cmds/sta.cc`: `StaWorker` 数据结构
- `yosys/kernel/timinginfo.h`: `TimingInfo` 实现

这些实现已经过充分测试，可以作为设计参考。
