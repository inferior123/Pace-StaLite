
#include "sta_data_structures.hpp"
#include <cstddef>

// 编译期调试开关（统一放在此处，便于查找与修改）
namespace sta {
/// candidate DFS 过程调试输出，置 true 启用
inline constexpr bool kDebugCandidateDfs = false;

// candidate DFS 调试打印函数
void debug_dfs_start_nodes(const std::vector<std::size_t> &node_ids,
                           const CandidateGraphy &cg);
void debug_dfs_start_node(std::size_t node_id, std::size_t pt);
void debug_dfs_push_or_continue(std::size_t end_node_id, std::size_t end_pt,
                                double delay, bool terminal,
                                TransitionDirection dir);
void debug_dfs_dup_skip();
void debug_dfs_push_path(std::size_t path_idx, std::size_t startpoint,
                         std::size_t endpoint, double arrival);
void debug_dfs_emit_chains(std::size_t node_id, std::size_t pt,
                           TransitionDirection dir, double delay_so_far,
                           std::size_t fanout_cnt, std::size_t relate_cnt);
void debug_dfs_unate_path(std::size_t path_id, std::size_t end_node_id,
                          std::size_t end_pt);
void debug_dfs_relate(std::size_t to_node_id, std::size_t to_pt);
void debug_dfs_branch(bool is_rise, std::size_t start_node_id);
void debug_dfs_total_paths(std::size_t total);

/// non-unate 段调试输出，置 true 启用
inline constexpr bool kDebugNonUnateSegment = false;
/// 只对指定 point 打印，取 static_cast<std::size_t>(-1) 表示全部
inline constexpr std::size_t kDebugNonUnateFilterPt =
    static_cast<std::size_t>(-1);

void debug_non_unate_entry(std::size_t from_pt, std::size_t to_pt,
                           const TimingPointRef &from_ref,
                           const TimingPointRef &to_ref,
                           TransitionDirection output_dir, double input_slew_ns,
                           AnalysisMode mode);
void debug_non_unate_arc(const celllib::TimingArc &arc, bool is_comb,
                         bool is_c2q, double load_cap, double delay_tmp,
                         double slew_tmp);
void debug_non_unate_summary(std::size_t from_pt, std::size_t to_pt,
                             bool has_unate, double best_delay, double best_slew,
                             double unate_delay, double unate_slew);

/// GBA setup/hold 调试输出，置 true 启用
inline constexpr bool kDebugGbaSetupHold = true;
/// 只对指定 endpoint pt 打印，取 static_cast<std::size_t>(-1) 表示全部
inline constexpr std::size_t kDebugGbaSetupHoldFilterPt = 20;

void debug_gba_setup_hold_propagate(std::size_t u_pt, std::size_t v_pt,
                                    const GbaPath &path, double cand_delay,
                                    double data_trans_ns);
void debug_gba_setup_hold_result(const TimingPathResult &pr,
                                 std::size_t v_node_id);
void debug_gba_setup_hold_lut(std::size_t endpoint, const char *arc_type,
                              double data_trans_ns, double clk_trans_ns,
                              double result_ns, double result_ps);
} // namespace sta

void fanout_debuger(sta::STAWorker &worker);
void run_debuger(sta::STAWorker &worker, bool verbose);

int setup_hold_test(int argc, char *argv[]);

void gcd_test();
void singal_test(char *file_name);
void auto_test(int /*argc*/, char * /*argv*/[]);
void debug_lib_cell();
void spi_test();
void test_lut(char *cell_name, char *pin_name, char *related_pin, double cap,
              double slew);
void test_setup_hold(const char *cell_name, const char *pin_name,
                     const char *related_pin, double slew_ns);

namespace sta {
    void display_longest_path(sta::STAWorker &worker);
    void display_points_fanout(sta::STAWorker &worker, std::size_t pt_no);
    void show_lib_details(const char *cell_name, celllib::CellLibrary lib);
    void debug_paths_through_instance(STAWorker &worker,const std::string &inst_substr);
}