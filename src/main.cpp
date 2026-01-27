#include <cstdlib>
#include <iostream>
#include <string>

#include "sta/sta_data_structures.hpp"
#include "sta/sta_report.hpp"
#include "sdc_parser.hpp"

#include "sta/debug.h"

#include "interface.hpp"

int main(int argc, char *argv[]){
  // 创建 worker 实例
  sta::STAWorker worker;
  MyVerilogParser parser(worker);
  MySDCParser sdc_interface(worker, parser);
  
  // 创建 SDC 解析器，传入接口实现
  sdc::SDCParser sdc_parser(&sdc_interface);
  
  // 解析 SDC 文件（如果提供了命令行参数）
  if(argc > 1) {
    std::string sdc_file = argv[1];
    std::cout << "Parsing SDC file: " << sdc_file << std::endl;
    sdc_parser.parse_file(sdc_file);
  }
  
  // 读取 Verilog 文件
  if(parser.get_filename().empty()) {
    std::cout << "no verilog file specific, use \"/home/ysyx/project/pba-sta-base/proj/Testing/reg.v \"\n" << std::endl;
    parser.read("/home/ysyx/project/pba-sta-base/proj/Testing/reg.v");
  } else {
    parser.read_with_filename();
  }
  worker.build_fanouts();

  // fanout_debuger(worker);

  run_debuger(worker, true);
  // worker.run();
  
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