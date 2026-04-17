#include <cstddef>
#include <vector>

namespace sta {

// Layered topology container:
// - layers_[0] is the first forward layer
// - layers_[n-1] is the last forward layer
// This data structure is intentionally independent from any concrete graph
// building flow, so propagation code only depends on traversal interfaces.
class TopoVisitor {
public:
  using Layer = std::vector<std::size_t>;
  using LayerContainer = std::vector<Layer>;

  void clear() { layers_.clear(); }
  bool empty() const { return layers_.empty(); }

  std::size_t layer_count() const { return layers_.size(); }

  std::size_t node_count() const {
    std::size_t count = 0;
    for (const auto &layer : layers_) {
      count += layer.size();
    }
    return count;
  }

  void set_layers(LayerContainer layers) { layers_ = std::move(layers); }
  void add_layer(Layer layer) { layers_.push_back(std::move(layer)); }

  const LayerContainer &layers() const { return layers_; }
  const Layer &layer(std::size_t idx) const { return layers_[idx]; }

  template <typename Fn>
  void for_each_forward_layer(Fn &&fn) const {
    for (std::size_t level = 0; level < layers_.size(); ++level) {
      fn(level, layers_[level]);
    }
  }

  template <typename Fn>
  void for_each_backward_layer(Fn &&fn) const {
    if (layers_.empty()) {
      return;
    }
    for (std::size_t level = layers_.size(); level-- > 0;) {
      fn(level, layers_[level]);
    }
  }

  template <typename Fn>
  void for_each_forward_node(Fn &&fn) const {
    for (const auto &topo_layer : layers_) {
      for (std::size_t vex : topo_layer) {
        fn(vex);
      }
    }
  }

  template <typename Fn>
  void for_each_backward_node(Fn &&fn) const {
    if (layers_.empty()) {
      return;
    }
    for (std::size_t level = layers_.size(); level-- > 0;) {
      for (std::size_t vex : layers_[level]) {
        fn(vex);
      }
    }
  }

private:
  LayerContainer layers_;
};

// Keep backward compatibility with the old typo name.
using TopoVistor = TopoVisitor;

} // namespace sta
