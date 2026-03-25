#include "cell/cell_data_structure.hpp"
#include "sta/sta_data_structures.hpp"
#include <cassert>
#include <string>

using namespace verilog;

namespace sta {
SignalSpec STAWorker::get_signal_bits(const std::string &signame) const {
  auto it = signal_registry.find(signame);
  if (it != signal_registry.end()) {
    return it->second;
  }

  assert(false && "trans wire name to spec, find a unknown wire");
  return {};
}

bool STAWorker::is_reg(std::string name) {
  // 遍历 instances，查找是否有对应的 REG 实例
  // REG 实例的命名规则是 "reg_" + name
  std::string expected_instance_name = "reg_" + name;
  for (const auto &instance : instances) {
    if (instance->module_name == "REG" &&
        instance->instance_name == expected_instance_name) {
      return true;
    }
  }
  return false;
}
} // namespace sta
