#include "sta/sta_data_structures.hpp"
#include <cstddef>
#include <utility>
#include <vector>

namespace sta {

std::pair<double, double>
STAWorker::compute_require_and_slack(size_t path_idx) {
  if (analysis_mode == AnalysisMode::MAX) {
    double required = static_cast<double>(get_effective_clock_period());
    double setup = res.paths[path_idx].library_setup_time.value_or(0.0);

    if (setup > 0.0) {
      required -= setup;
      double slack = required - res.paths[path_idx].data_arrival_time;
      return {required, slack};
    } else {
      double required = res.paths[path_idx].library_hold_time.value_or(0.0);
      double slack = required + res.paths[path_idx].data_arrival_time;
      return {required, slack};
    }
  }

  return {0.0, 0.0};
}

double STAWorker::compare_slack(size_t a, size_t b) {
  return compute_require_and_slack(a).second <
         compute_require_and_slack(b).second;
};

void STAWorker::divide_path_entry() {
  for (const auto &path : res.paths) {
    PathGroup g = PathGroup::IN2OUT;
    if (path.startpoint < res.points.size() &&
        path.endpoint < res.points.size()) {
      PointType start_type =
          effective_start_type_for_group(res.points[path.startpoint]);
      PointType end_type = res.points[path.endpoint].type;
      g = classify_path_group(start_type, end_type);
    }
    PathEntry e{&path};
    if (analysis_mode == AnalysisMode::MAX) {
      switch (g) {
      case PathGroup::REG2REG:
        reg2reg_max.push_back(e);
        break;
      case PathGroup::IN2REG:
        in2reg_max.push_back(e);
        break;
      case PathGroup::REG2OUT:
        reg2out_max.push_back(e);
        break;
      case PathGroup::IN2OUT:
        in2out_max.push_back(e);
        break;
      }
    } else {
      switch (g) {
      case PathGroup::REG2REG:
        reg2reg_min.push_back(e);
        break;
      case PathGroup::IN2REG:
        in2reg_min.push_back(e);
        break;
      case PathGroup::REG2OUT:
        reg2out_min.push_back(e);
        break;
      case PathGroup::IN2OUT:
        in2out_min.push_back(e);
        break;
      }
    }
  }
}

std::vector<PathEntry> STAWorker::get_path_entry(PathEntryType type) {
  switch (type){
    case sta::PathEntryType::REG2REG_MAX:
      return reg2reg_max;
    case sta::PathEntryType::IN2REG_MAX:
      return in2reg_max;
    case sta::PathEntryType::REG2OUT_MAX:
      return reg2out_max;
    case sta::PathEntryType::IN2OUT_MAX:
      return in2out_max;
    case sta::PathEntryType::REG2REG_MIN:
      return reg2reg_min;
    case sta::PathEntryType::IN2REG_MIN:
      return in2reg_min;
    case sta::PathEntryType::REG2OUT_MIN:
      return reg2out_min;
    case sta::PathEntryType::IN2OUT_MIN:
      return in2out_min;
    default:
      return {};
  }
}

std::vector<PathEntry> STAWorker::get_path_entry(PathGroup group_type, AnalysisMode mode) {
  // 根据group_type和mode的组合返回对应vector
  if (mode == AnalysisMode::MAX) {
    switch (group_type) {
      case PathGroup::REG2REG:
        return reg2reg_max;
      case PathGroup::IN2REG:
        return in2reg_max;
      case PathGroup::REG2OUT:
        return reg2out_max;
      case PathGroup::IN2OUT:
        return in2out_max;
      default:
        return {};
    }
  } else { // MIN
    switch (group_type) {
      case PathGroup::REG2REG:
        return reg2reg_min;
      case PathGroup::IN2REG:
        return in2reg_min;
      case PathGroup::REG2OUT:
        return reg2out_min;
      case PathGroup::IN2OUT:
        return in2out_min;
      default:
        return {};
    }
  }
}

std::vector<PathEntry> *STAWorker::get_path_entry_ptr(PathGroup group_type,
                                                      AnalysisMode mode) {
  if (mode == AnalysisMode::MAX) {
    switch (group_type) {
    case PathGroup::REG2REG: return &reg2reg_max;
    case PathGroup::IN2REG: return &in2reg_max;
    case PathGroup::REG2OUT: return &reg2out_max;
    case PathGroup::IN2OUT: return &in2out_max;
    default: return nullptr;
    }
  }
  switch (group_type) {
  case PathGroup::REG2REG: return &reg2reg_min;
  case PathGroup::IN2REG: return &in2reg_min;
  case PathGroup::REG2OUT: return &reg2out_min;
  case PathGroup::IN2OUT: return &in2out_min;
  default: return nullptr;
  }
}

std::vector<PathEntry> *STAWorker::get_path_entry_ptr(PathEntryType type) {
  switch (type) {
  case PathEntryType::REG2REG_MAX: return &reg2reg_max;
  case PathEntryType::IN2REG_MAX: return &in2reg_max;
  case PathEntryType::REG2OUT_MAX: return &reg2out_max;
  case PathEntryType::IN2OUT_MAX: return &in2out_max;
  case PathEntryType::REG2REG_MIN: return &reg2reg_min;
  case PathEntryType::IN2REG_MIN: return &in2reg_min;
  case PathEntryType::REG2OUT_MIN: return &reg2out_min;
  case PathEntryType::IN2OUT_MIN: return &in2out_min;
  default: return nullptr;
  }
}

void STAWorker::respath_ascending(PathEntryType type) {
  auto *vec = get_path_entry_ptr(type);
  if (!vec) return;
  std::sort(vec->begin(), vec->end(),
            [this](const PathEntry &a, const PathEntry &b) {
              size_t ia = static_cast<size_t>(a.path - res.paths.data());
              size_t ib = static_cast<size_t>(b.path - res.paths.data());
              return compare_slack(ia, ib);
            });
}

void STAWorker::respath_descending(PathEntryType type) {
  auto *vec = get_path_entry_ptr(type);
  if (!vec) return;
  std::sort(vec->begin(), vec->end(),
            [this](const PathEntry &a, const PathEntry &b) {
              size_t ia = static_cast<size_t>(a.path - res.paths.data());
              size_t ib = static_cast<size_t>(b.path - res.paths.data());
              return !compare_slack(ia, ib);
            });
}

void STAWorker::respath_ascending(PathGroup group_type, AnalysisMode mode) {
  auto *vec = get_path_entry_ptr(group_type, mode);
  if (!vec) return;
  std::sort(vec->begin(), vec->end(),
            [this](const PathEntry &a, const PathEntry &b) {
              size_t ia = static_cast<size_t>(a.path - res.paths.data());
              size_t ib = static_cast<size_t>(b.path - res.paths.data());
              return compare_slack(ia, ib);
            });
}

void STAWorker::respath_descending(PathGroup group_type, AnalysisMode mode) {
  auto *vec = get_path_entry_ptr(group_type, mode);
  if (!vec) return;
  std::sort(vec->begin(), vec->end(),
            [this](const PathEntry &a, const PathEntry &b) {
              size_t ia = static_cast<size_t>(a.path - res.paths.data());
              size_t ib = static_cast<size_t>(b.path - res.paths.data());
              return !compare_slack(ia, ib);
            });
}

} // namespace sta
