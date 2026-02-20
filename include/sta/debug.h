
#include "sta_data_structures.hpp"
#include <cstddef>

// 编译期调试开关（统一放在此处，便于查找与修改）
namespace sta {
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
} // namespace sta

void fanout_debuger(sta::STAWorker &worker);
void run_debuger(sta::STAWorker &worker, bool verbose);

int setup_hold_test(int argc, char *argv[]);

void gcd_test();
void singal_test(char *file_name);
void auto_test(int /*argc*/, char * /*argv*/[]);
void debug_lib_cell();
void spi_test();
void test_lut(char *cell_name, char *pin_name, char *related_pin, double cap, double slew);

namespace sta {
    void display_longest_path(sta::STAWorker &worker);
    void display_points_fanout(sta::STAWorker &worker, std::size_t pt_no);
    void show_lib_details(const char *cell_name, celllib::CellLibrary lib);
    void debug_paths_through_instance(STAWorker &worker,const std::string &inst_substr);
}