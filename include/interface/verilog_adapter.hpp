#pragma once

#include <string>

#include "sta/sta_data_structures.hpp"

struct MyVerilogParser {
private:
  sta::STAWorker &worker_;
  std::string filename;

public:
  explicit MyVerilogParser(sta::STAWorker &worker, std::string file_name = "");
  ~MyVerilogParser() = default;

  void set_filename(const std::string &file_name);
  const std::string &get_filename() const;
  void read(const char *verilog_file);
  void read_with_filename();
};