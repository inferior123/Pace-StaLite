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

int sta_main(int argc, char *argv[]) {
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
    std::cout << "not stanard lib specific, abort" << std::endl;
    assert(false && "no stanard lib specificed");
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
  fanout_debuger(worker);
  std::cout << "Step 2: calculate_load_capacitance()..." << std::endl;
  worker.calculate_load_capacitance_dfs();

  std::cout << "Step 4: run_timing_analysis_dfs()..." << std::endl;
  worker.run_timing_analysis_dfs();

  // 设置时钟配置
  if (worker.get_config().clk_period == 0) {
    std::cout << "no clk period specified, use 1000ps (1ns)" << std::endl;
    worker.get_config().clk_period = 100;
  }

  // 生成与 PT 格式一致的 8 个 timing 报告文件，便于与 ref 对比
  std::string design_name =
      worker.top_moudle.empty() ? "design" : worker.top_moudle;
  std::string report_dir = "./result/dfs/" + design_name;
  sta::STAReportGenerator::generate_report_pt_files(worker, "__clk__",
                                                    report_dir, design_name, 1);

  return EXIT_SUCCESS;
}

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
    std::cout << "not stanard lib specific, abort" << std::endl;
    assert(false && "no stanard lib specificed");
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
  worker.caculate_candidate_load_cap();
  std::cout << "Step 3: build_candidate_graphy()..." << std::endl;
  worker.build_candidate_graphy_dfs();

  worker.run_candidate_graphy_dfs();

  // 生成与 PT 格式一致的 8 个 timing 报告文件，便于与 ref 对比
  std::string design_name =
      worker.top_moudle.empty() ? "design" : worker.top_moudle;
  std::string report_dir = "./result/candidate/" + design_name;
  sta::STAReportGenerator::generate_report_pt_files(worker, "__clk__",
                                                    report_dir, design_name, 1);
  return 0;
}

int main(int argc, char *argv[]) {
  // return sta_main(argc, argv);
  // return candidate_test(argc, argv);
  // gcd_test();
  //

  if (argc > 1)
    singal_test(argv[1]);
  else
    auto_test(argc, argv);

  // spi_test();
  test_lut();

  // debug_lib_cell();

  return 0;
}
