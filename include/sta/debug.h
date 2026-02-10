
#include "sta_data_structures.hpp"
#include <cstddef>

void fanout_debuger(sta::STAWorker &worker);
void run_debuger(sta::STAWorker &worker, bool verbose);

int setup_hold_test(int argc, char *argv[]);

void gcd_test();
void singal_test(char *file_name);
void auto_test(int /*argc*/, char * /*argv*/[]);
void debug_lib_cell();
void spi_test();
void test_lut();

namespace sta {
    void display_longest_path(sta::STAWorker &worker);
    void display_points_fanout(sta::STAWorker &worker, std::size_t pt_no);
    void show_lib_details(char *cell_name, celllib::CellLibrary lib);
    void debug_paths_through_instance(STAWorker &worker,const std::string &inst_substr);
}