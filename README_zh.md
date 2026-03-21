# PBA-STA: 基于路径分析的静态时序分析工具

> 本科毕业设计项目。从零实现一个支持 GBA（Graph-Based Analysis）和 PBA（Path-Based Analysis）的静态时序分析（Static Timing Analysis）引擎，用于验证数字集成电路设计中的时序约束是否满足。

## 项目背景

静态时序分析（STA）是数字集成电路设计流程中不可或缺的一环。它通过计算信号在电路中各条路径上的传播延迟，验证设计是否满足建立时间（Setup）和保持时间（Hold）约束，从而替代耗时的动态仿真。

本项目是一个学习性质的 STA 工具实现，目标是理解 STA 的核心原理和工程实现细节，包括：

- 标准单元库（Liberty）的解析与建模
- 时序图的构建与传播
- 查找表（Lookup Table）的双线性插值
- GBA 与 PBA 两种分析策略的差异与实现
- 时序报告的生成与验证

## 功能特性

### 输入文件支持

| 文件格式 | 说明 | 解析方式 |
|---------|------|---------|
| `.lib` | Liberty 标准单元库 | iEDA Rust Liberty Parser |
| `.v` | 门级网表（Verilog） | Flex / Bison |
| `.sdc` | 时序约束文件 | 自定义解析器 |

### 分析能力

- **GBA（Graph-Based Analysis）**：基于拓扑排序的全图传播，快速获取最坏情况时序
- **PBA（Path-Based Analysis）**：基于 DFS 的逐路径分析，在传播过程中重新计算 slew，获得更精确的逐路径时序结果
- **Setup / Hold 检查**：分别进行建立时间和保持时间验证
- **四类路径组**：REG2REG、IN2REG、REG2OUT、IN2OUT

### 输出

生成 8 类 PT（PrimeTime）格式的时序报告，涵盖 setup/hold × 4 种路径组的全部组合。

## 项目结构

```
proj/
├── CMakeLists.txt                  # CMake 构建配置
├── Makefile                        # 备选 Makefile 构建
├── include/
│   ├── sta/
│   │   ├── sta_data_structures.hpp  # 核心时序数据结构
│   │   └── sta_report.hpp           # 报告生成接口
│   ├── cell/
│   │   ├── cell_data_structure.hpp  # 单元库数据结构
│   │   └── celllib_cache.hpp        # 库文件缓存
│   ├── sdc/
│   │   └── sdc_parser.hpp           # SDC 约束解析
│   ├── parser-verilog/
│   │   ├── verilog_data.hpp         # Verilog AST 数据结构
│   │   └── verilog_driver.hpp       # Flex/Bison 驱动
│   ├── interface/
│   │   ├── verilog_adapter.hpp      # Verilog → STA 适配层
│   │   └── sdc_adapter.hpp          # SDC → STA 适配层
│   └── lib_parser/
│       └── lib_to_celllib_converter.hpp  # Liberty → 内部格式转换
├── src/
│   ├── main.cpp                     # 程序入口
│   ├── sta.cpp                      # 核心时序引擎（图构建、负载计算）
│   ├── lookuptable.cpp              # 查找表插值
│   ├── sta_report.cpp               # 报告生成
│   ├── candidate/                   # PBA 候选路径分析
│   │   ├── candidate.cpp            # PBA DFS 主逻辑
│   │   ├── candidate_recaculate.cpp # 传播中的 slew 重计算
│   │   └── candidate_graphy.cpp     # 候选图构建（处理 non-unate / 时序弧）
│   └── gba_dfs/
│       └── gba_dfs.cpp              # GBA 拓扑排序传播
├── doc/                             # 设计文档
├── lib/                             # Liberty 库文件
├── Testing/                         # 测试用例（~85 个设计）
├── result/                          # 分析结果输出
├── scripts/                         # 辅助脚本（可视化、回归测试）
└── third_party/
    └── liberty-parser/              # iEDA Liberty 解析器
```

## 核心概念与数据结构

### 时序图模型

STA 的核心是一张有向时序图，节点为设计中每个标准单元实例的引脚（Pin），边表示信号的传播关系。

```
TimingPointRef (时序点)
  ├── inst          # 所属实例
  ├── port_name     # 引脚名称
  ├── type          # 类型: INPUT / OUTPUT / CLK_PIN / REGQ / REGD / COMB_PIN
  ├── fanouts       # 扇出时序边列表
  └── load capacitance  # 负载电容 (rise_cap / fall_cap)

TimingEdge (时序边)
  ├── type          # WIRE / COMB_ARC / SEQ_ARC
  ├── from          # 源时序点
  └── target_point  # 目标时序点
```

边的三种类型：
- **WIRE**：线网连接，延迟可忽略或由线负载模型估算
- **COMB_ARC**：组合逻辑弧，从输入引脚到输出引脚的延迟
- **SEQ_ARC**：时序弧，触发器 CLK → Q 的传播延迟

### 单元库建模

```
CellLibrary
  └── StandardCell
      └── Pin (引脚)
          ├── direction     # input / output / inout
          ├── capacitance   # 输入电容
          └── TimingArc (时序弧)
              ├── timing_sense   # unate / non-unate
              ├── cell_rise      # 输出上升延迟 LUT
              ├── cell_fall      # 输出下降延迟 LUT
              ├── rise_transition # 输出上升转换时间 LUT
              └── fall_transition # 输出下降转换时间 LUT
```

延迟和转换时间通过 Liberty 格式的二维查找表（Lookup Table）存储，以输入转换时间（input transition）和输出负载电容（output capacitance）为索引，使用双线性插值获取精确值。

### GBA vs PBA

**GBA（Graph-Based Analysis）**：
- 在每个时序点上只维护最坏/最好的时序值（arrival time、slew）
- 通过拓扑排序依次传播，到达每个节点时取 max（setup）或 min（hold）
- 速度快，但可能过于悲观（pessimistic），因为不同路径上的 slew 可能不同

**PBA（Path-Based Analysis）**：
- 构建候选图（Candidate Graph），通过 DFS 枚举每条时序路径
- 在每条路径上传播时重新计算 slew，避免了 GBA 中"取最坏值合并"带来的悲观性
- 精度更高，但计算量更大

关键区别在于处理"ignorance nodes"（non-unate 单元、CLK→Q 弧等）时，PBA 不提前合并方向，而是在具体路径中确定信号的翻转方向。

## 构建与运行

### 依赖

- C++17 编译器（GCC 9+ / Clang 10+）
- CMake 3.16+
- Flex & Bison（Verilog 解析）
- Rust / Cargo（Liberty 解析器）
- Python 3（回归测试脚本）

### 编译

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 运行

```bash
./pba_sta
```

主程序支持多种测试模式，可在 `src/main.cpp` 中选择：
- `candidate_test`：PBA 完整分析流程
- `auto_test`：自动化批量测试
- `singal_test`：单设计调试

### 回归测试

```bash
python3 run_sta.py              # 运行 iSTA 回归测试
python3 compare_timing_reports.py  # 对比参考结果（PrimeTime / iEDA）
```

## 学习笔记

### STA 的核心流程

```
1. 解析输入
   ├── Liberty → CellLibrary（单元延迟模型）
   ├── Verilog → 门级网表实例列表
   └── SDC → 时钟定义、I/O 约束

2. 构建时序图
   ├── 为每个实例的每个引脚创建 TimingPointRef
   ├── 根据网表连接建立 WIRE 边
   ├── 根据单元库建立 COMB_ARC / SEQ_ARC 边
   └── 用并查集（Union-Find）处理信号等价性

3. 计算负载电容
   └── BFS 遍历，累加扇出引脚的输入电容

4. 时序传播
   ├── GBA: 拓扑排序传播 arrival time 和 slew
   └── PBA: DFS 枚举路径，逐路径传播

5. 时序检查
   ├── Setup: data_arrival_time <= data_required_time ?
   └── Hold:  data_arrival_time >= data_required_time ?

6. 生成报告
   └── PT 格式时序报告（8 类）
```

### 延迟计算：查找表插值

Liberty 中的延迟和转换时间以二维查找表形式给出。给定实际的输入转换时间 $T_{in}$ 和输出负载电容 $C_{out}$，需要在查找表中进行插值：

1. 定位 $(T_{in}, C_{out})$ 在表中所在的网格区间
2. 对四个角点的值进行双线性插值
3. 处理边界情况（超出表范围时使用最近值或外推）

### PBA 中的 slew 重计算

GBA 中，每个节点的 slew 是所有到达该节点的路径中最坏情况的 slew。但在 PBA 中，当沿某条具体路径传播时，slew 应该基于该路径实际的前驱节点来计算，而非取全局最坏值。这就是 PBA 能减少悲观性的核心原因。

## 参考资料

- [iSTA](https://gitee.com/OSCC-Project/iSTA) — 鹏城实验室开源 STA 工具，本项目的参考实现
- Synopsys PrimeTime User Guide — 行业标准 STA 工具文档
- *Static Timing Analysis for Nanometer Designs* — STA 经典教材
- Liberty User Guide — 标准单元库格式规范

## 许可证

本项目仅供学习和研究使用。

第三方依赖 [iEDA Liberty Parser](third_party/liberty-parser/) 遵循 Mulan PSL v2 许可证。
