#include "cell/cell_data_structure.hpp"
#include "cell/liberty_parser.hpp"
#include "lib_parser/Lib.hh"
#include "lib_parser/lib_to_celllib_converter.hpp"
#include "parser-verilog/verilog_driver.hpp"
#include "sdc/sdc_parser.hpp"
#include "sta/sta_data_structures.hpp"
#include "verilog_data.hpp"
#include <filesystem>
#include <string>
#include <vector>

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
  void add_module(std::string &&name) { worker_.top_moudle = name; }

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

struct SampleParser : public verilog::ParserVerilogInterface {

  virtual ~SampleParser() {}

  // Function that will be called when encountering the top module name.
  void add_module(std::string &&name) {
    std::cout << "Module: " << name << '\n';
  }

  // Function that will be called when encountering a port.
  void add_port(verilog::Port &&port) { std::cout << "Port: " << port << '\n'; }

  // Function that will be called when encountering a net.
  void add_net(verilog::Net &&net) { std::cout << "Net: " << net << '\n'; }

  // Function that will be called when encountering a assignment statement.
  void add_assignment(verilog::Assignment &&ast) {
    std::cout << "Assignment: " << ast << '\n';
  }

  // Function that will be called when encountering a module instance.
  void add_instance(verilog::Instance &&inst) {
    std::cout << "Instance: " << inst << '\n';
  }
};

struct MySDCParser : public sdc::SDCParserInterface {
private:
  sta::STAWorker &worker_;

public:
  std::string verilog_file_name;
  std::vector<std::string> celllib_file_name;

  explicit MySDCParser(sta::STAWorker &worker) : worker_(worker) {}

  virtual ~MySDCParser() {}

  void create_clock(const std::string &name, double period,
                    const std::vector<double> &waveform,
                    const sdc::SDCObjectCollection &objects) override {
    // period 单位：ps（1ns = 1000ps）
    worker_.get_config().clk_period = static_cast<int>(period);
  }

  void set_clock_uncertainty(double setup_uncertainty, double hold_uncertainty,
                             const sdc::SDCObjectCollection &objects) override {
    // 单位：ps
    worker_.get_config().clock_uncertain = static_cast<int>(setup_uncertainty);
  }

  void set_clock_transition(double rise_transition, double fall_transition,
                            const sdc::SDCObjectCollection &objects) override {
    assert(false && "not implement");
  }

  void set_input_delay(double delay_value, const std::string &clock_name,
                       const sdc::SDCObjectCollection &objects) override {
    assert(false && "not implement");
  }

  void set_output_delay(double delay_value, const std::string &clock_name,
                        const sdc::SDCObjectCollection &objects) override {
    assert(false && "not implement");
  }

  void read_verilog(const std::string &filename) override {
    verilog_file_name = filename;
  }

  void read_liberty(const std::string &filename) override {
    celllib_file_name.push_back(filename);
  };

  void unknown_command(const sdc::SDCCommand &cmd) override {
    std::cerr << "invalid command " << cmd.command_name << std::endl;
    assert(false && "invalid sdc command");
  }
};

/**
 * Standard Cell Library Parser Interface
 * 用于解析标准单元库文件并存储到celllib命名空间中
 */
struct MyCellLibParser {
private:
  celllib::CellLibrary &cell_library_;

public:
  explicit MyCellLibParser(celllib::CellLibrary &cell_library)
      : cell_library_(cell_library) {}

  /**
   * 从文件路径解析标准单元库（使用 Lib Rust 解析器 + 转换为 CellLibrary）
   * @param file_path Liberty格式文件路径
   * @return 是否解析成功
   */
  bool parse_from_file(const std::string &file_path) {
    ista::Lib lib;
    ista::RustLibertyReader reader =
        lib.loadLibertyWithRustParser(file_path.c_str());
    unsigned ok = reader.linkLib();
    if (!ok) {
      std::cerr << "Lib parser: linkLib failed for " << file_path << std::endl;
      return false;
    }
    ista::LibBuilder *builder = reader.get_library_builder();
    if (!builder || !builder->get_lib()) {
      std::cerr << "Lib parser: no library builder after linkLib" << std::endl;
      return false;
    }
    celllib::convert_lib_to_cell_library(builder->get_lib(), cell_library_);
    return true;
  }

  /**
   * 从输入流解析标准单元库
   * @param input 输入流
   * @return 是否解析成功
   */
  bool parse_from_stream(std::istream &input) {
    celllib::LibertyParser parser(input);
    if (!parser.is_valid()) {
      std::cerr << "Parser error: " << parser.get_error() << std::endl;
      return false;
    }

    celllib::CellLibrary lib = parser.parse();
    if (!parser.is_valid()) {
      std::cerr << "Parse error: " << parser.get_error() << std::endl;
      return false;
    }

    // 将解析结果合并到cell_library_中
    for (const auto &cell_name : lib.get_cell_names()) {
      const auto *cell = lib.get_cell(cell_name);
      if (cell) {
        cell_library_.add_cell(*cell);
      }
    }

    // 更新库级别属性
    if (cell_library_.get_library_name().empty() &&
        !lib.get_library_name().empty()) {
      cell_library_.set_library_name(lib.get_library_name());
    }
    if (cell_library_.get_time_unit().empty() && !lib.get_time_unit().empty()) {
      cell_library_.set_time_unit(lib.get_time_unit());
    }

    return true;
  }

  /**
   * 获取解析的单元库引用
   */
  celllib::CellLibrary &get_library() { return cell_library_; }
  const celllib::CellLibrary &get_library() const { return cell_library_; }
};

/**
 * Simple Cell Library Parser with Output
 * 简单的标准单元库解析器，带输出功能用于调试和验证
 */
struct SimpleCellLibParser {
private:
  celllib::CellLibrary cell_library_;
  bool verbose_;

public:
  explicit SimpleCellLibParser(bool verbose = true) : verbose_(verbose) {}

  /**
   * 从文件路径解析标准单元库并输出解析内容
   * @param file_path Liberty格式文件路径
   * @return 是否解析成功
   */
  bool parse_from_file(const std::string &file_path) {
    std::cout << "\n========================================\n";
    std::cout << "Parsing Liberty file: " << file_path << "\n";
    std::cout << "========================================\n\n";

    celllib::LibertyParser parser(file_path);
    if (!parser.is_valid()) {
      std::cerr << "Parser initialization error: " << parser.get_error()
                << std::endl;
      return false;
    }

    celllib::CellLibrary lib = parser.parse();
    if (!parser.is_valid()) {
      std::cerr << "Parse error: " << parser.get_error() << std::endl;
      return false;
    }

    // 将解析结果合并到cell_library_中
    // 1. 复制table templates
    for (const auto &template_name : lib.get_table_template_names()) {
      const auto *templ = lib.get_table_template(template_name);
      if (templ) {
        cell_library_.add_table_template(*templ);
      }
    }

    // 2. 复制cells
    for (const auto &cell_name : lib.get_cell_names()) {
      const auto *cell = lib.get_cell(cell_name);
      if (cell) {
        cell_library_.add_cell(*cell);
      }
    }

    // 更新库级别属性
    if (cell_library_.get_library_name().empty() &&
        !lib.get_library_name().empty()) {
      cell_library_.set_library_name(lib.get_library_name());
    }
    if (cell_library_.get_time_unit().empty() && !lib.get_time_unit().empty()) {
      cell_library_.set_time_unit(lib.get_time_unit());
    }
    if (lib.get_capacitance_unit() != 1.0) {
      cell_library_.set_capacitance_unit(lib.get_capacitance_unit());
    }

    // 输出解析结果
    if (verbose_) {
      print_parsed_content();
    }

    return true;
  }

  /**
   * 输出解析的内容
   */
  void print_parsed_content() const {
    std::cout << "\n========================================\n";
    std::cout << "Parsed Library Information\n";
    std::cout << "========================================\n\n";

    // 库级别信息
    std::cout << "Library Name: " << cell_library_.get_library_name() << "\n";
    std::cout << "Time Unit: " << cell_library_.get_time_unit() << "\n";
    std::cout << "Capacitance Unit: " << cell_library_.get_capacitance_unit()
              << " ff\n";
    std::cout << "Total Cells: " << cell_library_.size() << "\n";

    // 输出查找表模板
    auto template_names = cell_library_.get_table_template_names();
    if (!template_names.empty()) {
      std::cout << "Table Templates (" << template_names.size() << "):\n";
      for (const auto &tname : template_names) {
        const auto *templ = cell_library_.get_table_template(tname);
        if (templ) {
          std::cout << "  - " << tname;
          if (templ->variable_1.has_value() || templ->variable_2.has_value()) {
            std::cout << " (";
            if (templ->variable_1.has_value()) {
              std::cout << "var1: " << templ->variable_1.value();
            }
            if (templ->variable_1.has_value() &&
                templ->variable_2.has_value()) {
              std::cout << ", ";
            }
            if (templ->variable_2.has_value()) {
              std::cout << "var2: " << templ->variable_2.value();
            }
            std::cout << ")";
          }
          std::cout << " [index_1: " << templ->index_1.size() << " values";
          if (!templ->index_1.empty()) {
            std::cout << " (";
            for (size_t i = 0; i < std::min(templ->index_1.size(), size_t(3));
                 ++i) {
              if (i > 0)
                std::cout << ", ";
              std::cout << templ->index_1[i];
            }
            if (templ->index_1.size() > 3)
              std::cout << "...";
            std::cout << ")";
          }
          std::cout << ", index_2: " << templ->index_2.size() << " values";
          if (!templ->index_2.empty()) {
            std::cout << " (";
            for (size_t i = 0; i < std::min(templ->index_2.size(), size_t(3));
                 ++i) {
              if (i > 0)
                std::cout << ", ";
              std::cout << templ->index_2[i];
            }
            if (templ->index_2.size() > 3)
              std::cout << "...";
            std::cout << ")";
          }
          std::cout << "]\n";
        }
      }
      std::cout << "\n";
    }

    // 遍历所有单元
    for (const auto &cell_name : cell_library_.get_cell_names()) {
      const auto *cell = cell_library_.get_cell(cell_name);
      if (!cell)
        continue;

      std::cout << "----------------------------------------\n";
      std::cout << "Cell: " << cell->name << "\n";

      if (cell->area.has_value()) {
        std::cout << "  Area: " << cell->area.value() << "\n";
      }
      if (cell->drive_strength.has_value()) {
        std::cout << "  Drive Strength: " << cell->drive_strength.value()
                  << "\n";
      }
      if (cell->cell_leakage_power.has_value()) {
        std::cout << "  Cell Leakage Power: "
                  << cell->cell_leakage_power.value() << "\n";
      }

      // 输出FF定义
      if (cell->ff.has_value()) {
        const auto &ff_def = cell->ff.value();
        std::cout << "  FF Definition:\n";
        std::cout << "    State Var: " << ff_def.state_var
                  << ", State Var Inv: " << ff_def.state_var_inv << "\n";
        if (ff_def.next_state.has_value()) {
          std::cout << "    Next State: " << ff_def.next_state.value() << "\n";
        }
        if (ff_def.clocked_on.has_value()) {
          std::cout << "    Clocked On: " << ff_def.clocked_on.value() << "\n";
        }
        if (ff_def.clear.has_value()) {
          std::cout << "    Clear: " << ff_def.clear.value() << "\n";
        }
        if (ff_def.preset.has_value()) {
          std::cout << "    Preset: " << ff_def.preset.value() << "\n";
        }
      }

      // 输出PG Pins
      if (!cell->pg_pins.empty()) {
        std::cout << "  PG Pins (" << cell->pg_pins.size() << "):\n";
        for (const auto &pg_pin : cell->pg_pins) {
          std::cout << "    - " << pg_pin.name;
          if (pg_pin.voltage_name.has_value()) {
            std::cout << " (voltage: " << pg_pin.voltage_name.value() << ")";
          }
          if (pg_pin.pg_type.has_value()) {
            std::cout << " [type: " << pg_pin.pg_type.value() << "]";
          }
          std::cout << "\n";
        }
      }

      // 输出Leakage Power
      if (!cell->leakage_power.empty()) {
        std::cout << "  Leakage Power Entries (" << cell->leakage_power.size()
                  << "):\n";
        for (size_t i = 0; i < cell->leakage_power.size(); ++i) {
          const auto &leakage = cell->leakage_power[i];
          std::cout << "    [" << i << "]";
          if (leakage.when.has_value()) {
            std::cout << " when: " << leakage.when.value();
          }
          if (leakage.value.has_value()) {
            std::cout << ", value: " << leakage.value.value();
          }
          std::cout << "\n";
        }
      }

      // 输出输入pins
      auto input_pins = cell->get_input_pins();
      if (!input_pins.empty()) {
        std::cout << "  Input Pins (" << input_pins.size() << "):\n";
        for (const auto &pin_name : input_pins) {
          const auto *pin = cell->get_pin(pin_name);
          if (pin) {
            std::cout << "    - " << pin_name;
            if (pin->capacitance.has_value()) {
              std::cout << " (capacitance: " << pin->capacitance.value()
                        << " ff)";
            }
            if (pin->rise_capacitance.has_value()) {
              std::cout << ", rise_cap: " << pin->rise_capacitance.value()
                        << " ff";
            }
            if (pin->fall_capacitance.has_value()) {
              std::cout << ", fall_cap: " << pin->fall_capacitance.value()
                        << " ff";
            }
            if (pin->max_capacitance.has_value()) {
              std::cout << ", max_cap: " << pin->max_capacitance.value()
                        << " ff";
            }
            if (pin->is_clock) {
              std::cout << " [CLOCK]";
            }
            if (pin->related_power_pin.has_value()) {
              std::cout << " [power_pin: " << pin->related_power_pin.value()
                        << "]";
            }
            if (pin->related_ground_pin.has_value()) {
              std::cout << " [ground_pin: " << pin->related_ground_pin.value()
                        << "]";
            }
            std::cout << "\n";

            // 输出input pin的timing arcs（setup/hold等）
            if (!pin->timing_arcs.empty()) {
              std::cout << "      Timing Arcs (" << pin->timing_arcs.size()
                        << "):\n";
              for (size_t i = 0; i < pin->timing_arcs.size(); ++i) {
                const auto &arc = pin->timing_arcs[i];
                std::cout << "        [" << i
                          << "] related_pin: " << arc.related_pin;

                // 输出timing type
                std::string timing_type_str;
                switch (arc.timing_type) {
                case celllib::TimingType::COMBINATIONAL:
                  timing_type_str = "combinational";
                  break;
                case celllib::TimingType::SETUP_RISING:
                  timing_type_str = "setup_rising";
                  break;
                case celllib::TimingType::SETUP_FALLING:
                  timing_type_str = "setup_falling";
                  break;
                case celllib::TimingType::HOLD_RISING:
                  timing_type_str = "hold_rising";
                  break;
                case celllib::TimingType::HOLD_FALLING:
                  timing_type_str = "hold_falling";
                  break;
                case celllib::TimingType::RISING_EDGE:
                  timing_type_str = "rising_edge";
                  break;
                case celllib::TimingType::FALLING_EDGE:
                  timing_type_str = "falling_edge";
                  break;
                case celllib::TimingType::MIN_PULSE_WIDTH:
                  timing_type_str = "min_pulse_width";
                  break;
                default:
                  timing_type_str = "unknown";
                  break;
                }
                std::cout << ", timing_type: " << timing_type_str;

                // 输出constraint查找表
                if (arc.fall_constraint.has_value()) {
                  const auto &lut = arc.fall_constraint.value();
                  std::cout << ", fall_constraint LUT (" << lut.index_1.size()
                            << "x" << lut.index_2.size() << ")";
                  if (lut.template_name.has_value()) {
                    std::cout << " [template: " << lut.template_name.value()
                              << "]";
                  }
                }
                if (arc.rise_constraint.has_value()) {
                  const auto &lut = arc.rise_constraint.value();
                  std::cout << ", rise_constraint LUT (" << lut.index_1.size()
                            << "x" << lut.index_2.size() << ")";
                  if (lut.template_name.has_value()) {
                    std::cout << " [template: " << lut.template_name.value()
                              << "]";
                  }
                }

                std::cout << "\n";
              }
            }
          } else {
            std::cout << "    - " << pin_name << " [NOT FOUND]\n";
          }
        }
      }

      // 输出输出pins
      auto output_pins = cell->get_output_pins();
      if (!output_pins.empty()) {
        std::cout << "  Output Pins (" << output_pins.size() << "):\n";
        for (const auto &pin_name : output_pins) {
          const auto *pin = cell->get_pin(pin_name);
          if (pin) {
            std::cout << "    - " << pin_name;
            if (pin->function.has_value()) {
              std::cout << " (function: " << pin->function.value() << ")";
            }
            std::cout << "\n";

            // 输出时序弧信息
            if (!pin->timing_arcs.empty()) {
              std::cout << "      Timing Arcs (" << pin->timing_arcs.size()
                        << "):\n";
              for (size_t i = 0; i < pin->timing_arcs.size(); ++i) {
                const auto &arc = pin->timing_arcs[i];
                std::cout << "        [" << i
                          << "] related_pin: " << arc.related_pin;

                // 输出timing type
                std::string timing_type_str;
                switch (arc.timing_type) {
                case celllib::TimingType::COMBINATIONAL:
                  timing_type_str = "combinational";
                  break;
                case celllib::TimingType::SETUP_RISING:
                  timing_type_str = "setup_rising";
                  break;
                case celllib::TimingType::SETUP_FALLING:
                  timing_type_str = "setup_falling";
                  break;
                case celllib::TimingType::HOLD_RISING:
                  timing_type_str = "hold_rising";
                  break;
                case celllib::TimingType::HOLD_FALLING:
                  timing_type_str = "hold_falling";
                  break;
                case celllib::TimingType::RISING_EDGE:
                  timing_type_str = "rising_edge";
                  break;
                case celllib::TimingType::FALLING_EDGE:
                  timing_type_str = "falling_edge";
                  break;
                case celllib::TimingType::MIN_PULSE_WIDTH:
                  timing_type_str = "min_pulse_width";
                  break;
                default:
                  timing_type_str = "unknown";
                  break;
                }
                std::cout << ", timing_type: " << timing_type_str;

                if (arc.intrinsic_rise.has_value()) {
                  std::cout << ", intrinsic_rise: "
                            << arc.intrinsic_rise.value();
                }
                if (arc.intrinsic_fall.has_value()) {
                  std::cout << ", intrinsic_fall: "
                            << arc.intrinsic_fall.value();
                }

                if (arc.cell_rise.has_value()) {
                  const auto &lut = arc.cell_rise.value();
                  std::cout << ", cell_rise LUT (" << lut.index_1.size() << "x"
                            << lut.index_2.size() << ")";
                  if (lut.template_name.has_value()) {
                    std::cout << " [template: " << lut.template_name.value()
                              << "]";
                  }
                }
                if (arc.cell_fall.has_value()) {
                  const auto &lut = arc.cell_fall.value();
                  std::cout << ", cell_fall LUT (" << lut.index_1.size() << "x"
                            << lut.index_2.size() << ")";
                  if (lut.template_name.has_value()) {
                    std::cout << " [template: " << lut.template_name.value()
                              << "]";
                  }
                }
                if (arc.rise_transition.has_value()) {
                  const auto &lut = arc.rise_transition.value();
                  std::cout << ", rise_transition LUT (" << lut.index_1.size()
                            << "x" << lut.index_2.size() << ")";
                  if (lut.template_name.has_value()) {
                    std::cout << " [template: " << lut.template_name.value()
                              << "]";
                  }
                }
                if (arc.fall_transition.has_value()) {
                  const auto &lut = arc.fall_transition.value();
                  std::cout << ", fall_transition LUT (" << lut.index_1.size()
                            << "x" << lut.index_2.size() << ")";
                  if (lut.template_name.has_value()) {
                    std::cout << " [template: " << lut.template_name.value()
                              << "]";
                  }
                }
                if (arc.fall_constraint.has_value()) {
                  const auto &lut = arc.fall_constraint.value();
                  std::cout << ", fall_constraint LUT (" << lut.index_1.size()
                            << "x" << lut.index_2.size() << ")";
                  if (lut.template_name.has_value()) {
                    std::cout << " [template: " << lut.template_name.value()
                              << "]";
                  }
                }
                if (arc.rise_constraint.has_value()) {
                  const auto &lut = arc.rise_constraint.value();
                  std::cout << ", rise_constraint LUT (" << lut.index_1.size()
                            << "x" << lut.index_2.size() << ")";
                  if (lut.template_name.has_value()) {
                    std::cout << " [template: " << lut.template_name.value()
                              << "]";
                  }
                }

                std::cout << "\n";
              }
            }

            // 输出Internal Power信息
            if (!pin->internal_power.empty()) {
              std::cout << "      Internal Power Entries ("
                        << pin->internal_power.size() << "):\n";
              for (size_t j = 0; j < pin->internal_power.size(); ++j) {
                const auto &ip = pin->internal_power[j];
                std::cout << "        [" << j << "]";
                if (ip.when.has_value()) {
                  std::cout << " when: " << ip.when.value();
                }
                if (ip.related_pin.has_value()) {
                  std::cout << ", related_pin: " << ip.related_pin.value();
                }
                if (ip.rise_power.has_value()) {
                  const auto &lut = ip.rise_power.value();
                  std::cout << ", rise_power LUT (" << lut.index_1.size()
                            << (lut.index_2.empty()
                                    ? ""
                                    : "x" + std::to_string(lut.index_2.size()))
                            << ")";
                  if (lut.template_name.has_value()) {
                    std::cout << " [template: " << lut.template_name.value()
                              << "]";
                  }
                }
                if (ip.fall_power.has_value()) {
                  const auto &lut = ip.fall_power.value();
                  std::cout << ", fall_power LUT (" << lut.index_1.size()
                            << (lut.index_2.empty()
                                    ? ""
                                    : "x" + std::to_string(lut.index_2.size()))
                            << ")";
                  if (lut.template_name.has_value()) {
                    std::cout << " [template: " << lut.template_name.value()
                              << "]";
                  }
                }
                std::cout << "\n";
              }
            }
          }
        }
      }

      std::cout << "\n";
    }

    std::cout << "========================================\n\n";
  }

  /**
   * 获取解析的单元库引用
   */
  celllib::CellLibrary &get_library() { return cell_library_; }
  const celllib::CellLibrary &get_library() const { return cell_library_; }

  /**
   * 设置是否输出详细信息
   */
  void set_verbose(bool verbose) { verbose_ = verbose; }
};
