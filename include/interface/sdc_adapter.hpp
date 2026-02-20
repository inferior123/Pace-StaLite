#include "sdc/sdc_parser.hpp"
#include "sta/sta_data_structures.hpp"
#include "verilog_data.hpp"
#include <string>
#include <vector>

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
    worker_.get_config().clk_name = name;
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
                       const sdc::SDCObjectCollection &objects) override {}

  void set_output_delay(double delay_value, const std::string &clock_name,
                        const sdc::SDCObjectCollection &objects) override {}

  void read_verilog(const std::string &filename) override {
    verilog_file_name = filename;
  }

  void read_liberty(const std::string &filename) override {
    celllib_file_name.push_back(filename);
  };

  void unknown_command(const sdc::SDCCommand &cmd) override {}
};