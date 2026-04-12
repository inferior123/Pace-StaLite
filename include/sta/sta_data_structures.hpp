#ifndef STA_DATA_STRUCTURES_HPP
#define STA_DATA_STRUCTURES_HPP

/**
 * STA 数据结构的模块门面（facade）。
 *
 * 按依赖层次包含子头文件；调用方既可继续包含本头，也可仅包含所需子模块以缩短编译依赖。
 * 层次（自下而上）：信号与实例 → 时序核心枚举 → 时序结果图 → GBA 图 → Candidate 图 →
 * STAWorker → 弧求值与全局 LUT API。
 */

#include "sta_signal.hpp"
#include "sta_instance.hpp"
#include "sta_timing_core.hpp"
#include "sta_timing_result.hpp"
#include "sta_gba_graph.hpp"
#include "sta_candidate_graph.hpp"
#include "sta_worker.hpp"
#include "sta_timing_api.hpp"

#endif // STA_DATA_STRUCTURES_HPP
