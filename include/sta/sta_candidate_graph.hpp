#ifndef STA_CANDIDATE_GRAPH_HPP
#define STA_CANDIDATE_GRAPH_HPP

#include "sta_timing_result.hpp"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace sta {

struct CandidatePath {
  std::size_t id;
  std::size_t start_node;
  std::size_t end_node;
  std::optional<std::size_t> next_path;

  std::vector<size_t> fanouts_edge;
};

struct CandidateNode {
  std::size_t id;
  size_t point_idx;

  std::vector<std::size_t> relate_candidate_point;
  std::vector<std::size_t> fanout_paths;
};

struct CandidatePathSegmentResult {
  double total_delay_ps = 0.0;
  double output_slew_ns = 0.0;
  TransitionDirection output_dir = TransitionDirection::UNKNOWN;
  std::vector<TimingStep> steps;
};

struct CandidateGraphy {
  std::vector<CandidateNode> nodes;
  std::vector<CandidatePath> paths;

  std::unordered_map<std::size_t, std::size_t> point_to_node;
};

struct PathEntry {
  const TimingPathResult *path;
};

enum class PathEntryType {
  REG2REG_MAX,
  IN2REG_MAX,
  REG2OUT_MAX,
  IN2OUT_MAX,
  REG2REG_MIN,
  IN2REG_MIN,
  REG2OUT_MIN,
  IN2OUT_MIN
};

} // namespace sta

#endif // STA_CANDIDATE_GRAPH_HPP
