#pragma once

#include "sta/sta_data_structures.hpp"
#include <string>

namespace sta {

/// 判断是否为时钟点（CLK_PIN 或 __clk__ 命名）
bool is_clock_point_for_report(const TimingPointRef &p);

/// 判断 candidate 节点是否为路径终点（REGD 或 OUTPUT）
bool is_terminal_node(const TimingRunResult &res,
                      const CandidateGraphy &cg, std::size_t node_id);

/// PointType 转字符串
std::string point_type_str(PointType type);

} // namespace sta
