#include "sta/sta_data_structures.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iomanip>
#include <string>
#include <unordered_set>

using namespace verilog;

namespace sta {

std::size_t
STAWorker::get_or_create_candidate_node(Instance *inst,
                                        const std::string &port_name) {
  CandidateNodeKey key{inst, port_name};
  auto it = candidate_graphy_.node_index.find(key);
  if (it != candidate_graphy_.node_index.end()) {
    return it->second;
  }

  std::size_t id = candidate_graphy_.nodes.size();
  candidate_graphy_.node_index.emplace(key, id);

  CandidateNode node;
  node.id = id;
  node.inst = inst;
  node.port_name = port_name;
  candidate_graphy_.nodes.push_back(std::move(node));

  return id;
}

std::size_t STAWorker::get_or_create_point_node(Instance *inst,
                                                const SignalBit &bit,
                                                const std::string &port_name) {
  TimingPointRefKey key{inst, port_name, bit};
  auto it = res.point_index.find(key);
  if (it != res.point_index.end()) {
    return it->second;
  }

  std::size_t id = res.points.size();
  res.point_index.emplace(key, id);

  TimingPointRef point;
  point.id = id;
  point.inst = inst;
  point.bit = bit;
  point.port_name = port_name;

  res.points.push_back(point);
  return id;
}



void STAWorker::build_candidate_graphy_dfs() {
  candidate_graphy_.paths.clear();
  candidate_timing_queue.clear();
  for (auto &node : candidate_graphy_.nodes) {
    node.fanout_paths.clear();
  }

  // 确保顶层 input 对应节点存在
  for (const SignalBit &bit : top_module_inputs) {
    get_or_create_candidate_node(nullptr, bit.wire_name);
  }

  // 将 endpoint（reg D 端、顶层输出）也添加为 candidate node
  for (const auto &[bit, eps] : endpoints) {
    SignalBit canonical = sigmap.find(bit);
    for (const auto &ep : eps) {
      if (ep.sink == nullptr) {
        // 顶层输出端口
        get_or_create_candidate_node(nullptr, ep.port);
      } else {
        // reg D 端或其他 endpoint
        get_or_create_candidate_node(ep.sink, ep.port);
      }
    }
  }

  // 为每个已有节点得到对应的 canonical SignalBit，建立 candidate_bit -> node_id
  std::unordered_map<SignalBit, std::size_t, SignalBitHash>
      candidate_bit_to_node_id;
  for (const CandidateNode &node : candidate_graphy_.nodes) {
    SignalBit rep;
    if (node.inst == nullptr) {
      // 顶层端口：通过 port_name 查找对应的 SignalBit
      SignalSpec spec = get_signal_bits(node.port_name);
      if (spec.empty())
        continue;
      rep = sigmap.find(spec[0]);
    } else {
      // 实例端口：取连接到该端口的信号
      // - 对于输入端口（如 CK、D）：取连接到该端口的信号
      // - 对于输出端口（如 NON_UNATE 的输出）：取该端口驱动的信号
      auto it = node.inst->connections.find(node.port_name);
      if (it == node.inst->connections.end() || it->second.empty())
        continue;

      // 检查这是输入还是输出端口
      bool is_input_port = false;
      if (cell_library_) {
        const auto *cell = cell_library_->get_cell(node.inst->module_name);
        if (cell) {
          auto input_pins = cell->get_input_pins();
          is_input_port = std::find(input_pins.begin(), input_pins.end(),
                                    node.port_name) != input_pins.end();
        }
      }

      if (is_input_port) {
        // 输入端口（CK、D 等）：取连接到该端口的信号
        rep = sigmap.find(it->second[0]);
      } else {
        // 输出端口（Q、NON_UNATE 输出等）：取该端口驱动的信号
        rep = sigmap.find(it->second[0]);
      }
    }
    candidate_bit_to_node_id[rep] = node.id;
  }

  if (candidate_bit_to_node_id.empty()) {
    std::cerr << "  [WARNING] No candidate bits found for DFS\n";
    return;
  }

  // 调试：打印 candidate_bit_to_node_id 映射
  std::cerr << "  [DEBUG] Candidate bits to node mapping:\n";
  for (const auto &[bit, node_id] : candidate_bit_to_node_id) {
    std::string module_name = "input/output port";
    std::string instance_name = " ";
    if (candidate_graphy_.nodes[node_id].inst) {
      module_name = candidate_graphy_.nodes[node_id].inst->module_name;
      instance_name =
          candidate_graphy_.nodes[node_id].inst->instance_name + " ";
    }

    std::cerr << "    " << bit.wire_name << "[" << bit.bit_offset
              << "] -> Node[" << node_id << "]" << " " << module_name << ": "
              << instance_name << candidate_graphy_.nodes[node_id].port_name
              << std::endl;
  }

  std::unordered_set<SignalBit, SignalBitHash> visited;
  std::vector<Fanout> path_fanouts;

  std::function<void(std::size_t, const SignalBit &)> dfs =
      [&](std::size_t start_node_id, const SignalBit &cur_bit) {
        SignalBit cur_canonical = sigmap.find(cur_bit);

        // 如果当前信号没有时序数据，尝试继续查找（可能还没建立 fanout）
        if (!timing_data.count(cur_canonical)) {
          return;
        }

        visited.insert(cur_canonical);
        const auto &timing = timing_data.at(cur_canonical);

        // 如果没有 fanout，说明到达了终点（可能是 endpoint）
        if (timing.fanouts.empty()) {
          visited.erase(cur_canonical);
          return;
        }

        for (const auto &fo : timing.fanouts) {
          // 虚拟 reg_input 只用于电容计算，不参与 candidate 图
          if (fo.target_bit.wire_name.find("__reg_input__") == 0) {
            continue;
          }

          SignalBit next_canonical = sigmap.find(fo.target_bit);
          if (visited.count(next_canonical))
            continue;

          path_fanouts.push_back(fo);

          std::size_t end_node_id = SIZE_MAX;

          // 检查是否是 candidate bit
          auto it_end = candidate_bit_to_node_id.find(next_canonical);
          if (it_end != candidate_bit_to_node_id.end()) {
            end_node_id = it_end->second;
          }

          if (end_node_id != SIZE_MAX) {
            // 找到下一个 candidate 节点，创建路径

            CandidatePath path;
            path.id = candidate_graphy_.paths.size();
            path.start_node = start_node_id;
            path.end_node = end_node_id;
            path.next_path = std::nullopt;
            path.fanouts = path_fanouts;

            const std::size_t path_id = path.id;
            candidate_graphy_.nodes[start_node_id].fanout_paths.push_back(
                path_id);
            candidate_graphy_.paths.push_back(std::move(path));

            // startpoint：顶层 input 或 clk，入队供 PBA 传播
            const CandidateNode &start_node =
                candidate_graphy_.nodes[start_node_id];
            bool is_startpoint = (start_node.inst == nullptr) ||
                                 (cur_canonical.wire_name == "__clk__");
            if (is_startpoint)
              candidate_timing_queue.push_back(path_id);

            // 找到 candidate 节点后，不再继续深入（回溯）
          } else {
            // 继续向前探索
            dfs(start_node_id, next_canonical);
          }

          path_fanouts.pop_back();
        }

        visited.erase(cur_canonical);
      };

  // 从每个 candidate 节点对应的 bit 出发做 DFS 建路径
  for (const auto &[bit, node_id] : candidate_bit_to_node_id) {
    visited.clear();
    path_fanouts.clear();
    dfs(node_id, bit);
  }

  // 调试：在这里打印一下path相关的内容
  // 打印节点信息
  std::unordered_set<std::size_t> startpoint_nodes;
  for (const auto &node : candidate_graphy_.nodes) {
    if (node.inst == nullptr) {
      startpoint_nodes.insert(node.id);
    } else {
      // 检查是否是时钟节点：通过查找对应的 SignalBit 是否为 __clk__
      SignalSpec spec;
      auto it = node.inst->connections.find(node.port_name);
      if (it != node.inst->connections.end() && !it->second.empty()) {
        SignalBit rep = sigmap.find(it->second[0]);
        if (rep.wire_name == "__clk__") {
          startpoint_nodes.insert(node.id);
        }
      }
    }
  }

  std::cout << "\n=== NODES ===\n";
  for (const auto &node : candidate_graphy_.nodes) {
    bool is_startpoint = startpoint_nodes.count(node.id) > 0;

    std::cout << "  [" << std::setw(3) << node.id << "] ";
    if (is_startpoint) {
      std::cout << "[START] ";
    }

    if (node.inst == nullptr) {
      std::cout << "PORT: ";
    } else {
      std::cout << "INST: ";
      std::cout << node.inst->instance_name << " (" << node.inst->module_name
                << ") ";
    }
    std::cout << "port:\"" << node.port_name << "\"";

    std::cout << " | fanout_paths: " << node.fanout_paths.size();
    if (!node.fanout_paths.empty()) {
      std::cout << " -> [";
      for (size_t i = 0; i < node.fanout_paths.size() && i < 5; ++i) {
        if (i > 0)
          std::cout << ", ";
        std::cout << node.fanout_paths[i];
      }
      if (node.fanout_paths.size() > 5) {
        std::cout << ", ...";
      }
      std::cout << "]";
    }
    std::cout << "\n";
  }

  // 打印路径边信息
  std::cout << "\n=== PATHS ===\n";
  for (const auto &path : candidate_graphy_.paths) {
    bool is_startpath = startpoint_nodes.count(path.start_node) > 0;

    std::cout << "  [" << std::setw(3) << path.id << "] ";
    if (is_startpath) {
      std::cout << "[START] ";
    }

    std::cout << "Node[" << path.start_node << "] -> Node[" << path.end_node
              << "]";

    if (path.next_path.has_value()) {
      std::cout << " -> Path[" << path.next_path.value() << "]";
    } else {
      std::cout << " [END]";
    }

    std::cout << " | segments: " << path.fanouts.size();

    // 显示 fanout 详细信息
    if (!path.fanouts.empty()) {
      std::cout << " | ";
      if (path.fanouts.size() <= 3) {
        for (size_t i = 0; i < path.fanouts.size(); ++i) {
          if (i > 0)
            std::cout << " -> ";
          const auto &fo = path.fanouts[i];
          std::cout << fo.target_bit.wire_name << "["
                    << fo.target_bit.bit_offset << "]";
          if (fo.delay > 0) {
            std::cout << " (+" << std::fixed << std::setprecision(1) << fo.delay
                      << "ps)";
          }
        }
      } else {
        const auto &first = path.fanouts.front();
        const auto &last = path.fanouts.back();
        std::cout << first.target_bit.wire_name << "["
                  << first.target_bit.bit_offset << "]";
        std::cout << " -> ... -> ";
        std::cout << last.target_bit.wire_name << "["
                  << last.target_bit.bit_offset << "]";
        if (last.delay > 0) {
          std::cout << " (+" << std::fixed << std::setprecision(1) << last.delay
                    << "ps)";
        }
      }
    }
    std::cout << "\n";
  }
}

void STAWorker::caculate_candidate_path_arc() {
  candidate_graphy_.candidate_path_res.clear();

  if (!cell_library_) {
    std::cerr
        << "[ERROR] cell_library_ is null in caculate_candidate_path_arc\n";
    return;
  }

  // 对每一条 CandidatePath 预先计算两种情况的等效延迟：
  // - is_rise = true  : 起点为上升沿
  // - is_rise = false : 起点为下降沿
  for (const auto &path : candidate_graphy_.paths) {
    auto caculate_path = [&](bool is_rise_start) -> double {
      double total_delay = 0.0;
      double prev_slew = 0.0;

      // 当前沿方向（随路径传播）
      TransitionDirection cur_dir = is_rise_start
                                        ? TransitionDirection::RISING
                                        : TransitionDirection::FALLING;

      for (const auto &fo : path.fanouts) {
        Instance *inst = fo.cell;
        if (!inst) {
          continue;
        }

        const auto *cell = cell_library_->get_cell(inst->module_name);
        if (!cell) {
          std::cerr << "[WARNING] cannot find cell " << inst->module_name
                    << " in library when calc candidate path arc\n";
          continue;
        }

        // 目标信号（输出 net）
        SignalBit dst_canonical = sigmap.find(fo.target_bit);

        double segment_delay = 0.0;
        bool found_arc = false;

        // 遍历该单元的所有输出 pin，找到真正驱动 dst_canonical 的 pin
        auto output_pins = cell->get_output_pins();
        for (const auto &output_pin_name : output_pins) {
          const auto *output_pin = cell->get_pin(output_pin_name);
          if (!output_pin) {
            continue;
          }

          auto it_conn = inst->connections.find(output_pin_name);
          if (it_conn == inst->connections.end() || it_conn->second.empty()) {
            continue;
          }

          bool match_output = false;
          for (const auto &sig : it_conn->second) {
            if (sigmap.find(sig) == dst_canonical) {
              match_output = true;
              break;
            }
          }
          if (!match_output) {
            continue;
          }

          // 找到输出 pin 后，计算 load_cap
          double load_cap = 0.0;
          if (inst->load_capacitance.count(output_pin_name)) {
            load_cap = inst->load_capacitance.at(output_pin_name);
          }

          // 在该输出 pin 的 timing_arcs 中，找到 related_pin == fo.port_name
          // 的弧
          for (const auto &arc : output_pin->timing_arcs) {
            bool is_combinational =
                (arc.timing_type == celllib::TimingType::COMBINATIONAL);
            bool is_clock_to_q =
                (arc.timing_type == celllib::TimingType::RISING_EDGE ||
                 arc.timing_type == celllib::TimingType::FALLING_EDGE);

            if (!is_combinational && !is_clock_to_q) {
              continue;
            }
            if (arc.related_pin != fo.port_name) {
              continue;
            }

            TransitionDirection out_dir =
                specualte_transition_direction(is_clock_to_q, arc, cur_dir);

            // 对该弧进行 LUT 计算：rise / fall delay
            double d = 0.0;
            if (out_dir == TransitionDirection::RISING) {
              d = caculate_delay_rise(arc, cell_library_, prev_slew, load_cap);
            } else if (out_dir == TransitionDirection::FALLING) {
              d = caculate_delay_fall(arc, cell_library_, prev_slew, load_cap);
            } else {
              assert(false && "should not reach here");
            }

            double cur_slew = 0.0;
            if (out_dir == TransitionDirection::RISING) {
              cur_slew = caculate_transition_rise(arc, cell_library_, prev_slew,
                                                  load_cap) /
                         1000;
            } else if (out_dir == TransitionDirection::FALLING) {
              cur_slew = caculate_transition_fall(arc, cell_library_, prev_slew,
                                                  load_cap) /
                         1000;
            } else {
              assert(false && "should not reach here");
            }

            cur_dir = out_dir;
            segment_delay = d;
            found_arc = true;
            prev_slew = cur_slew;
          }

          if (found_arc) {
            break; // 已经在该输出 pin 上找到匹配的 arc
          }
        }

        total_delay += segment_delay;
      }
      return total_delay;
    };

    CandidatePathArcKey key_rise{path.id, true};
    CandidatePathArcKey key_fall{path.id, false};

    double delay_rise = caculate_path(true);
    double delay_fall = caculate_path(false);

    candidate_graphy_.candidate_path_res[key_rise] = delay_rise;
    candidate_graphy_.candidate_path_res[key_fall] = delay_fall;
  }
}

void STAWorker::display_candidate_path() {
  // 识别 startpoint 节点（clk 或顶层 input）
  std::unordered_set<std::size_t> startpoint_nodes;
  for (const auto &node : candidate_graphy_.nodes) {
    if (node.inst == nullptr) {
      startpoint_nodes.insert(node.id);
    } else {
      auto it = node.inst->connections.find(node.port_name);
      if (it != node.inst->connections.end() && !it->second.empty()) {
        SignalBit rep = sigmap.find(it->second[0]);
        if (rep.wire_name == "__clk__") {
          startpoint_nodes.insert(node.id);
        }
      }
    }
  }

  if (!cell_library_) {
    std::cerr << "[ERROR] cell_library_ is null in display_candidate_path\n";
    assert(false);
  }

  std::cout << "\n=== Candidate Paths Detailed View ===\n";

  // 对每条 CandidatePath，分别以起点为 rise/fall 打印
  for (auto &path : candidate_graphy_.paths) {
    bool is_startpath = startpoint_nodes.count(path.start_node) > 0;

    for (bool is_rise_start : {true, false}) {
      char start_dir_ch = is_rise_start ? 'r' : 'f';

      std::cout << "\nPath [" << path.id << "] "
                << (is_startpath ? "[START] " : "")
                << "(start_dir=" << start_dir_ch << ")\n";

      const CandidateNode &start_node =
          candidate_graphy_.nodes[path.start_node];
      const CandidateNode &end_node = candidate_graphy_.nodes[path.end_node];

      std::cout << "  From Node[" << start_node.id << "] ";
      if (start_node.inst) {
        std::cout << start_node.inst->instance_name << "("
                  << start_node.inst->module_name << ")/";
      }
      std::cout << start_node.port_name << "  ->  Node[" << end_node.id << "] ";
      if (end_node.inst) {
        std::cout << end_node.inst->instance_name << "("
                  << end_node.inst->module_name << ")/";
      }
      std::cout << end_node.port_name << "\n";

      // 路径内部逐段打印：方向 + delay
      TransitionDirection cur_dir = is_rise_start
                                        ? TransitionDirection::RISING
                                        : TransitionDirection::FALLING;
      double total_delay = 0.0;
      double prev_slew = 0.0;

      for (size_t i = 0; i < path.fanouts.size(); ++i) {
        const auto &fo = path.fanouts[i];
        Instance *inst = fo.cell;

        std::cout << "    [" << i << "] ";
        if (inst) {
          std::cout << inst->instance_name << "(" << inst->module_name << ")/"
                    << fo.port_name << " -> ";
        }
        std::cout << fo.target_bit.wire_name << "[" << fo.target_bit.bit_offset
                  << "]";

        if (!inst) {
          std::cerr
              << "\n[ERROR] fanout.cell is null in display_candidate_path\n";
          assert(false);
        }

        const auto *cell = cell_library_->get_cell(inst->module_name);
        if (!cell) {
          std::cerr << "\n[ERROR] Cannot find cell " << inst->module_name
                    << " in library when printing candidate path.\n";
          assert(false);
        }

        // 找到对应输出 pin（真正驱动 fo.target_bit 的 pin）
        const celllib::Pin *output_pin = nullptr;
        std::string output_pin_name;
        for (const auto &opn : cell->get_output_pins()) {
          auto it_conn = inst->connections.find(opn);
          if (it_conn == inst->connections.end() || it_conn->second.empty())
            continue;
          bool match = false;
          for (const auto &sig : it_conn->second) {
            if (sigmap.find(sig) == sigmap.find(fo.target_bit)) {
              match = true;
              break;
            }
          }
          if (match) {
            output_pin_name = opn;
            output_pin = cell->get_pin(opn);
            break;
          }
        }
        if (!output_pin) {
          std::cerr << "\n[ERROR] Cannot find output pin for fanout segment "
                       "in candidate path.\n";
          assert(false);
        }

        double load_cap = 0.0;
        if (inst->load_capacitance.count(output_pin_name)) {
          load_cap = inst->load_capacitance.at(output_pin_name);
        }

        double seg_delay = 0.0;

        // 找到 related_pin == fo.port_name 的 timing arc，计算本段 delay 和方向
        for (const auto &arc : output_pin->timing_arcs) {
          bool is_combinational =
              (arc.timing_type == celllib::TimingType::COMBINATIONAL);
          bool is_clock_to_q =
              (arc.timing_type == celllib::TimingType::RISING_EDGE ||
               arc.timing_type == celllib::TimingType::FALLING_EDGE);

          if (!is_combinational && !is_clock_to_q)
            continue;
          if (arc.related_pin != fo.port_name)
            continue;

          TransitionDirection out_dir =
              specualte_transition_direction(is_clock_to_q, arc, cur_dir);

          // 对该弧进行 LUT 计算：rise / fall delay
          double d = 0.0;
          if (out_dir == TransitionDirection::RISING) {
            d = caculate_delay_rise(arc, cell_library_, prev_slew, load_cap);
          } else if (out_dir == TransitionDirection::FALLING) {
            d = caculate_delay_fall(arc, cell_library_, prev_slew, load_cap);
          } else {
            assert(false && "should not reach here");
          }

          double cur_slew = 0.0;
          if (out_dir == TransitionDirection::RISING) {
            cur_slew = caculate_transition_rise(arc, cell_library_, prev_slew,
                                                load_cap) /
                       1000;
          } else if (out_dir == TransitionDirection::FALLING) {
            cur_slew = caculate_transition_fall(arc, cell_library_, prev_slew,
                                                load_cap) /
                       1000;
          } else {
            assert(false && "should not reach here");
          }

          // 要求精确：不允许 UNKNOWN
          if (out_dir == TransitionDirection::UNKNOWN) {
            std::cerr
                << "\n[ERROR] UNKNOWN transition direction encountered in "
                   "candidate path ("
                << inst->instance_name << "/" << fo.port_name << " -> "
                << output_pin_name << ").\n";
            assert(false);
          }

          char dir_ch = '?';
          if (out_dir == TransitionDirection::RISING) {
            dir_ch = 'r';
          } else if (out_dir == TransitionDirection::FALLING) {
            dir_ch = 'f';
          }

          seg_delay = d;
          cur_dir = out_dir;

          std::cout << " dir=" << dir_ch << " delay=" << d << "ps"
                    << " slew=" << prev_slew << "ns" << " load=" << load_cap
                    << "uf";

          prev_slew = cur_slew;
           // ===== 只在“最后一个节点是寄存器”时计算 setup =====
          bool is_last_segment = (i == path.fanouts.size() - 1);
          if (is_last_segment && path.next_path == std::nullopt) {
            const CandidateNode &end_node = candidate_graphy_.nodes[path.end_node];
            if (end_node.inst) {
              const auto *end_cell =
                  cell_library_->get_cell(end_node.inst->module_name);
              if (end_cell) {
                const auto *d_pin = end_cell->get_pin(end_node.port_name);
                if (d_pin) {
                  // 用当前这条弧的约束 + data/clk transition 来算 setup
                  double data_trans = prev_slew; // 这里是 ns
                  double clk_trans = 0.0;        // 简单先写 0，有需要可换成 cfg 里的时钟 slew

                  double setup_r =
                      caculate_setup_rise(arc, cell_library_, data_trans, clk_trans);
                  double setup_f =
                      caculate_setup_fall(arc, cell_library_, data_trans, clk_trans);

                  // 保持和 STA 主流程一致：内部用 ps
                  if (cur_dir == TransitionDirection::RISING) {
                    path.setup = setup_r * 1000.0;
                  } else {
                    path.setup = setup_f * 1000.0;
                  }
                }
              }
            }

            std::cout << " setup: " << path.setup << "ps" << " hold: " << path.hold << "ps";
          }
        }
        total_delay += seg_delay;

        std::cout << "  | cum=" << total_delay << "ps\n";
      }

      std::cout << "  Total path delay (start_dir=" << start_dir_ch
                << "): " << total_delay << "ps\n";
    }
  }
}

void STAWorker::run_candidate_graphy_dfs() {
  auto get_path_res = [&](std::size_t path_id, bool is_rise) -> double {
    CandidatePathArcKey key{path_id, is_rise};
    auto it = candidate_graphy_.candidate_path_res.find(key);
    if (it == candidate_graphy_.candidate_path_res.end()) {
      std::cerr << "[WARNING] candidate_path_res missing: path_id=" << path_id
                << " is_rise=" << (is_rise ? "true" : "false") << "\n";
      return 0.0;
    }
    return it->second;
  };

  res.paths.clear();
  res.points.clear();
  res.point_index.clear();

  // 为每个 CandidateNode 建立一个 TimingPointRef，并记录 node_id -> point_id 的映射
  std::vector<std::size_t> node_point_idx(candidate_graphy_.nodes.size(), 0);

  auto make_point = [&](const CandidateNode &node) -> std::size_t {
    // 构造 key（用于去重）
    TimingPointRefKey key{};
    key.inst = node.inst;
    key.port_name = node.port_name;

    // 尝试解析 canonical bit：顶层端口或实例端口
    std::optional<SignalBit> bit_opt = std::nullopt;
    if (node.inst == nullptr) {
      // 顶层端口
      SignalSpec spec = get_signal_bits(node.port_name);
      if (!spec.empty()) {
        bit_opt = sigmap.find(spec[0]);
      }
    } else {
      auto it = node.inst->connections.find(node.port_name);
      if (it != node.inst->connections.end() && !it->second.empty()) {
        bit_opt = sigmap.find(it->second[0]);
      }
    }
    key.bit = bit_opt;

    auto it = res.point_index.find(key);
    if (it != res.point_index.end()) {
      return it->second;
    }

    std::size_t id = res.points.size();
    TimingPointRef p{};
    p.id = id;
    p.inst = key.inst;
    p.port_name = key.port_name;
    p.bit = key.bit;

    res.points.push_back(p);
    res.point_index.emplace(key, id);
    return id;
  };

  for (const auto &node : candidate_graphy_.nodes) {
    node_point_idx[node.id] = make_point(node);
  }

  for (const auto &path : candidate_graphy_.paths) {
    const auto &end_node = candidate_graphy_.nodes[path.end_node];

    // 这里做了简化，如果最后的fanout不是空的，要做拼接
    // 仅处理 fanout 为空的路径（终点）
    if (!end_node.fanout_paths.empty()) {
      continue;
    }

    double rise_rs = get_path_res(path.id, true);
    double fall_rs = get_path_res(path.id, false);
    double total_delay = std::max(rise_rs, fall_rs);

    TimingPathResult pr;
    pr.mode = AnalysisMode::MAX; // 当前 candidate 仅用于 max 分析
    // group 在报告阶段通过起终点再分类（classify_start_end_type）

    pr.startpoint = node_point_idx[path.start_node];
    pr.endpoint = node_point_idx[path.end_node];

    // 先把整条路径视作单步：incr=total_delay，arrival=total_delay
    pr.data_arrival_time = total_delay;
    pr.data_required_time = total_delay; // 暂定 required == arrival，slack=0
    pr.slack = 0.0;

    TimingStep step;
    step.start_point = pr.startpoint;
    step.end_point = pr.endpoint;
    step.incr = total_delay;
    step.arrival = total_delay;
    step.dir = (rise_rs >= fall_rs) ? TransitionDirection::RISING
                                    : TransitionDirection::FALLING;

    pr.steps.push_back(step);
    res.paths.push_back(std::move(pr));
  }
}

} // namespace sta
