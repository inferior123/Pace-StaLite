# `build_gba_graphy` 设计与重构说明

本文描述 **GBA（Graph-Based Analysis）图构建** 中 `STAWorker::build_gba_graphy` 的职责划分、数据抽象与阶段流水线，并与「好的设计 / 抽象 / 函数」原则对齐。风格上与 [`build_fanouts_design.md`](build_fanouts_design.md) 保持一致。

## 1. 问题域：这个入口在做什么？

`build_gba_graphy` 在 **`res.points` 与各点 `fanouts`** 构成的有向图上：

1. 为每个 timing point 分配一个 **`GbaNode`**，并维护 **`pt_to_node`** 映射。
2. 对 point 图做 **Kahn 拓扑排序**，结果写入 **`gba_graphy_.topo_order`**，供后续 `run_gba_propagate` **复用**（只做路径选择、不重算 segment）。
3. 若存在 **组合环**，按策略删除环上 **WIRE** 边或强制将剩余点追加进拓扑序（与原先行为一致）。
4. 将 **INPUT / CLK** 对应点（`input_clk_point_ids`）设为 **零延迟、零转换时间** 的传播源。
5. 按拓扑序 **前向 DP**：对每条 fanout 调用 **`segment_delays_slews_gba`**，写入 **`GbaPath`**（挂在前驱 `fanouts` 上），并更新后继节点的 **delay/slew/prev**（MAX/MIN 由 `analysis_mode` 决定）。
6. 标记 **OUTPUT / REGD** 点为 **`end_node`**，供 `run_gba_timing_analysis` 回溯。

命名 **`graphy`** 为历史沿用（与 `GbaGraphy`、`candidate_graphy_` 等一致）；本文不强制改名，以免牵动大量符号。

## 2. 核心抽象（概念模型）

| 抽象 | 含义 |
|------|------|
| **`GbaGraphy`** | GBA 专用子图：节点表、路径表、`pt_to_node`、`end_node`、缓存的 **`topo_order`**。 |
| **拓扑序** | Point 层面的 DAG 序（经环处理后）；既是 **构建期** 传播顺序，也是 **增量传播** 的遍历顺序。 |
| **`gba_append_path`** | 在 **单一路径** 上追加一条 `GbaPath`，并同步更新前驱节点的 `fanouts` 与全局 `paths` 表。 |
| **`gba_relax_fanout_segments`** | 对 **一条** `(u_pt → v_pt)`、**一种输入沿**（rise/fall），完成 segment 计算与 **对 `v_node` 的松弛**（含 slew 比较与 prev 记录）。 |

实现细节（Kahn、`unordered_set` 环上节点、删 WIRE 日志等）收敛在 **`gba_compute_topo_and_break_cycles`** 内，调用方只需理解「会得到可用的 `topo_order`，且 `res` 可能被删边」。

## 3. 流水线（编排 vs 实现）

`build_gba_graphy()` **只做编排**（短函数、易读）：

1. **`gba_clear_graph_structure`** — 清空 `GbaGraphy` 各容器。
2. **`gba_allocate_nodes_for_points`** — 按 `analysis_mode` 初始化 delay/slew 极值与 prev 哨兵，建立 `pt_to_node`。
3. **`gba_compute_topo_and_break_cycles`** — 入度 + Kahn + 组合环处理，填充 **`topo_order`**。
4. **`gba_seed_input_clock_nodes`** — 对 `input_clk_point_ids` 置源。
5. **`gba_forward_propagate_build_paths`** — 沿 `topo_order` 松弛，收集 `end_node`，生成全部 **`GbaPath`**。

**辅助：**

- **`gba_append_path`** — 替代原先闭包 `add_path`，便于单测或将来复用（仍仅为 `STAWorker` 私有接口）。

## 4. 与原则的对照

- **单一职责**：每个 `gba_*` 对应流水线中的一步或一种操作（清空、骨架、拓扑、源点、松弛），避免单函数混合「排序 + 环处理 + 弧求值 + DP」。
- **好的抽象**：`gba_relax_fanout_segments` 把 **「rise/fall 两套几乎相同的 segment 循环」** 合并为 **一次** 以 `input_dir` 为参数的实现，减少重复与分叉修改成本。
- **信息隐藏**：`run_gba_propagate` 的调用方仍只依赖 **`topo_order` 与已缓存的 `GbaPath`**；不必了解 Kahn 与拆环细节。
- **YAGNI / KISS**：未引入虚接口或策略类；与现有 **`STAWorker` 状态**（`res`、`cell_library_`、`analysis_mode`）自然契合，与 `fanout_*` 私有阶段风格一致。

## 5. 文件索引

| 内容 | 位置 |
|------|------|
| `gba_*` 声明 | `include/sta/sta_data_structures.hpp`（`STAWorker` 私有区） |
| 实现与 `build_gba_graphy` 编排 | `src/analysis/gba/gba_engine.cpp` |
| Segment 求值 | `segment_delays_slews_gba`（`timing_arc_eval.cpp` / 头文件声明） |

---

*若后续要继续演进，可考虑：将「纯 Kahn + 拆环」抽到仅依赖 `TimingRunResult` 的自由函数并返回 `topo_order`（便于无 `STAWorker` 夹具测试）；或在环处理与日志之间加可注入的 `CycleBreaker` 回调（仅在产品需要可插拔策略时再引入）。*
