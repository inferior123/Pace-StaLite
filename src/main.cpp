#include <cstdlib>
#include <iostream>
#include <string>

#include "cell/cell_data_structure.hpp"
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

  for (const auto &file : sdc_interface.celllib_file_name) {
    std::cout << "liberty parser handle file " << file << std::endl;
    lib_parser.parse_from_file(file);
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
  worker.calculate_load_capacitance();
  std::cout << "Step 3: calculate_timing_arcs()..." << std::endl;
  worker.calculate_timing_arcs();
  std::cout << "✓ Three-stage flow completed" << std::endl;

  // fanout_debuger(worker);

  // run_debuger(worker, true);
  // worker.run();
  worker.run_dfs();
  std::cout << "\n=== DFS 枚举所有时序路径 ===\n";
  worker.print_all_timing_paths_dfs();

  // 设置时钟配置
  if (worker.get_config().clk_period == 0) {
    std::cout << "no clk period specified, use 1000ps (1ns)" << std::endl;
    worker.get_config().clk_period = 100;
  }
  worker.sta_check();


  // 生成与 PT 格式一致的 8 个 timing 报告文件，便于与 ref 对比
  std::string design_name = worker.top_moudle.empty() ? "design" : worker.top_moudle;
  std::string report_dir = "./result/dfs/" + design_name;
  sta::STAReportGenerator::generate_report_pt_files(worker, "__clk__", report_dir, design_name);

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

  for (const auto &file : sdc_interface.celllib_file_name) {
    std::cout << "liberty parser handle file " << file << std::endl;
    lib_parser.parse_from_file(file);
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
  worker.calculate_load_capacitance();
  std::cout << "Step 3: build_candidate_graphy()..." << std::endl;
  worker.build_candidate_graphy_dfs();
  worker.caculate_candidate_path_arc();

  worker.display_candidate_path();
  worker.run_candidate_graphy_dfs();

  // 生成与 PT 格式一致的 8 个 timing 报告文件，便于与 ref 对比
  std::string design_name = worker.top_moudle.empty() ? "design" : worker.top_moudle;
  std::string report_dir = "./result/candidate/" + design_name;
  sta::STAReportGenerator::generate_report_pt_files(worker, "__clk__", report_dir, design_name);
  return 0;
}

#include <filesystem>
namespace fs = std::filesystem;

void auto_test(int /*argc*/, char* /*argv*/[]) {
  // 1) 只解析一次 liberty，所有 design 共享同一个 cell_lib
  celllib::CellLibrary cell_lib;
  MyCellLibParser lib_parser(cell_lib);

  lib_parser.parse_from_file("/home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CH/liberty/ics55_LLSC_H7CH_typ_tt_1p2_25_nldm.lib");
  lib_parser.parse_from_file("/home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CR/liberty/ics55_LLSC_H7CR_typ_tt_1p2_25_nldm.lib");
  lib_parser.parse_from_file("/home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CL/liberty/ics55_LLSC_H7CL_typ_tt_1p2_25_nldm.lib");

  // 2) 枚举 Testing/ics55 目录下所有 .v 文件
  std::vector<fs::path> verilog_files;
  const fs::path root_dir = "/home/ysyx/project/pba-sta-base/proj/Testing/ics55";
  for (auto& entry : fs::recursive_directory_iterator(root_dir)) {
    if (!entry.is_regular_file())
      continue;
    if (entry.path().extension() == ".v") {
      verilog_files.push_back(entry.path());
    }
  }

  if (verilog_files.empty()) {
    std::cout << "No .v files found under " << root_dir << "\n";
    return;
  }

  // 3) 对每个 verilog 设计分别跑 DFS 和 PBA，两套 worker 互不干扰
  for (const auto& vpath : verilog_files) {
    std::string vfile = vpath.string();
    std::cout << "\n========================================\n";
    std::cout << "Design: " << vfile << "\n";
    std::cout << "========================================\n";

    // // ---------- DFS worker ----------
    // {
    //   sta::STAWorker worker;
    //   worker.set_cell_library(cell_lib);
    //   MyVerilogParser verilog_parser(worker);

    //   verilog_parser.read(vfile.c_str());

    //   std::cout << "  [DFS] Step 1: build_fanouts()...\n";
    //   worker.build_fanouts();
    //   std::cout << "  [DFS] Step 2: calculate_load_capacitance()...\n";
    //   worker.calculate_load_capacitance();
    //   std::cout << "  [DFS] Step 3: calculate_timing_arcs()...\n";
    //   worker.calculate_timing_arcs();

    //   if (worker.get_config().clk_period == 0) {
    //     worker.get_config().clk_period = 1000; // 1ns = 1000ps
    //   }

    //   worker.run_dfs();
    //   worker.print_all_timing_paths_dfs();
    //   worker.sta_check();

    //   std::string design_name =
    //       worker.top_moudle.empty() ? "design" : worker.top_moudle;
    //   std::string report_dir = "./result1/dfs/" + design_name;
    //   sta::STAReportGenerator::generate_report_pt_files(
    //       worker, "__clk__", report_dir, design_name);
    // }

    // ---------- PBA / candidate worker ----------
    {
      sta::STAWorker worker;
      worker.set_cell_library(cell_lib);
      MyVerilogParser verilog_parser(worker);

      verilog_parser.read(vfile.c_str());

      std::cout << "  [PBA] Step 1: build_fanouts()...\n";
      worker.build_fanouts();
      std::cout << "  [PBA] Step 2: calculate_load_capacitance()...\n";
      worker.calculate_load_capacitance();
      std::cout << "  [PBA] Step 3: build_candidate_graphy()...\n";
      worker.build_candidate_graphy_dfs();
      worker.caculate_candidate_path_arc();
      worker.display_candidate_path();
      worker.run_candidate_graphy_dfs(); // 填充 worker.res

      std::string design_name =
          worker.top_moudle.empty() ? "design" : worker.top_moudle;
      std::string report_dir = "./result1/candidate/" + design_name;
      sta::STAReportGenerator::generate_report_pt_files(
          worker, "__clk__", report_dir, design_name);
    }
  }
}

int main(int argc, char *argv[]) {
  // return test_celllib_parser(argc, argv);
  // return test_all_sta_functions(argc, argv);
  // return test_transition_calculation(argc, argv);

  // sta_main(argc, argv);
  // candidate_test(argc, argv);

  auto_test(argc, argv);

  return 0;
  // return setup_hold_test(argc, argv);
}
