#include <cstdlib>
#include <iostream>
#include <string>

#include "cell/cell_data_structure.hpp"
#include "cell/celllib_cache.hpp"
#include "sdc/sdc_parser.hpp"
#include "sta/sta_data_structures.hpp"
#include "sta/sta_report.hpp"

#include "sta/debug.h"

#include "interface.hpp"

int candidate_test(int argc, char *argv[]) {
  // 创建 worker 实例
  sta::STAWorker worker;
  celllib::CellLibrary cell_lib;
  MyVerilogParser verilog_parser(worker);
  MySDCParser sdc_interface(worker);
  MyCellLibParser lib_parser(cell_lib);

  // 创建并解析 SDC 解析器，传入接口实现
  sdc::SDCParser sdc_parser(&sdc_interface);
  if (argc > 1) {
    std::string sdc_file = argv[1];
    std::cout << "Parsing SDC file: " << sdc_file << std::endl;
    sdc_parser.parse_file(sdc_file);
  }
  std::cout << "finish parse SDC file" << std::endl;

  if (sdc_interface.celllib_file_name.empty()) {
    std::cout << "not standard lib specific, abort" << std::endl;
    assert(false && "no standard lib specified");
  }

  if (!celllib::try_load_celllib_cache(sdc_interface.celllib_file_name,
                                       cell_lib)) {
    for (const auto &file : sdc_interface.celllib_file_name) {
      std::cout << "liberty parser handle file " << file << std::endl;
      lib_parser.parse_from_file(file);
    }
    celllib::save_celllib_cache(sdc_interface.celllib_file_name, cell_lib);
  }
  worker.set_cell_library(cell_lib);

  // parse verilog file
  verilog_parser.set_filename(sdc_interface.verilog_file_name);
  if (verilog_parser.get_filename().empty()) {
    std::cout << "no verilog file specific, use "
                 "\"/home/ysyx/project/pba-sta-base/proj/Testing/reg.v \"\n"
              << std::endl;
    verilog_parser.read("/home/ysyx/project/pba-sta-base/proj/Testing/reg.v");
  } else {
    verilog_parser.read_with_filename();
  }
  // 三阶段时序分析流程
  std::cout << "Step 1: build_fanouts()..." << std::endl;
  worker.build_fanouts();
  std::cout << "Step 2: calculate_load_capacitance()..." << std::endl;
  worker.calculate_load_cap();
  std::cout << "Step 3: build_candidate_graphy()..." << std::endl;
  worker.build_candidate_graphy_dfs();

  worker.run_candidate_graphy_dfs();

  // 生成与 PT 格式一致的 8 个 timing 报告文件，便于与 ref 对比
  std::string design_name =
      worker.top_module.empty() ? "design" : worker.top_module;
  std::string report_dir = "./result/candidate/" + design_name;
  sta::STAReportGenerator::generate_report_pt_files(worker, "__clk__",
                                                    report_dir, design_name, 1);
  return 0;
}

int main(int argc, char *argv[]) {

  if (argc > 6) {
    test_lut(argv[2], argv[3], argv[4], std::stod(argv[5]), std::stod(argv[6]));
  } else if (argc > 5) {
    // test_setup_hold: cell pin related_pin slew_ns（4参数，cell被忽略时可不传）
    test_setup_hold(argv[2], argv[3], argv[4], std::stod(argv[5]));
  } else if(argc > 3) {
    debug_lib_cell();
  } else if (argc > 2) {
    std::cerr << "unknown argv count" << std::endl;
  } else if (argc > 1)
    signal_test(argv[1]);
  else
    auto_test(argc, argv);

  return 0;
}
