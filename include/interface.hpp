#include "verilog_data.hpp"
#include "sta/sta_data_structures.hpp"
#include "sdc_parser.hpp"
#include <string>
#include <filesystem>

struct MyVerilogParser : public verilog::ParserVerilogInterface {
private:
  sta::STAWorker& worker_;  // 通过引用存储，避免复制
  std::string filename;     // 存储文件名

public:
  // 构造函数：注入 worker 引用
  explicit MyVerilogParser(sta::STAWorker& worker, std::string file_name = "") 
      : worker_(worker), filename(file_name) {}
  
  virtual ~MyVerilogParser(){}

  void set_filename(const std::string& file_name) {
    filename = file_name;
  }
  
  // 使用存储的 filename 读取文件
  void read_with_filename() {
    if (!filename.empty()) {
      ParserVerilogInterface::read(std::filesystem::path(filename));
    }
  }
  
  // 获取当前文件名
  const std::string& get_filename() const {
    return filename;
  }

  // Function that will be called when encountering the top module name.
  void add_module(std::string&& name){
    worker_.top_moudle = name;
  }

  // Function that will be called when encountering a port.
  void add_port(verilog::Port&& port) {
    worker_.collect_port(port);
  }  

  // Function that will be called when encountering a net.
  void add_net(verilog::Net&& net) {
    worker_.collect_net(net);    
  }  

  // Function that will be called when encountering a assignment statement.
  void add_assignment(verilog::Assignment&& ast) {
    worker_.collect_assign(ast);
  }  

  // Function that will be called when encountering a module instance.
  void add_instance(verilog::Instance&& inst) {
    worker_.collect_instance(inst);
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

struct MySDCParser : public sdc::SDCParserInterface {
private:
    sta::STAWorker& worker_;
    MyVerilogParser& verilog_parser_;
        
public:
    explicit MySDCParser(sta::STAWorker& worker, MyVerilogParser& verilog_parser)
        : worker_(worker), verilog_parser_(verilog_parser) {}
    
    virtual ~MySDCParser() {}
    
    void create_clock(
        const std::string& name,
        double period,
        const std::vector<double>& waveform,
        const sdc::SDCObjectCollection& objects
    ) override {
        worker_.get_config().clk_period = period;
    }
    
    void set_clock_uncertainty(
        double setup_uncertainty,
        double hold_uncertainty,
        const sdc::SDCObjectCollection& objects
    ) override {
        worker_.get_config().clock_uncertain = setup_uncertainty;
    }
    
    void set_clock_transition(
        double rise_transition,
        double fall_transition,
        const sdc::SDCObjectCollection& objects
    ) override {
        assert(false && "not implement");
    }
    
    void set_input_delay(
        double delay_value,
        const std::string& clock_name,
        const sdc::SDCObjectCollection& objects
    ) override {
        assert(false && "not implement");
    }
    
    void set_output_delay(
        double delay_value,
        const std::string& clock_name,
        const sdc::SDCObjectCollection& objects
    ) override {
        assert(false && "not implement");
    }
    
    void read_verilog(const std::string& filename) override {
        verilog_parser_.set_filename(filename);
    }

    void unknown_command(const sdc::SDCCommand& cmd) override {
      std::cerr << "invalid command " << cmd.command_name << std::endl;
      assert(false && "invalid sdc command");
    }
};