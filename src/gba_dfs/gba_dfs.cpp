#include "cell/cell_data_structure.hpp"
#include "sta/sta_data_structures.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stack>
#include <string>
#include <unordered_set>
#include <vector>


namespace sta {
// Liberty LUT 返回 ns，STA 内部统一用 ps
static constexpr double NS_TO_PS = 1000.0;

// 辅助函数：从查找表中获取悲观值（最大值），返回 ps
double get_pessimistic_delay_from_lut(const celllib::LookupTable &lut) {
  double max_delay_ns = 0.0;
  for (const auto &row : lut.values) {
    for (double val : row) {
      if (val > max_delay_ns) {
        max_delay_ns = val;
      }
    }
  }
  return max_delay_ns * NS_TO_PS;
}

void STAWorker::run_timing_analysis_dfs() {
  assert(cell_library_);
  res.paths.clear();

  if (analysis_granularity_ == AnalysisGranularity::FINE) {
    assert(false &&
           "FINE mode uses candidate path, not run_timing_analysis_dfs");
  }

  const double clk_slew_ns =
      std::max(static_cast<double>(cfg.clock_transit_raise),
               static_cast<double>(cfg.clock_transit_fall)) /
      NS_TO_PS;

  struct PathFrame {
    std::size_t point_id;
    double arrival;
    double slew_rise_ns;
    double slew_fall_ns;
    TransitionDirection dir;
    std::size_t fanout_idx;
  };
  struct StackFrame {
    std::size_t point_id;
    double arrival;
    double slew_rise_ns;
    double slew_fall_ns;
    TransitionDirection dir;
    std::size_t fanout_idx;
  };

  auto find_arc = [this](const TimingPointRef &origin,
                         const TimingPointRef &target,
                         EdgeType edge_type) -> const celllib::TimingArc * {
    if (!target.inst)
      return nullptr;
    const auto *cell = cell_library_->get_cell(target.inst->module_name);
    if (!cell)
      return nullptr;
    const auto *out_pin = cell->get_pin(target.port_name);
    if (!out_pin)
      return nullptr;

    if (edge_type == COMB_ARC) {
      // 优先选择 sdf_cond 为空的默认组合弧
      for (const auto &a : out_pin->timing_arcs) {
        if (a.timing_type == celllib::TimingType::COMBINATIONAL &&
            a.related_pin == origin.port_name && !a.sdf_cond.has_value())
          return &a;
      }
      // 若没有无条件弧，则退回到原始逻辑（任意匹配弧）
      for (const auto &a : out_pin->timing_arcs) {
        if (a.timing_type == celllib::TimingType::COMBINATIONAL &&
            a.related_pin == origin.port_name)
          return &a;
      }
    } else if (edge_type == SEQ_ARC) {
      std::string clk_pin = "CK";
      if (cell->ff.has_value() && cell->ff->clocked_on.has_value())
        clk_pin = cell->ff->clocked_on.value();

      // 优先选择 sdf_cond 为空的 C2Q 弧
      for (const auto &a : out_pin->timing_arcs) {
        if ((a.timing_type == celllib::TimingType::RISING_EDGE ||
             a.timing_type == celllib::TimingType::FALLING_EDGE) &&
            a.related_pin == clk_pin && !a.sdf_cond.has_value())
          return &a;
      }
      // 若没有无条件弧，则退回到原始逻辑
      for (const auto &a : out_pin->timing_arcs) {
        if ((a.timing_type == celllib::TimingType::RISING_EDGE ||
             a.timing_type == celllib::TimingType::FALLING_EDGE) &&
            a.related_pin == clk_pin)
          return &a;
      }
    }
    return nullptr;
  };

  auto compute_setup_hold =
      [this, clk_slew_ns](
          Instance *sink, const std::string &port, double data_arrival_ps,
          double data_slew_rise_ns, double data_slew_fall_ns,
          TransitionDirection data_dir) -> std::pair<double, double> {
    if (!sink || !cell_library_)
      return {0, 0};
    const auto *cell = cell_library_->get_cell(sink->module_name);
    if (!cell)
      return {0, 0};
    const auto *pin = cell->get_pin(port);
    if (!pin)
      return {0, 0};
    double data_trans = std::max(data_slew_rise_ns, data_slew_fall_ns);
    double setup_ps = 0, hold_ps = 0;
    for (const auto &arc : pin->timing_arcs) {
      if (arc.timing_type == celllib::TimingType::SETUP_RISING ||
          arc.timing_type == celllib::TimingType::SETUP_FALLING) {
        double sr =
            caculate_setup_rise(arc, cell_library_, data_trans, clk_slew_ns);
        double sf =
            caculate_setup_fall(arc, cell_library_, data_trans, clk_slew_ns);
        if (data_dir == TransitionDirection::RISING)
          setup_ps = sr * NS_TO_PS;
        else if (data_dir == TransitionDirection::FALLING)
          setup_ps = sf * NS_TO_PS;
        else
          setup_ps = std::max(sr, sf) * NS_TO_PS;
      }
      if (arc.timing_type == celllib::TimingType::HOLD_RISING ||
          arc.timing_type == celllib::TimingType::HOLD_FALLING) {
        double hr =
            caculate_hold_rise(arc, cell_library_, data_trans, clk_slew_ns);
        double hf =
            caculate_hold_fall(arc, cell_library_, data_trans, clk_slew_ns);
        if (data_dir == TransitionDirection::RISING)
          hold_ps = hr * NS_TO_PS;
        else if (data_dir == TransitionDirection::FALLING)
          hold_ps = hf * NS_TO_PS;
        else
          hold_ps = std::max(hr, hf) * NS_TO_PS;
      }
    }
    return {setup_ps, hold_ps};
  };

  int path_count = 0;
  std::unordered_set<std::string> path_printed;

  for (std::size_t start_id : input_clk_point_ids) {
    if (start_id >= res.points.size())
      continue;

    double slew_ns = clk_slew_ns;

    std::stack<StackFrame> stk;
    std::vector<PathFrame> path;
    stk.push(
        {start_id, 0.0, slew_ns, slew_ns, TransitionDirection::UNKNOWN, 0});

    while (!stk.empty()) {
      StackFrame f = stk.top();
      stk.pop();

      if (f.fanout_idx == 0) {
        path.push_back(
            {f.point_id, f.arrival, f.slew_rise_ns, f.slew_fall_ns, f.dir, 0});
      }

      const TimingPointRef &cur = res.points[f.point_id];

      if (cur.type == REGD || cur.type == OUTPUT) {
        std::string fp;
        for (const auto &pf : path)
          fp += std::to_string(pf.point_id) + ":" + std::to_string(pf.arrival) +
                "->";
        if (path_printed.insert(fp).second) {
          path_count++;
          TimingPathResult pr;
          pr.startpoint = path.front().point_id;
          pr.endpoint = path.back().point_id;
          pr.data_arrival_time = f.arrival;
          pr.index = res.paths.size();
          pr.group = classify_path_group(
              effective_start_type_for_group(res.points[pr.startpoint]),
              cur.type);

          if (cur.type == REGD && cur.inst) {
            auto [setup_ps, hold_ps] =
                compute_setup_hold(cur.inst, cur.port_name, f.arrival,
                                   f.slew_rise_ns, f.slew_fall_ns, f.dir);
            pr.library_setup_time = setup_ps;
            pr.library_hold_time = hold_ps;
          }

          for (size_t i = 0; i < path.size(); ++i) {
            if (i == 0) {
              continue;
            }
            double incr = path[i].arrival - path[i - 1].arrival;
            double slew =
                std::max(path[i].slew_rise_ns, path[i].slew_fall_ns) * NS_TO_PS;
            TimingStep step{
                path[i - 1].point_id,
                path[i].point_id,
                incr,
                slew,
                res.points[path[i - 1].point_id].load_cap,
                path[i].arrival,
                path[i].dir};
            pr.steps.push_back(step);
          }
          res.paths.push_back(pr);

          std::cout << "  [DEBUG] Path #" << path_count << " pt"
                    << pr.startpoint << "->pt" << pr.endpoint
                    << " arrival=" << f.arrival << "ps"
                    << ", steps=" << pr.steps.size() << "\n";
        }
      }

      if (f.fanout_idx >= cur.fanouts.size()) {
        path.pop_back();
        continue;
      }

      const TimingEdge &e = cur.fanouts[f.fanout_idx];
      stk.push({f.point_id, f.arrival, f.slew_rise_ns, f.slew_fall_ns, f.dir,
                f.fanout_idx + 1});

      const TimingPointRef &target = res.points[e.target_point];

      double delay_ps = 0.0;
      double out_slew_rise_ns = f.slew_rise_ns;
      double out_slew_fall_ns = f.slew_fall_ns;
      TransitionDirection out_dir = f.dir;

      if (e.type == WIRE) {
        delay_ps = 0.0;
        out_slew_rise_ns = f.slew_rise_ns;
        out_slew_fall_ns = f.slew_fall_ns;
        out_dir = f.dir;
      } else {
        const celllib::TimingArc *arc = find_arc(cur, target, e.type);
        if (!arc) {
          std::cerr << "  [DEBUG] arc not found: pt " << f.point_id << " -> "
                    << e.target_point << " type "
                    << (e.type == COMB_ARC ? "COMB_ARC" : "SEQ_ARC") << "\n";
          // 不 pop_back：target 尚未加入 path，cur 仍应保留在 path 中
          continue;
        }

        double load_cap = target.load_cap;

        double in_slew_rise = f.slew_rise_ns;
        double in_slew_fall = f.slew_fall_ns;
        bool is_start = (cur.type == INPUT || cur.type == CLK_PIN);
        if (is_start && in_slew_rise == 0)
          in_slew_rise = clk_slew_ns;
        if (is_start && in_slew_fall == 0)
          in_slew_fall = clk_slew_ns;

        bool is_ck2q = (e.type == SEQ_ARC);

        if (analysis_granularity_ == AnalysisGranularity::COARSE) {
          delay_ps = get_pessimistic_delay_from_lut(
              arc->cell_rise.has_value() ? arc->cell_rise.value()
                                         : celllib::LookupTable{});
          if (delay_ps == 0 && arc->cell_fall.has_value())
            delay_ps = get_pessimistic_delay_from_lut(arc->cell_fall.value());
          if (delay_ps == 0 && arc->intrinsic_rise.has_value())
            delay_ps = std::max(arc->intrinsic_rise.value_or(0),
                                arc->intrinsic_fall.value_or(0)) *
                       NS_TO_PS;
        } else {
          double dr =
              caculate_delay_rise(*arc, cell_library_, in_slew_rise, load_cap);
          double df =
              caculate_delay_fall(*arc, cell_library_, in_slew_fall, load_cap);
          out_slew_rise_ns = caculate_transition_rise(*arc, cell_library_,
                                                      in_slew_rise, load_cap) /
                             NS_TO_PS;
          out_slew_fall_ns = caculate_transition_fall(*arc, cell_library_,
                                                      in_slew_fall, load_cap) /
                             NS_TO_PS;
          out_dir = speculate_transition_direction(is_ck2q, *arc, f.dir);
          if (out_dir == TransitionDirection::RISING)
            delay_ps = dr;
          else if (out_dir == TransitionDirection::FALLING)
            delay_ps = df;
          else
            delay_ps = std::max(dr, df);

          std::string cell_name =
              target.inst ? target.inst->instance_name : "?";
          std::cout << "  [DEBUG] delay lut: " << cell_name << " "
                    << cur.port_name << "->" << target.port_name
                    << " slew_rise=" << in_slew_rise
                    << " slew_fall=" << in_slew_fall << " load_cap=" << load_cap
                    << " delay_rise=" << dr << " delay_fall=" << df << " -> "
                    << delay_ps << "ps\n";
        }
      }

      double new_arrival = f.arrival + delay_ps;
      stk.push({e.target_point, new_arrival, out_slew_rise_ns, out_slew_fall_ns,
                out_dir, 0});
    }
  }

  std::cout << "  [DEBUG] run_timing_analysis_dfs: " << path_count
            << " paths found, res.paths.size()=" << res.paths.size() << "\n";
}
}