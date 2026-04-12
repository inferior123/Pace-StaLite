#ifndef STA_INSTANCE_HPP
#define STA_INSTANCE_HPP

#include "sta_signal.hpp"

#include <string>
#include <unordered_map>

namespace sta {

/**
 * 单元实例：表示 Verilog 中的模块实例
 */
struct Instance {
  std::string module_name;
  std::string instance_name;

  std::unordered_map<std::string, SignalSpec> connections;

  Instance(const std::string &mod, const std::string &inst)
      : module_name(mod), instance_name(inst) {}
};

} // namespace sta

#endif // STA_INSTANCE_HPP
