#ifndef STA_SIGNAL_HPP
#define STA_SIGNAL_HPP

#include <cassert>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sta {

/**
 * 信号位：表示一个信号的一位
 * 由 (wire_name, bit_offset) 唯一标识
 */
struct SignalBit {
  std::string wire_name;
  int bit_offset;

  SignalBit() : bit_offset(0) {}
  SignalBit(const std::string &name, int offset)
      : wire_name(name), bit_offset(offset) {}

  bool operator==(const SignalBit &other) const {
    return wire_name == other.wire_name && bit_offset == other.bit_offset;
  }

  bool operator!=(const SignalBit &other) const { return !operator==(other); }

  bool operator<(const SignalBit &other) const {
    if (wire_name != other.wire_name)
      return wire_name < other.wire_name;
    return bit_offset < other.bit_offset;
  }
};

struct SignalBitHash {
  std::size_t operator()(const SignalBit &bit) const {
    return std::hash<std::string>{}(bit.wire_name) ^
           (std::hash<int>{}(bit.bit_offset) << 1);
  }
};

using SignalSpec = std::vector<SignalBit>;

/**
 * 信号映射表：使用 Union-Find 将连接的信号映射到规范代表（简化版，适合原型工具）
 */
class SignalMap {
private:
  mutable std::unordered_map<SignalBit, SignalBit, SignalBitHash> parent;

  SignalBit find_root(const SignalBit &bit) const {
    auto it = parent.find(bit);
    if (it == parent.end() || it->second == bit) {
      return bit;
    }
    SignalBit root = find_root(it->second);
    parent[bit] = root;
    return root;
  }

public:
  void add_connection(const SignalSpec &from, const SignalSpec &to) {
    if (from.size() != to.size()) {
      assert(false && "the connnect size not equ");
      return;
    }

    for (size_t i = 0; i < from.size(); ++i) {
      SignalBit root_from = find_root(from[i]);
      SignalBit root_to = find_root(to[i]);

      if (root_from != root_to) {
        parent[root_from] = root_to;
      }
    }
  }

  SignalBit find(const SignalBit &bit) const {
    if (parent.find(bit) == parent.end()) {
      return bit;
    }
    return find_root(bit);
  }

  SignalBit operator()(const SignalBit &bit) { return find(bit); }

  SignalSpec operator()(const SignalSpec &sig) {
    SignalSpec result;
    for (const auto &bit : sig) {
      result.push_back(find(bit));
    }
    return result;
  }
};

} // namespace sta

#endif // STA_SIGNAL_HPP
