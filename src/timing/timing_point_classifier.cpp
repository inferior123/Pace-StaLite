#include "sta/timing_point_classifier.hpp"

namespace sta {

bool is_clock_point_for_report(const TimingPointRef &p) {
  return p.type == CLK_PIN ||
         (!p.port_name.empty() && p.port_name == "__clk__") ||
         (p.bit.has_value() && p.bit->wire_name == "__clk__");
}

bool is_terminal_node(const TimingRunResult &res,
                      const CandidateGraphy &cg, std::size_t node_id) {
  if (node_id >= cg.nodes.size())
    return true;
  std::size_t pt_id = cg.nodes[node_id].point_idx;
  if (pt_id >= res.points.size())
    return true;
  PointType t = res.points[pt_id].type;
  return (t == REGD || t == OUTPUT);
}

std::string point_type_str(PointType type) {
  switch (type) {
  case CLK:
    return "CLK";
  case CLK_PIN:
    return "CLK_PIN";
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
  case CLK_SOURCE:
    return "CLK_SOURCE";
  }

  return "?";
}

} // namespace sta
