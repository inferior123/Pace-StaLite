#include "sta/debug.h"
#include "sta/sta_data_structures.hpp"
#include <iostream>

namespace sta {

static const char *point_type_str(PointType t) {
  switch (t) {
  case CLK:
    return "CLK";
  case INPUT:
    return "INPUT";
  case OUTPUT:
    return "OUTPUT";
  case REGD:
    return "REGD";
  case REGQ:
    return "REGQ";
  case COMB_PIN:
    return "COMB_PIN";
  }
  return "?";
}

static void print_point_ref(const TimingPointRef &p) {
  std::cout << " pt" << p.id;
  if (p.inst)
    std::cout << " " << p.inst->instance_name << "(" << p.inst->module_name
              << ")/";
  std::cout << p.port_name << " type=" << point_type_str(p.type);
}

void display_longest_path(STAWorker &worker) {
  worker.divide_path_entry();
  TimingRunResult res = worker.get_sta_res();
  if (res.paths.empty()) {
    std::cout << "display_longest_path: no paths\n";
    return;
  }

  // Longest = max data_arrival_time
  size_t longest_idx = 0;
  for (size_t i = 1; i < res.paths.size(); ++i) {
    if (res.paths[i].data_arrival_time >
        res.paths[longest_idx].data_arrival_time)
      longest_idx = i;
  }
  const TimingPathResult &pr = res.paths[longest_idx];
  const auto &points = res.points;

  std::cout << "\n========== Longest Path (max data_arrival) ==========\n";
  std::cout << "Path #" << pr.index << "\n";
  std::cout << "  group=" << static_cast<int>(pr.group) << "\n";
  std::cout << "  startpoint:";
  if (pr.startpoint < points.size())
    print_point_ref(points[pr.startpoint]);
  std::cout << "\n";
  std::cout << "  endpoint:";
  if (pr.endpoint < points.size())
    print_point_ref(points[pr.endpoint]);
  std::cout << "\n";
  std::cout << "  data_arrival_time=" << pr.data_arrival_time << "ps\n";
  if (pr.library_setup_time.has_value())
    std::cout << "  library_setup_time=" << pr.library_setup_time.value()
              << "ps\n";
  if (pr.library_hold_time.has_value())
    std::cout << "  library_hold_time=" << pr.library_hold_time.value()
              << "ps\n";

  std::cout << "  --- steps (step = lowest granularity) ---\n";
  for (size_t i = 0; i < pr.steps.size(); ++i) {
    const TimingStep &st = pr.steps[i];
    char d = (st.dir == TransitionDirection::RISING)
                 ? 'r'
                 : (st.dir == TransitionDirection::FALLING ? 'f' : '?');
    std::cout << "  [" << i << "] start_point:";
    if (st.start_point < points.size())
      print_point_ref(points[st.start_point]);
    std::cout << "\n";
    std::cout << "      end_point:";
    if (st.end_point < points.size())
      print_point_ref(points[st.end_point]);
    std::cout << "\n";
    std::cout << "      dir=" << d << " incr=" << st.incr
              << "ps slew=" << (st.slew / 1000.0) << "ns";
    // cap_load：仅对 output 点有值（下游 input pin 电容之和），单位 pF；与 PT 差异见下
    if (st.end_point < points.size() && points[st.end_point].inst) {
      std::cout << " cap_load=" << st.cap_load << "pf";
    }
    std::cout << " arrival=" << st.arrival << "ps\n";
  }
  std::cout << "========== end longest path ==========\n\n";
}

} // namespace sta
