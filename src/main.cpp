#include <cstdlib>
#include <iostream>
#include <set>
#include <vector>

#include "sta/sta_data_structures.hpp"
#include "parser-verilog/verilog_driver.hpp"

#include "sta/debug.h"

sta::STAWorker worker;

// Define your own parser by inheriting the ParserVerilogInterface
struct MyVerilogParser : public verilog::ParserVerilogInterface {

  virtual ~MyVerilogParser(){}

  // Function that will be called when encountering the top module name.
  void add_module(std::string&& name){
    worker.top_moudle = name;
  }

  // Function that will be called when encountering a port.
  void add_port(verilog::Port&& port) {
    worker.collect_port(port);
  }  

  // Function that will be called when encountering a net.
  void add_net(verilog::Net&& net) {
    worker.collect_net(net);    
  }  

  // Function that will be called when encountering a assignment statement.
  void add_assignment(verilog::Assignment&& ast) {
    worker.collect_assign(ast);
  }  

  // Function that will be called when encountering a module instance.
  void add_instance(verilog::Instance&& inst) {
    worker.collect_instance(inst);
  }
};

struct SampleParser : public verilog::ParserVerilogInterface {

  virtual ~SampleParser(){}

  // Function that will be called when encountering the top module name.
  void add_module(std::string&& name){
    std::cout << "Module: " << name << '\n';
  }

  // Function that will be called when encountering a port.
  void add_port(verilog::Port&& port) {
    std::cout << "Port: " << port << '\n';
  }  

  // Function that will be called when encountering a net.
  void add_net(verilog::Net&& net) {
    std::cout << "Net: " << net << '\n';
  }  

  // Function that will be called when encountering a assignment statement.
  void add_assignment(verilog::Assignment&& ast) {
    std::cout << "Assignment: " << ast << '\n';
  }  

  // Function that will be called when encountering a module instance.
  void add_instance(verilog::Instance&& inst) {
    std::cout << "Instance: " << inst << '\n';
  }
};




int main(){
  MyVerilogParser parser;
  parser.read("../Testing/reg.v");
  worker.build_fanouts();

  // fanout_debuger(worker);

  run_debuger(worker, true);
  // worker.run();
  worker.sta_check(100);
  worker.report(100);

  return EXIT_SUCCESS;
}