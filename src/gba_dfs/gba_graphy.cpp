
#include "cell/cell_data_structure.hpp"
#include "sta/sta_data_structures.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <unordered_set>
#include <vector>

namespace sta {
void STAWorker::calculate_load_capacitance_dfs() {
  assert(cell_library_);

  // 从 input_clk_point_ids 出发，拓扑遍历
  std::deque<std::size_t> queue(input_clk_point_ids.begin(),
                                input_clk_point_ids.end());
  std::unordered_set<std::size_t> visited;

  while (!queue.empty()) {
    std::size_t pt_id = queue.front();
    queue.pop_front();
    if (visited.count(pt_id))
      continue;
    visited.insert(pt_id);

    TimingPointRef &pt = res.points[pt_id];

    // 若为 cell 输出点（COMB_PIN 或 REGQ），将该输出端口的所有 fanout 的 input
    // pin 电容累加
    if (pt.type == COMB_PIN || pt.type == REGQ || pt.type == INPUT) {
      for (const TimingEdge &e : pt.fanouts) {
        const TimingPointRef &target = res.points[e.target_point];

        if (!target.inst)
          continue; // 顶层端口，无电容
        const auto *fanout_cell =
            cell_library_->get_cell(target.inst->module_name);
        if (!fanout_cell) {
          std::cerr << "invalid cell " << target.inst->module_name << std::endl;
          assert(false);
        }
        const auto *input_pin = fanout_cell->get_pin(target.port_name);

        if (input_pin && input_pin->capacitance.has_value()) {
          pt.load_cap += input_pin->capacitance.value();
        }
      }
    }

    for (const TimingEdge &e : pt.fanouts) {
      if (visited.count(e.target_point) == 0) {
        queue.push_back(e.target_point);
      }
    }
  }
}
}