#include <cstdlib>
#include <iostream>
#include <string>

#include "cell/cell_data_structure.hpp"
#include "cell/liberty_parser.hpp"
#include "sta/sta_data_structures.hpp"
#include "sta/sta_report.hpp"
#include "sdc/sdc_parser.hpp"

#include "sta/debug.h"

#include "interface.hpp"

int sta_main(int argc, char *argv[]) {
  // 创建 worker 实例
  sta::STAWorker worker;
  celllib::CellLibrary cell_lib;
  MyVerilogParser verilog_parser(worker);
  MySDCParser sdc_interface(worker);
  MyCellLibParser lib_parser(cell_lib);
  
  // 创建并解析 SDC 解析器，传入接口实现
  sdc::SDCParser sdc_parser(&sdc_interface);  
  if(argc > 1) {
    std::string sdc_file = argv[1];
    std::cout << "Parsing SDC file: " << sdc_file << std::endl;
    sdc_parser.parse_file(sdc_file);
  }
  std::cout << "finish parse SDC file" << std::endl;

  if(sdc_interface.celllib_file_name.empty()) {
    std::cout << "not stanard lib specific, abort" << std::endl;
    assert(false && "no stanard lib specificed");
  }

  lib_parser.parse_from_file(sdc_interface.celllib_file_name);
  worker.set_cell_library(cell_lib);

  // parse verilog file
  verilog_parser.set_filename(sdc_interface.verilog_file_name);
  if(verilog_parser.get_filename().empty()) {
    std::cout << "no verilog file specific, use \"/home/ysyx/project/pba-sta-base/proj/Testing/reg.v \"\n" << std::endl;
    verilog_parser.read("/home/ysyx/project/pba-sta-base/proj/Testing/reg.v");
  } else {
    verilog_parser.read_with_filename();
  }
  // 三阶段时序分析流程
  std::cout << "Step 1: build_fanouts()..." << std::endl;
  worker.build_fanouts();
  std::cout << "Step 2: calculate_load_capacitance()..." << std::endl;
  worker.calculate_load_capacitance();
  std::cout << "Step 3: calculate_timing_arcs()..." << std::endl;
  worker.calculate_timing_arcs();
  std::cout << "✓ Three-stage flow completed" << std::endl;

  // fanout_debuger(worker);

  // run_debuger(worker, true);
  worker.run();
  worker.print_all_timing_paths_bfs();
  // worker.run_dfs();
  // std::cout << "\n=== DFS 枚举所有时序路径 ===\n";
  // worker.print_all_timing_paths_dfs();

  // 设置时钟配置
  if(worker.get_config().clk_period == 0) {
    std::cout << "no clk period specific, use 10" << std::endl;
    worker.get_config().clk_period = 100;
  }
  worker.sta_check(worker.get_config().clk_period);
  
  // 使用新的解耦报告生成器
  sta::STAReportGenerator::generate_report(worker, "__clk__");

  return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
  // return test_celllib_parser(argc, argv);
  // return test_all_sta_functions(argc, argv);
  // return test_transition_calculation(argc, argv);

  return sta_main(argc, argv);
  // return setup_hold_test(argc, argv);
}