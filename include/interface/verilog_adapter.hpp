#include "parser-verilog/verilog_driver.hpp"
#include "sta/sta_data_structures.hpp"
#include "verilog_data.hpp"
#include <string>

struct MyVerilogParser : public verilog::ParserVerilogInterface {
private:
  sta::STAWorker &worker_; // 通过引用存储，避免复制
  std::string filename;    // 存储文件名

public:
  // 构造函数：注入 worker 引用
  explicit MyVerilogParser(sta::STAWorker &worker, std::string file_name = "")
      : worker_(worker), filename(file_name) {}

  virtual ~MyVerilogParser() {}

  void set_filename(const std::string &file_name) { filename = file_name; }

  // 使用存储的 filename 读取文件
  void read_with_filename() {
    if (!filename.empty()) {
      ParserVerilogInterface::read(std::filesystem::path(filename));
    }
  }

  // 获取当前文件名
  const std::string &get_filename() const { return filename; }

  // Function that will be called when encountering the top module name.
  void add_module(std::string &&name) { worker_.top_module = name; }

  // Function that will be called when encountering a port.
  void add_port(verilog::Port &&port) { worker_.collect_port(port); }

  // Function that will be called when encountering a net.
  void add_net(verilog::Net &&net) { worker_.collect_net(net); }

  // Function that will be called when encountering a assignment statement.
  void add_assignment(verilog::Assignment &&ast) {
    worker_.collect_assign(ast);
  }

  // Function that will be called when encountering a module instance.
  void add_instance(verilog::Instance &&inst) {
    worker_.collect_instance(inst);
  }
};