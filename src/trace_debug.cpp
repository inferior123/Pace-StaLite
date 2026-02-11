#include "cell/cell_data_structure.hpp"
#include "interface.hpp"
#include "sta/sta_data_structures.hpp"
#include "sta/sta_report.hpp"
#include <iostream>
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

// 针对某个实例名（或子串）打印相关 point 及其 fanout，方便逐步排查 fanout
// 是否正确
void fanout_test_by_instance(sta::STAWorker &worker,
                             const std::string &inst_name_substr) {
  sta::TimingRunResult res = worker.get_sta_res();
  for (size_t i = 0; i < res.points.size(); ++i) {
    const sta::TimingPointRef &pt = res.points[i];
    if (!pt.inst)
      continue;
    if (pt.inst->instance_name.find(inst_name_substr) == std::string::npos)
      continue;

    std::cout << "\n==== fanout for pt" << i << " " << pt.inst->instance_name
              << "(" << pt.inst->module_name << ")/" << pt.port_name
              << " ====\n";
    sta::display_points_fanout(worker, i);
  }
}

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

      MySDCParser sdc_interface(worker);
      sdc::SDCParser sdc_parser(&sdc_interface);
      std::string sdc_file = vfile;
      size_t pos = sdc_file.find_last_of('.');
      if (pos != std::string::npos && sdc_file.substr(pos) == ".v") {
        sdc_file.replace(pos, std::string::npos, ".sdc");
      }
      std::cout << "Parsing SDC file: " << sdc_file << std::endl;
      sdc_parser.parse_file(sdc_file);
      std::cout << "finish parse SDC file" << std::endl;

      worker.set_cell_library(cell_lib);
      MyVerilogParser verilog_parser(worker);

      verilog_parser.read(vfile.c_str());
      worker.set_analysis_mode(sta::AnalysisMode::MAX);

      std::string design_name =
          worker.top_moudle.empty() ? "design" : worker.top_moudle;
      std::string report_dir = "./result/candidate/" + design_name;

      std::cout << "  [PBA] Step 1: build_fanouts()...\n";
      worker.build_fanouts();

      std::cout << "  [PBA-MAX] Step 2: calculate_load_capacitance()...\n";
      worker.caculate_candidate_load_cap();
      std::cout << "  [PBA-MAX] Step 3: build_candidate_graphy()...\n";
      worker.build_candidate_graphy_dfs();
      worker.run_candidate_graphy_dfs();

      // display_all_longest_path(worker);

      worker.get_config().clk_period = 10000;
      sta::STAReportGenerator::generate_report_pt_files(
          worker, "__clk__", report_dir, design_name, 1);
    }

    {
      sta::STAWorker worker;

      MySDCParser sdc_interface(worker);
      sdc::SDCParser sdc_parser(&sdc_interface);
      std::string sdc_file = vfile;
      size_t pos = sdc_file.find_last_of('.');
      if (pos != std::string::npos && sdc_file.substr(pos) == ".v") {
        sdc_file.replace(pos, std::string::npos, ".sdc");
      }
      std::cout << "Parsing SDC file: " << sdc_file << std::endl;
      sdc_parser.parse_file(sdc_file);
      std::cout << "finish parse SDC file" << std::endl;

      worker.set_cell_library(cell_lib);
      MyVerilogParser verilog_parser(worker);

      verilog_parser.read(vfile.c_str());
      worker.set_analysis_mode(sta::AnalysisMode::MIN);

      std::string design_name =
          worker.top_moudle.empty() ? "design" : worker.top_moudle;
      std::string report_dir = "./result/candidate/" + design_name;

      std::cout << "  [PBA] Step 1: build_fanouts()...\n";
      worker.build_fanouts();

      std::cout << "  [PBA-MIN] Step 2: calculate_load_capacitance()...\n";
      worker.caculate_candidate_load_cap();
      std::cout << "  [PBA-MIN] Step 3: build_candidate_graphy()...\n";
      worker.build_candidate_graphy_dfs();
      worker.run_candidate_graphy_dfs();

      debug_paths_through_instance(worker, "state_1__reg_p");

      worker.get_config().clk_period = 0;
      sta::STAReportGenerator::generate_report_pt_files(
          worker, "__clk__", report_dir, design_name, 1);
    }
  }
}

void display_all_longest_path(sta::STAWorker &worker) {
  worker.divide_path_entry();
  const sta::AnalysisMode mode = worker.get_analysis_mode();
  const sta::PathGroup groups[] = {
      sta::PathGroup::REG2REG, sta::PathGroup::IN2REG, sta::PathGroup::REG2OUT,
      sta::PathGroup::IN2OUT};
  const char *names[] = {"reg2reg", "in2reg", "reg2out", "in2out"};
  for (int i = 0; i < 4; ++i) {
    const sta::PathEntry *e = worker.get_top_k(groups[i], mode, 0);
    if (!e || !e->path)
      continue;
    std::cout << "\n========== Longest path [" << names[i] << "] ==========\n";
    worker.display_result_path_detail(*e->path);
  }

  const sta::PathEntry *e =
      worker.get_top_k(groups[1], worker.get_analysis_mode(), 0);
  if (e && e->path) {
    // 保持原有调用语义，仅用于调试时手动观察
    sta::TimingPathResult &pr = const_cast<sta::TimingPathResult &>(*e->path);
    (void)pr;
  }
  sta::display_points_fanout(worker, 466);
}

void singal_test(char *file_name) {

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

  std::string vfile = file_name;

  {
    sta::STAWorker worker;

    MySDCParser sdc_interface(worker);
    sdc::SDCParser sdc_parser(&sdc_interface);
    std::string sdc_file = vfile;
    size_t pos = sdc_file.find_last_of('.');
    if (pos != std::string::npos && sdc_file.substr(pos) == ".v") {
      sdc_file.replace(pos, std::string::npos, ".sdc");
    }
    std::cout << "Parsing SDC file: " << sdc_file << std::endl;
    sdc_parser.parse_file(sdc_file);
    std::cout << "finish parse SDC file" << std::endl;

    worker.set_cell_library(cell_lib);
    MyVerilogParser verilog_parser(worker);

    verilog_parser.read(vfile.c_str());
    worker.set_analysis_mode(sta::AnalysisMode::MAX);

    std::string design_name =
        worker.top_moudle.empty() ? "design" : worker.top_moudle;
    std::string report_dir = "./result1/candidate/" + design_name;

    std::cout << "  [PBA] Step 1: build_fanouts()...\n";
    worker.build_fanouts();

    std::cout << "  [PBA-MAX] Step 2: calculate_load_capacitance()...\n";
    worker.caculate_candidate_load_cap();
    std::cout << "  [PBA-MAX] Step 3: build_candidate_graphy()...\n";
    worker.build_candidate_graphy_dfs();
    worker.run_candidate_graphy_dfs();

    // display_all_longest_path(worker);

    worker.get_config().clk_period = 10000;
    sta::STAReportGenerator::generate_report_pt_files(
        worker, "__clk__", report_dir, design_name, 1);
  }

  {
    sta::STAWorker worker;

    MySDCParser sdc_interface(worker);
    sdc::SDCParser sdc_parser(&sdc_interface);
    std::string sdc_file = vfile;
    size_t pos = sdc_file.find_last_of('.');
    if (pos != std::string::npos && sdc_file.substr(pos) == ".v") {
      sdc_file.replace(pos, std::string::npos, ".sdc");
    }
    std::cout << "Parsing SDC file: " << sdc_file << std::endl;
    sdc_parser.parse_file(sdc_file);
    std::cout << "finish parse SDC file" << std::endl;

    worker.set_cell_library(cell_lib);
    MyVerilogParser verilog_parser(worker);

    verilog_parser.read(vfile.c_str());
    worker.set_analysis_mode(sta::AnalysisMode::MIN);

    std::string design_name =
        worker.top_moudle.empty() ? "design" : worker.top_moudle;
    std::string report_dir = "./result1/candidate/" + design_name;

    std::cout << "  [PBA] Step 1: build_fanouts()...\n";
    worker.build_fanouts();

    std::cout << "  [PBA-MIN] Step 2: calculate_load_capacitance()...\n";
    worker.caculate_candidate_load_cap();
    std::cout << "  [PBA-MIN] Step 3: build_candidate_graphy()...\n";
    worker.build_candidate_graphy_dfs();
    worker.run_candidate_graphy_dfs();

    debug_paths_through_instance(worker, "state_1__reg_p");

    worker.get_config().clk_period = 0;
    sta::STAReportGenerator::generate_report_pt_files(
        worker, "__clk__", report_dir, design_name, 1);
  }
}

void debug_lib_cell() {
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

  sta::show_lib_details("OA31X3H7L", cell_lib);
}

void spi_test() {
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
      "/home/ysyx/project/pba-sta-base/proj/Testing/ics55/simple/simple.v";

  {
    sta::STAWorker worker;
    worker.get_config().clk_name = "clk";
    worker.set_cell_library(cell_lib);
    MyVerilogParser verilog_parser(worker);

    verilog_parser.read(vfile.c_str());
    worker.set_analysis_mode(sta::AnalysisMode::MIN);

    std::cout << "  [PBA] Step 1: build_fanouts()...\n";
    worker.build_fanouts();
    // fanout_test_by_instance(worker, "state_1__reg_p");
    worker.caculate_candidate_load_cap();
    worker.build_candidate_graphy_dfs();
    worker.run_candidate_graphy_dfs(); // 填充 worker.res

    debug_paths_through_instance(worker, "state_1__reg_p");
    display_points_fanout(worker, 6);
  }
}

void test_lut() {
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

  auto cell = cell_lib.get_cell("NAND2BX0P5H7R");
  double cap = 0.0009657960;
  double slew = 0.2139875889;

  auto pin = cell->get_pin("Y");
  std::string related_pin = "AN";
  for (const auto &arc : pin->timing_arcs) {
    if (arc.related_pin != related_pin) {
      continue;
    }
    double res = caculate_transition_rise(arc, &cell_lib, slew, cap);
    std::cout << "res is " << res << std::endl;

    res = caculate_delay_rise(arc, &cell_lib, slew, cap);
    std::cout << "res is " << res << std::endl;
  }
}
