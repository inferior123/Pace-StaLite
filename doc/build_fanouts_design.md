# `build_fanouts` 设计与重构说明

本文描述时序图构建中 **`STAWorker::build_fanouts`** 的职责划分、数据抽象与阶段流水线，并与「好的设计 / 抽象 / 函数」原则对齐。

## 1. 问题域：这个函数在做什么？

`build_fanouts` 在 **TimingPointRef** 图上建立有向边（`fanouts`），包括：

- **SEQ_ARC**：寄存器时钟 pin → Q（`REGQ`）
- **COMB_ARC**：组合单元输入 pin → 输出 pin
- **WIRE**：同一 `SignalBit`（经 `sigmap` canonical 后）上的 **driver timing point → load timing point**

同时维护 **`bit_to_driver`**：`SignalBit` → 当前认定的 **驱动点**（用于补 WIRE 与多驱动仲裁：例如 `REGQ` 优先于组合输出，避免组合环）。

最后调用 **`build_res_edges`**，把各点的 `fanouts` 摊平为 **`res.edges`**（边表）。

## 2. 核心抽象（概念模型）

| 抽象 | 含义 |
|------|------|
| **FanoutBitDriverMap** (`bit_to_driver`) | 每个 canonical 信号位当前由哪个 **timing point** 驱动；随实例遍历逐步完善。 |
| **FanoutPendingEdge** | 待写入的边 `(from_pt, to_pt, EdgeType)`；先收集再统一去重写入 `fanouts`，避免重复边干扰后续 DFS。 |
| **两趟补线** | 第一趟按实例建点/建弧时，`bit_to_driver` 可能尚未包含「后创建」的 driver；第二趟按 **全点 / 全 SEQ 的 REGD** 再补 **WIRE**。 |

这些类型与阶段函数声明在 **`STAWorker` 私有区**，实现集中在 **`src/timing/timing_graph_builder.cpp`**，避免对外暴露实现细节。

## 3. 流水线（好的「整体结构」）

`build_fanouts()` 本身只做 **编排**（高内聚、易读）：

1. **`fanout_check_preconditions`** — 断言已配置时钟；校验 `CellLibrary` 已设置。
2. **`fanout_seed_clk_input_drivers`** — 用已有 `input_clk_point_ids`（来自 `collect_port` 的顶层输入/时钟点）初始化 `bit_to_driver`。
3. **`fanout_first_pass_instances`** — 遍历所有实例：时序单元走 **`fanout_process_sequential_instance`**，否则走 **`fanout_process_combinational_instance`**。
4. **`fanout_patch_wires_driver_to_loads`** — 对每个有 `bit` 的点，若存在异实例 driver，补 **driver → load** 的 WIRE（跳过同实例内部反馈）。
5. **`fanout_patch_regd_secondary_pass`** — 再扫一遍时序单元输入，补可能漏掉的 **REGD** WIRE（实例顺序导致的首遍遗漏）。
6. **`fanout_wire_primary_outputs`** — 顶层 **OUTPUT** 端口连到对应 net 的 driver。
7. **`fanout_apply_pending_edges`** — 将 pending 边 **去重** 写入各点 `fanouts`。
8. **`build_res_edges`** — 生成 `res.edges`。

**辅助：**

- **`fanout_resolve_sequential_clock_bit`** — 单独封装「CLK pin 接网 → sigmap；否则回退 `cfg.clk_name`」的逻辑与日志/`exit` 行为。
- **`fanout_pending_has`** — 判断 pending 中是否已有同 `(from, to)`，供补线阶段使用。

## 4. 与原则的对照

- **单一职责**：每个 `fanout_*` 函数对应流水线中的一步或一种单元类型，而不是一个「上帝函数」做完所有事。
- **信息隐藏**：调用方只需理解「bit 驱动关系 + pending 边 + 两趟补线」；CLK 解析、REGQ 优先级、non-unate candidate 等细节留在对应步骤内。
- **YAGNI / KISS**：未引入额外策略类或虚接口；当前仅拆分为 **私有方法 + 两个小型 POD 类型**，与现有 `STAWorker` 状态自然契合。
- **可测试性（后续）**：若需要单测，可将纯逻辑（例如 pending 去重、driver 仲裁）进一步抽到 **无状态的自由函数** 并接固定 `TimingRunResult` 夹具；当前改动以可读性与模块边界为先。

## 5. 其它修正

- **前置条件**：`build_fanouts` 入口处由 `assert("...")`（恒为真）改为 **`assert(has_clock && "…")`**，与意图一致。

## 6. 文件索引

| 内容 | 位置 |
|------|------|
| `FanoutPendingEdge` / `FanoutBitDriverMap` 与各 `fanout_*` 声明 | `include/sta/sta_data_structures.hpp`（`STAWorker` 私有区） |
| 实现与 `build_fanouts` 编排 | `src/timing/timing_graph_builder.cpp` |

---

*若后续要继续演进，可考虑：将「pending 收集 + 去重」收拢为小型 `FanoutEdgeBuffer` 类型（仍不强制面向接口），或把时序/组合实例处理移到独立 `.cpp` 以缩短单文件长度。*
