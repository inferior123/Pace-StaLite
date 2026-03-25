#include "sta/sta_data_structures.hpp"
#include <cstddef>
#include <utility>
#include <vector>

namespace sta {
std::pair<double, double>
STAWorker::compute_require_and_slack(size_t path_idx) {
  const auto &p = res.paths[path_idx];

  if (analysis_mode == AnalysisMode::MAX) {
    // 与 STAReportGenerator::compute_required_and_slack 保持一致：
    // required = clock_period - setup（若有），slack = required - arrival
    double required = static_cast<double>(get_effective_clock_period());
    double setup_ps = p.library_setup_time.value_or(0.0);
    if (setup_ps > 0.0) {
      required -= setup_ps;
    }
    double slack = required - p.data_arrival_time;
    return {required, slack};
  } else {
    // MIN(hold)：与 sta_report / display 一致，slack = arrival - required
    double required = p.library_hold_time.value_or(0.0);
    double slack = p.data_arrival_time - required;
    return {required, slack};
  }
}

double STAWorker::compare_slack(size_t a, size_t b) {
  return compute_require_and_slack(a).second <
         compute_require_and_slack(b).second;
};

void STAWorker::divide_path_entry() {
  // 在分类之前，首先清理所有可能用到的 vector
  reg2reg_max.clear();
  in2reg_max.clear();
  reg2out_max.clear();
  in2out_max.clear();
  reg2reg_min.clear();
  in2reg_min.clear();
  reg2out_min.clear();
  in2out_min.clear();

  // 重置“已排序”标志，确保本次分类完成后再按需排序
  reg2reg_max_sorted = false;
  in2reg_max_sorted = false;
  reg2out_max_sorted = false;
  in2out_max_sorted = false;
  reg2reg_min_sorted = false;
  in2reg_min_sorted = false;
  reg2out_min_sorted = false;
  in2out_min_sorted = false;

  for (const auto &path : res.paths) {
    // 直接使用构建 TimingPathResult 时已经设置好的 group，
    // 避免重复根据 start/end point type 推导时出现不一致

    PathGroup g = path.group;
    PathEntry e{&path};
    // 统一分类，min 和 max 都插进去
    switch (g) {
    case PathGroup::REG2REG:
      reg2reg_max.push_back(e);
      reg2reg_min.push_back(e);
      break;
    case PathGroup::IN2REG:
      in2reg_max.push_back(e);
      in2reg_min.push_back(e);
      break;
    case PathGroup::REG2OUT:
      reg2out_max.push_back(e);
      reg2out_min.push_back(e);
      break;
    case PathGroup::IN2OUT:
      in2out_max.push_back(e);
      in2out_min.push_back(e);
      break;
    }
  }
}

std::vector<PathEntry> STAWorker::get_path_entry(PathEntryType type) {
  switch (type) {
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

std::vector<PathEntry> *STAWorker::get_sorted_entries(PathGroup group_type,
                                                      AnalysisMode mode) {
  auto *vec = get_path_entry_ptr(group_type, mode);
  if (!vec)
    assert(false && "should not be null");

  bool *sorted_flag = nullptr;
  if (mode == AnalysisMode::MAX) {
    switch (group_type) {
    case PathGroup::REG2REG:
      sorted_flag = &reg2reg_max_sorted;
      break;
    case PathGroup::IN2REG:
      sorted_flag = &in2reg_max_sorted;
      break;
    case PathGroup::REG2OUT:
      sorted_flag = &reg2out_max_sorted;
      break;
    case PathGroup::IN2OUT:
      sorted_flag = &in2out_max_sorted;
      break;
    }
  } else {
    switch (group_type) {
    case PathGroup::REG2REG:
      sorted_flag = &reg2reg_min_sorted;
      break;
    case PathGroup::IN2REG:
      sorted_flag = &in2reg_min_sorted;
      break;
    case PathGroup::REG2OUT:
      sorted_flag = &reg2out_min_sorted;
      break;
    case PathGroup::IN2OUT:
      sorted_flag = &in2out_min_sorted;
      break;
    }
  }

  if (!sorted_flag)
    return vec;

  // 始终按slack升序排列，降序（最大优先）时倒序取即可
  if (!*sorted_flag) {
    std::sort(vec->begin(), vec->end(),
              [this](const PathEntry &a, const PathEntry &b) {
                size_t ia = static_cast<size_t>(a.path - res.paths.data());
                size_t ib = static_cast<size_t>(b.path - res.paths.data());
                return compare_slack(ia, ib);
              });
    *sorted_flag = true;
  }

  return vec;
}

const PathEntry *STAWorker::get_top_k(PathGroup group_type, AnalysisMode mode,
                                      std::size_t k) {
  auto *vec = get_sorted_entries(group_type, mode);
  if (!vec)
    return nullptr;
  if (k >= vec->size())
    return nullptr;
  return &(*vec)[k];
}

const PathEntry *STAWorker::get_last_k(PathGroup group_type, AnalysisMode mode,
                                       std::size_t k) {
  auto *vec = get_sorted_entries(group_type, mode);
  if (!vec)
    return nullptr;
  if (k >= vec->size())
    return nullptr;
  std::size_t idx = vec->size() - 1 - k;
  return &(*vec)[idx];
}

std::size_t STAWorker::get_entries_size(PathGroup group_type,
                                        AnalysisMode mode) {
  return get_path_entry(group_type, mode).size();
}

std::size_t STAWorker::get_entries_size(PathEntryType type) {
  return get_path_entry(type).size();
}

std::vector<PathEntry> STAWorker::get_path_entry(PathGroup group_type,
                                                 AnalysisMode mode) {
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
    case PathGroup::REG2REG:
      return &reg2reg_max;
    case PathGroup::IN2REG:
      return &in2reg_max;
    case PathGroup::REG2OUT:
      return &reg2out_max;
    case PathGroup::IN2OUT:
      return &in2out_max;
    default:
      return nullptr;
    }
  }
  switch (group_type) {
  case PathGroup::REG2REG:
    return &reg2reg_min;
  case PathGroup::IN2REG:
    return &in2reg_min;
  case PathGroup::REG2OUT:
    return &reg2out_min;
  case PathGroup::IN2OUT:
    return &in2out_min;
  default:
    return nullptr;
  }
}

std::vector<PathEntry> *STAWorker::get_path_entry_ptr(PathEntryType type) {
  switch (type) {
  case PathEntryType::REG2REG_MAX:
    return &reg2reg_max;
  case PathEntryType::IN2REG_MAX:
    return &in2reg_max;
  case PathEntryType::REG2OUT_MAX:
    return &reg2out_max;
  case PathEntryType::IN2OUT_MAX:
    return &in2out_max;
  case PathEntryType::REG2REG_MIN:
    return &reg2reg_min;
  case PathEntryType::IN2REG_MIN:
    return &in2reg_min;
  case PathEntryType::REG2OUT_MIN:
    return &reg2out_min;
  case PathEntryType::IN2OUT_MIN:
    return &in2out_min;
  default:
    return nullptr;
  }
}

void STAWorker::respath_ascending(PathEntryType type) {
  auto *vec = get_path_entry_ptr(type);
  if (!vec)
    return;
  std::sort(vec->begin(), vec->end(),
            [this](const PathEntry &a, const PathEntry &b) {
              size_t ia = static_cast<size_t>(a.path - res.paths.data());
              size_t ib = static_cast<size_t>(b.path - res.paths.data());
              return compare_slack(ia, ib);
            });
}

void STAWorker::respath_descending(PathEntryType type) {
  auto *vec = get_path_entry_ptr(type);
  if (!vec)
    return;
  std::sort(vec->begin(), vec->end(),
            [this](const PathEntry &a, const PathEntry &b) {
              size_t ia = static_cast<size_t>(a.path - res.paths.data());
              size_t ib = static_cast<size_t>(b.path - res.paths.data());
              return !compare_slack(ia, ib);
            });
}

void STAWorker::respath_ascending(PathGroup group_type, AnalysisMode mode) {
  auto *vec = get_path_entry_ptr(group_type, mode);
  if (!vec)
    return;
  std::sort(vec->begin(), vec->end(),
            [this](const PathEntry &a, const PathEntry &b) {
              size_t ia = static_cast<size_t>(a.path - res.paths.data());
              size_t ib = static_cast<size_t>(b.path - res.paths.data());
              return compare_slack(ia, ib);
            });
}

void STAWorker::respath_descending(PathGroup group_type, AnalysisMode mode) {
  auto *vec = get_path_entry_ptr(group_type, mode);
  if (!vec)
    return;
  std::sort(vec->begin(), vec->end(),
            [this](const PathEntry &a, const PathEntry &b) {
              size_t ia = static_cast<size_t>(a.path - res.paths.data());
              size_t ib = static_cast<size_t>(b.path - res.paths.data());
              return !compare_slack(ia, ib);
            });
}

} // namespace sta
