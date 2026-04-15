#ifndef STA_WORKER_HPP
#define STA_WORKER_HPP

#include "../cell/cell_data_structure.hpp"
#include "../parser-verilog/verilog_data.hpp"
#include "sta_candidate_graph.hpp"
#include "sta_gba_graph.hpp"

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sta {

/**
 * STA 工作器：执行静态时序分析的核心类
 */
class STAWorker {
private:
  SignalMap sigmap;
  std::deque<SignalBit> timing_queue;
  std::deque<std::size_t> candidate_timing_queue;
  std::unordered_set<SignalBit, SignalBitHash> driven_signals;

  CandidateGraphy candidate_graphy_;

  GbaGraphy gba_graphy_;

  std::unordered_set<SignalBit, SignalBitHash> top_module_inputs;

  std::unordered_map<std::string, std::vector<SignalBit>> signal_registry;

  std::unordered_map<SignalBit, double, SignalBitHash> arrival_time;

  double max_arrival_time;
  SignalBit critical_signal;

  std::vector<std::unique_ptr<Instance>> instances;

  const celllib::CellLibrary *cell_library_ = nullptr;

  struct sta_config {
    int clk_period = 0;
    int clock_uncertain = 0;
    int clock_transit_raise = 0;
    int clock_transit_fall = 0;
    std::string clk_name = "clk";
  };
  sta_config cfg;
  bool has_clock = false;

  TimingRunResult res;

  std::vector<std::size_t> input_clk_point_ids;

public:
  enum class AnalysisGranularity {
    COARSE,
    MEDIUM,
    FINE
  };

  bool get_has_clock() { return has_clock; }

private:
  AnalysisGranularity analysis_granularity_ = AnalysisGranularity::MEDIUM;
  AnalysisMode analysis_mode = AnalysisMode::MAX;

  std::vector<PathEntry> reg2reg_max, in2reg_max, reg2out_max, in2out_max;
  std::vector<PathEntry> reg2reg_min, in2reg_min, reg2out_min, in2out_min;

  bool reg2reg_max_sorted = false;
  bool in2reg_max_sorted = false;
  bool reg2out_max_sorted = false;
  bool in2out_max_sorted = false;
  bool reg2reg_min_sorted = false;
  bool in2reg_min_sorted = false;
  bool reg2out_min_sorted = false;
  bool in2out_min_sorted = false;

  // --- build_fanouts（实现见 timing_graph_builder.cpp）---
  struct FanoutPendingEdge {
    std::size_t from_pt = 0;
    std::size_t to_pt = 0;
    EdgeType type = WIRE;
  };
  using FanoutBitDriverMap =
      std::unordered_map<SignalBit, std::size_t, SignalBitHash>;

  void fanout_check_preconditions() const;
  void fanout_seed_clk_input_drivers(FanoutBitDriverMap &bit_to_driver) const;
  SignalBit fanout_resolve_sequential_clock_bit(
      Instance *inst, const std::string &clock_pin_name) const;
  void fanout_process_sequential_instance(
      Instance *inst, const celllib::StandardCell &cell,
      FanoutBitDriverMap &bit_to_driver,
      std::vector<FanoutPendingEdge> &pending);
  void fanout_process_combinational_instance(
      Instance *inst, const celllib::StandardCell &cell,
      FanoutBitDriverMap &bit_to_driver,
      std::vector<FanoutPendingEdge> &pending);
  void fanout_first_pass_instances(FanoutBitDriverMap &bit_to_driver,
                                   std::vector<FanoutPendingEdge> &pending);
  static bool fanout_pending_has(const std::vector<FanoutPendingEdge> &pending,
                                 std::size_t from_pt, std::size_t to_pt);
  void fanout_patch_wires_driver_to_loads(
      const FanoutBitDriverMap &bit_to_driver,
      std::vector<FanoutPendingEdge> &pending);
  void fanout_patch_regd_secondary_pass(const FanoutBitDriverMap &bit_to_driver,
                                        std::vector<FanoutPendingEdge> &pending);
  void fanout_wire_primary_outputs(const FanoutBitDriverMap &bit_to_driver,
                                   std::vector<FanoutPendingEdge> &pending);
  void fanout_apply_pending_edges(const std::vector<FanoutPendingEdge> &pending);

  // --- build_gba_graphy（实现见 gba_engine.cpp）---
  void gba_clear_graph_structure();
  void gba_allocate_nodes_for_points();
  std::size_t gba_append_path(std::size_t from_node, std::size_t to_node,
                              double incr_ps, double slew_ns,
                              TransitionDirection dir,
                              TransitionDirection input_dir);
  void gba_compute_topo_and_break_cycles(std::size_t point_count);
  void gba_seed_input_clock_nodes();
  void gba_relax_fanout_segments(std::size_t u_pt, std::size_t v_pt,
                                 std::size_t u_node_id, std::size_t v_node_id,
                                 GbaNode &v_node, double base_delay_ps,
                                 double prev_slew_ns,
                                 TransitionDirection input_dir);
  void gba_forward_propagate_build_paths(std::size_t point_count);

public:
  STAWorker() : max_arrival_time(0) {}

  explicit STAWorker(const celllib::CellLibrary &lib)
      : max_arrival_time(0), cell_library_(&lib) {}

  AnalysisGranularity get_analysis_granularity() const {
    return analysis_granularity_;
  }
  void set_analysis_granularity(AnalysisGranularity granularity) {
    analysis_granularity_ = granularity;
  }
  AnalysisMode get_analysis_mode() const { return analysis_mode; }
  void set_analysis_mode(AnalysisMode mode) { analysis_mode = mode; }

  void collect_net(verilog::Net &net);
  void collect_port(verilog::Port &port);
  void collect_assign(verilog::Assignment &assign);
  void collect_instance(verilog::Instance &inst);

  void build_fanouts();
  void build_res_edges();
  void build_gba_graphy();
  void reset_gba_nodes_state();
  void run_gba_propagate(PointType pt_type);
  void calculate_load_cap();
  void calculate_timing_arcs();
  void run_gba_timing_analysis(bool clear_paths_first = true);
  void run_gba_backward_compute_required_and_slack();

  std::size_t get_or_create_candidate_node(std::size_t point_idx);
  std::size_t get_or_create_point_node(Instance *inst, const SignalBit &bit,
                                       const std::string &port_name);
  std::size_t get_or_create_point(Instance *inst, const std::string &port_name,
                                  std::optional<SignalBit> bit, PointType type);

  void build_candidate_graphy_dfs();
  CandidatePathSegmentResult
  compute_candidate_path_with_input(std::size_t path_id,
                                    TransitionDirection input_dir,
                                    double input_slew_ns) const;
  CandidatePathSegmentResult
  compute_one_edge_non_unate_segment(std::size_t from_pt, std::size_t to_pt,
                                     TransitionDirection output_dir,
                                     double input_slew_ns, bool use_max) const;
  void run_candidate_graphy_dfs();

  void candidate_recalculate();
  void recalculate_in2out();

  void sta_check();

  SignalSpec get_signal_bits(const std::string &signame) const;

  SignalSpec convert_to_signalspec(const verilog::RHS &rhs);
  SignalSpec convert_to_signalspec(const verilog::LHS &lhs);

  void display_result_path_detail(const TimingPathResult &pr) const;

  const PathEntry *get_top_k(PathGroup group_type, AnalysisMode mode,
                             std::size_t k);
  const PathEntry *get_last_k(PathGroup group_type, AnalysisMode mode,
                             std::size_t k);

  std::vector<PathEntry> get_path_entry(PathGroup group_type,
                                        AnalysisMode mode);
  std::vector<PathEntry> get_path_entry(PathEntryType type);

  void respath_descending(PathGroup group_type, AnalysisMode mode);
  void respath_ascending(PathGroup group_type, AnalysisMode mode);
  void respath_ascending(PathEntryType type);
  void respath_descending(PathEntryType type);
  std::size_t get_entries_size(PathEntryType type);
  std::size_t get_entries_size(PathGroup group_type, AnalysisMode mode);

  void divide_path_entry();

  std::string top_module;

  const sta_config &get_config() const { return cfg; }
  sta_config &get_config() { return cfg; }

  int get_effective_clock_period() const {
    int effective_period = cfg.clk_period;

    if (cfg.clock_uncertain > 0) {
      effective_period -= cfg.clock_uncertain;
    }

    return effective_period;
  }

  int calculate_data_required_time(int setup_time) const {
    int effective_period = get_effective_clock_period();
    return effective_period - setup_time;
  }

  double calculate_data_required_time(double setup_time) const {
    int effective_period = get_effective_clock_period();
    return (double)effective_period - setup_time;
  }

  void set_cell_library(const celllib::CellLibrary &lib) {
    cell_library_ = &lib;
  }

  bool has_cell_library() const { return cell_library_ != nullptr; }

  const celllib::CellLibrary *get_cell_library() const { return cell_library_; }

  bool is_reg(std::string name);

  bool is_top_module_input(const SignalBit &bit) const {
    SignalBit canonical = sigmap.find(bit);
    return top_module_inputs.count(canonical) > 0;
  }

  const std::vector<std::unique_ptr<Instance>> &get_instances() const {
    return instances;
  }
  const std::unordered_map<std::string, std::vector<SignalBit>> &
  get_signal_registry() const {
    return signal_registry;
  }
  const std::unordered_set<SignalBit, SignalBitHash> &
  get_driven_signals() const {
    return driven_signals;
  }
  const std::deque<SignalBit> &get_timing_queue() const { return timing_queue; }
  SignalBit get_canonical_signal(const SignalBit &bit) const {
    return sigmap.find(bit);
  }

  const CandidateGraphy &get_candidate_graphy() const {
    return candidate_graphy_;
  }

  const std::unordered_map<SignalBit, double, SignalBitHash> &
  get_arrival_time() const {
    return arrival_time;
  }
  double get_max_arrival_time() const { return max_arrival_time; }
  SignalBit get_critical_signal() const { return critical_signal; }

  TimingRunResult get_sta_res() const { return res; }
  const std::vector<std::size_t> &get_input_clk_point_ids() const {
    return input_clk_point_ids;
  }

private:
  std::pair<double, double> compute_require_and_slack(size_t path_idx);
  double compare_slack(size_t a, size_t b);
  void compute_path_setup_hold(TimingPathResult &pr) const;

  void compute_setup_hold_gba(TimingPathResult &pr);
  void gba_reset_required_state();
  void gba_seed_endpoint_requireds();
  void gba_backward_propagate_required();
  void gba_backward_rebuild_node_required_links();

  std::vector<PathEntry> *get_sorted_entries(PathGroup group_type,
                                             AnalysisMode mode);

  std::vector<PathEntry> *get_path_entry_ptr(PathGroup group_type,
                                             AnalysisMode mode);
  std::vector<PathEntry> *get_path_entry_ptr(PathEntryType type);

  void propagate_timing(const SignalBit &bit);
  void trace_critical_path();

  void trace_path(const SignalBit &endpoint_bit);
  SignalBit create_signal_bit(const std::string &name, int offset);
};

} // namespace sta

#endif // STA_WORKER_HPP
