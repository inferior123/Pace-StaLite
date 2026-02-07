#include "sta/sta_data_structures.hpp"

using namespace verilog;

namespace sta {
SignalSpec STAWorker::convert_to_signalspec(const LHS &lhs) {
  // Left hand side can be: a wire, a bit in a wire, a part of a wire
  // std::vector<std::variant<std::string, NetBit, NetRange>>
  SignalSpec result;

  for (const auto &item : lhs) {
    std::visit(
        [&result, this](const auto &elem) {
          using T = std::decay_t<decltype(elem)>;

          if constexpr (std::is_same_v<T, std::string>) {
            std::string target;
            if (is_reg(elem))
              target = "reg_" + elem + "_d";
            else
              target = elem;
            auto bits = this->get_signal_bits(target);
            result.insert(result.end(), bits.begin(), bits.end());
          } else if constexpr (std::is_same_v<T, NetBit>) {
            std::string target;
            if (is_reg(elem.name))
              target = "reg_" + elem.name + "_d";
            else
              target = elem.name;
            result.emplace_back(target, elem.bit);
          } else if constexpr (std::is_same_v<T, NetRange>) {
            int left = elem.beg;
            int right = elem.end;
            // Keep the same convention as collect_net: only support ascending
            // [left:right].
            assert(left <= right && "unsupported NetRange with left > right; "
                                    "use ascending range like [0:7]");
            std::string target;
            if (is_reg(elem.name))
              target = "reg_" + elem.name + "_d";
            else
              target = elem.name;
            for (int i = left; i <= right; ++i)
              result.emplace_back(target, i);
          } else {
            assert(false && "should not reach here");
          }
        },
        item);
  }

  return result;
}

SignalSpec STAWorker::convert_to_signalspec(const RHS &rhs) {
  // Right hand side can be: a wire, a bit in a wire, a part of a wire, a
  // constant std::vector<std::variant<std::string, NetBit, NetRange, Constant>>
  SignalSpec result;

  for (const auto &item : rhs) {
    std::visit(
        [&result, this](const auto &elem) {
          using T = std::decay_t<decltype(elem)>;

          if constexpr (std::is_same_v<T, std::string>) {
            std::string target;
            if (is_reg(elem))
              target = "reg_" + elem + "_q";
            else
              target = elem;
            auto bits = this->get_signal_bits(target);
            result.insert(result.end(), bits.begin(), bits.end());
          } else if constexpr (std::is_same_v<T, NetBit>) {
            std::string target;
            if (is_reg(elem.name))
              target = "reg_" + elem.name + "_q";
            else
              target = elem.name;
            result.emplace_back(target, elem.bit);
          } else if constexpr (std::is_same_v<T, NetRange>) {
            int left = elem.beg;
            int right = elem.end;
            assert(left <= right && "unsupported NetRange with left > right; "
                                    "use ascending range like [0:7]");
            std::string target;
            if (is_reg(elem.name))
              target = "reg_" + elem.name + "_q";
            else
              target = elem.name;
            for (int i = left; i <= right; ++i)
              result.emplace_back(target, i);
          } else if constexpr (std::is_same_v<T, Constant>) {
            // constants don't correspond to a SignalBit; ignore for
            // connectivity (for proper bit-blasting of assigns with constants,
            // handle separately)
          } else {
            assert(false && "should not reach here");
          }
        },
        item);
  }

  return result;
}

void STAWorker::collect_net(verilog::Net &net) {
  if (net.type == verilog::NetType::WIRE) {
    int left = net.beg == -1 ? 0 : net.beg;
    int right = net.end == -1 ? 0 : net.end;

    // For this prototype, only accept ascending ranges like [0:7].
    // Reject descending ranges like [7:0].
    if (left > right) {
      assert(false && "unsupported net range: left > right (e.g. [7:0]); use "
                      "ascending range like [0:7]");
    }
    for (auto &name : net.names) {
      std::vector<SignalBit> bits;
      for (int i = left; i <= right; i++) {
        sta::SignalBit bit(name, i);
        timing_data[bit] = SignalTimingData();
        bits.push_back(bit);
      }
      signal_registry[name] = std::move(bits);
    }
  } else if (net.type == verilog::NetType::REG) {
    assert(false && "should not meet a reg type in the netlist");
  } else {
    assert(false && "other type not implement yet");
  }
}

void STAWorker::collect_port(verilog::Port &port) {
  int left = port.beg == -1 ? 0 : port.beg;
  int right = port.end == -1 ? 0 : port.end;

  // only accept ranges like [0:7].
  // dont alllow ranges like [7:0].
  if (left > right) {
    assert(false && "unsupported net range: left > right (e.g. [7:0]); use "
                    "ascending range like [0:7]");
  }

  for (auto &sig_name : port.names) {
    std::vector<SignalBit> bits;
    for (int i = left; i <= right; i++) {
      SignalBit bit(sig_name, i);
      if (!timing_data.count(bit)) {
        timing_data[bit] = SignalTimingData{};
      }

      // Always add to signal_registry
      bits.push_back(bit);

      // Process based on port direction
      if (port.dir == PortDirection::INOUT) {
        assert(false && "not suport the INOUT port yet");
      } else if (port.dir == PortDirection::INPUT) {
        SignalBit input_canonical = sigmap.find(bit);
        top_module_inputs.insert(input_canonical); // 记录顶层模块输入端口
        driven_signals.insert(input_canonical);
        timing_queue.push_back(input_canonical);
        arrival_time[input_canonical] = 0;
      } else {
        // OUTPUT
        assert(port.dir == PortDirection::OUTPUT && "invalid port dir");
        SignalBit output_canonical = sigmap.find(bit);
        TimingEndpoint ep{nullptr, sig_name};
        ep.Setup_req = 0;
        ep.Hold_req = 0;
        endpoints[output_canonical].push_back(std::move(ep));
      }
    }
    signal_registry[sig_name] = std::move(bits);
  }
}

void STAWorker::collect_instance(verilog::Instance &inst) {
  // 如果使用 CellLibrary，验证单元是否存在
  if (cell_library_) {
    const auto *cell = cell_library_->get_cell(inst.module_name);
    if (!cell) {
      std::cerr << "cannot find the cell " << inst.module_name
                << " in CellLibrary" << std::endl;
      // 调试信息：打印库中所有可用的单元名称
      auto cell_names = cell_library_->get_cell_names();
      std::cerr << "Available cells in library (" << cell_names.size() << "): ";
      for (const auto &name : cell_names) {
        std::cerr << name << " ";
      }
      std::cerr << std::endl;
      assert(false && "Cell not found in CellLibrary");
    }

    auto input_pin_names = cell->get_input_pins();
    auto output_pin_names = cell->get_output_pins();

    // 将vector转换为set以便快速查找
    std::unordered_set<std::string> input_pin_set(input_pin_names.begin(),
                                                  input_pin_names.end());
    std::unordered_set<std::string> output_pin_set(output_pin_names.begin(),
                                                   output_pin_names.end());

    for (const auto &pin_name_variant : inst.pin_names) {
      // 从variant中提取pin名称
      std::string pin_name;
      if (std::holds_alternative<std::string>(pin_name_variant)) {
        pin_name = std::get<std::string>(pin_name_variant);
      } else if (std::holds_alternative<NetBit>(pin_name_variant)) {
        pin_name = std::get<NetBit>(pin_name_variant).name;
      } else if (std::holds_alternative<NetRange>(pin_name_variant)) {
        pin_name = std::get<NetRange>(pin_name_variant).name;
      }

      // 检查pin是否在input或output pin列表中
      if (input_pin_set.find(pin_name) == input_pin_set.end() &&
          output_pin_set.find(pin_name) == output_pin_set.end()) {
        std::cerr << "Invalid pin \"" << pin_name << "\" for cell \""
                  << inst.module_name << "\" (instance: " << inst.inst_name
                  << ")" << std::endl;
        std::cerr << "Available input pins: ";
        for (const auto &name : input_pin_names) {
          std::cerr << name << " ";
        }
        std::cerr << std::endl;
        std::cerr << "Available output pins: ";
        for (const auto &name : output_pin_names) {
          std::cerr << name << " ";
        }
        std::cerr << std::endl;
        assert(false && "Invalid pin name for cell");
      }
    }
  } else {
    assert(false && "cannot find standard library");
  }

  auto instance = std::make_unique<Instance>(inst.module_name, inst.inst_name);
  if (inst.pin_names.empty()) {
    assert(false &&
           "Positional port connection not supported in flattened netlist");
  }

  for (size_t i = 0; i < inst.pin_names.size() && i < inst.net_names.size();
       i++) {
    std::string port_name = std::get<std::string>(inst.pin_names[i]);
    SignalSpec signals = convert_to_signalspec(inst.net_names[i]);
    instance->connections[port_name] = std::move(signals);
  }
  instances.push_back(std::move(instance));
}

void STAWorker::collect_assign(verilog::Assignment &assign) {
  SignalSpec lhs = convert_to_signalspec(assign.lhs);
  SignalSpec rhs = convert_to_signalspec(assign.rhs);

  // 建立信号连接
  sigmap.add_connection(lhs, rhs);
}
}