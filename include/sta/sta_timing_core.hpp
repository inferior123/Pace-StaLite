#ifndef STA_TIMING_CORE_HPP
#define STA_TIMING_CORE_HPP

namespace sta {

/**
 * 信号转换方向枚举：用于选择使用哪个查找表（rise/fall）
 */
enum class TransitionDirection {
  RISING,
  FALLING,
  UNKNOWN
};

enum class AnalysisMode { MAX, MIN };

enum class PathGroup { REG2REG, IN2REG, REG2OUT, IN2OUT };

enum PointType {
  COMB_PIN,
  INPUT,
  OUTPUT,
  CLK_PIN,
  CLK_SOURCE,
  REGQ,
  REGD,
  CLK
};

inline PathGroup classify_path_group(PointType start_type, PointType end_type) {
  if (end_type == REGD)
    return (start_type == CLK_PIN) ? PathGroup::REG2REG : PathGroup::IN2REG;
  if (end_type == OUTPUT)
    return (start_type == CLK_PIN) ? PathGroup::REG2OUT : PathGroup::IN2OUT;
  return PathGroup::IN2OUT;
}

} // namespace sta

#endif // STA_TIMING_CORE_HPP
