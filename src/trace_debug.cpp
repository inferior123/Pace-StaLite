#include "cell/cell_data_structure.hpp"
#include "cell/liberty_parser.hpp"
#include "interface.hpp"
#include "parser-verilog/verilog_data.hpp"
#include "sdc/sdc_parser.hpp"
#include "sta/sta_data_structures.hpp"
#include "sta/sta_report.hpp"
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "cell/celllib_cache.hpp"
#include "sta/debug.h"

namespace {
std::string point_name(const sta::TimingPointRef &p) {
  if (p.inst) {
    std::string s = p.inst->instance_name;
    if (!p.port_name.empty())
      s += "/" + p.port_name;
    return s;
  }
  return p.port_name.empty() ? "?" : p.port_name;
}
const char *edge_type_str(sta::EdgeType t) {
  switch (t) {
  case sta::WIRE:
    return "WIRE";
  case sta::COMB_ARC:
    return "COMB_ARC";
  case sta::SEQ_ARC:
    return "SEQ_ARC";
  default:
    return "?";
  }
}
} // namespace

void fanout_debuger(sta::STAWorker &worker) {
  const sta::TimingRunResult &res = worker.get_sta_res();
  for (size_t i = 0; i < res.points.size(); ++i) {
    const sta::TimingPointRef &pt = res.points[i];
    std::cout << "[pt" << i << "] " << point_name(pt) << " (" << pt.port_name
              << ") fanouts=" << pt.fanouts.size();
    for (const auto &e : pt.fanouts) {
      std::string tgt = e.target_point < res.points.size()
                            ? point_name(res.points[e.target_point])
                            : "?";
      std::cout << " [" << edge_type_str(e.type) << "->pt" << e.target_point
                << "(" << tgt << ")]";
    }
    std::cout << "\n";
  }
}

void fanout_test() {}

#include <filesystem>
namespace fs = std::filesystem;

void auto_test(int /*argc*/, char * /*argv*/[]) {
  // 1) 只解析一次 liberty，所有 design 共享同一个 cell_lib
  std::vector<std::string> libs;

  libs.push_back(
      "/home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/"
      "ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CH/liberty/"
      "ics55_LLSC_H7CH_typ_tt_1p2_25_nldm.lib");
  libs.push_back(
      "/home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/"
      "ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CR/liberty/"
      "ics55_LLSC_H7CR_typ_tt_1p2_25_nldm.lib");
  libs.push_back(
      "/home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/"
      "ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CL/liberty/"
      "ics55_LLSC_H7CL_typ_tt_1p2_25_nldm.lib");

  celllib::CellLibrary cell_lib;
  MyCellLibParser lib_parser(cell_lib);

  if (!celllib::try_load_celllib_cache(libs, cell_lib)) {
    for (const auto &file : libs) {
      std::cout << "liberty parser handle file " << file << std::endl;
      lib_parser.parse_from_file(file);
    }
    celllib::save_celllib_cache(libs, cell_lib);
  }


  // 2) 枚举 Testing/ics55 目录下所有 .v 文件
  std::vector<fs::path> verilog_files;
  const fs::path root_dir =
      "/home/ysyx/project/pba-sta-base/proj/Testing/ics55";
  for (auto &entry : fs::recursive_directory_iterator(root_dir)) {
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
  for (const auto &vpath : verilog_files) {
    std::string vfile = vpath.string();
    std::cout << "\n========================================\n";
    std::cout << "Design: " << vfile << "\n";
    std::cout << "========================================\n";

    {
      sta::STAWorker worker;
      worker.get_config().clk_period = 10000;

      worker.set_cell_library(cell_lib);
      MyVerilogParser verilog_parser(worker);

      verilog_parser.read(vfile.c_str());

      std::cout << "  [PBA] Step 1: build_fanouts()...\n";
      worker.build_fanouts();
      std::cout << "  [PBA] Step 2: calculate_load_capacitance()...\n";
      worker.calculate_load_capacitance();
      std::cout << "  [PBA] Step 3: build_candidate_graphy()...\n";
      worker.build_candidate_graphy_dfs();
      worker.run_candidate_graphy_dfs(); // 填充 worker.res

      std::string design_name =
          worker.top_moudle.empty() ? "design" : worker.top_moudle;
      std::string report_dir = "./result1/candidate/" + design_name;
      sta::STAReportGenerator::generate_report_pt_files(
          worker, "__clk__", report_dir, design_name, 1);
    }
  }
}

void gcd_test() {

  std::vector<std::string> libs;

  libs.push_back(
      "/home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/"
      "ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CH/liberty/"
      "ics55_LLSC_H7CH_typ_tt_1p2_25_nldm.lib");
  libs.push_back(
      "/home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/"
      "ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CR/liberty/"
      "ics55_LLSC_H7CR_typ_tt_1p2_25_nldm.lib");
  libs.push_back(
      "/home/ysyx/project/pba-sta-base/proj/lib/icsprout55-pdk/IP/STD_cell/"
      "ics55_LLSC_H7C_V1p10C100/ics55_LLSC_H7CL/liberty/"
      "ics55_LLSC_H7CL_typ_tt_1p2_25_nldm.lib");

  celllib::CellLibrary cell_lib;
  MyCellLibParser lib_parser(cell_lib);

  if (!celllib::try_load_celllib_cache(libs, cell_lib)) {
    for (const auto &file : libs) {
      std::cout << "liberty parser handle file " << file << std::endl;
      lib_parser.parse_from_file(file);
    }
    celllib::save_celllib_cache(libs, cell_lib);
  }

  std::string vfile =
      "/home/ysyx/project/pba-sta-base/proj/Testing/ics55/gcd/gcd.v";

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
    worker.run_candidate_graphy_dfs();

    std::string design_name =
        worker.top_moudle.empty() ? "design" : worker.top_moudle;
    std::string report_dir = "./result1/candidate/" + design_name;
    sta::STAReportGenerator::generate_report_pt_files(worker, "__clk__",
                                                      report_dir, design_name, 1);
  }
}
