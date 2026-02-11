#include "cell/liberty_parser.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace celllib {

// ============================================================================
// 构造函数和主要解析入口
// ============================================================================

LibertyParser::LibertyParser(const std::string &file_path)
    : file_path(file_path),
      file_stream(std::make_unique<std::ifstream>(file_path)),
      input_stream(file_stream.get()), has_error(false), line_number(1) {
  if (!file_stream->is_open()) {
    report_error("Cannot open file: " + file_path);
  }
}

LibertyParser::LibertyParser(std::istream &input)
    : file_path(""), input_stream(&input), has_error(false), line_number(1) {}

CellLibrary LibertyParser::parse() {
  if (has_error) {
    return library;
  }

  try {
    parse_library();
  } catch (...) {
    if (!has_error) {
      report_error("Unexpected error during parsing");
    }
  }

  return library;
}

// ============================================================================
// 字符和Token处理函数
// ============================================================================

bool LibertyParser::is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r';
}

bool LibertyParser::is_identifier_char(char c) {
  return std::isalnum(c) || c == '_' || c == '\\';
}

char LibertyParser::peek_char() {
  if (eof())
    return '\0';
  return static_cast<char>(input_stream->peek());
}

char LibertyParser::next_char() {
  if (eof())
    return '\0';
  char c = static_cast<char>(input_stream->get());
  if (c == '\n') {
    line_number++;
  }
  return c;
}

bool LibertyParser::eof() {
  return input_stream->eof() || !input_stream->good();
}

void LibertyParser::skip_whitespace() {
  while (!eof()) {
    char c = peek_char();
    if (c == '\n') {
      next_char(); // 跳过换行符
      continue;
    }
    if (is_whitespace(c)) {
      next_char();
    } else {
      break;
    }
  }
}

void LibertyParser::skip_comment() {
  char c = peek_char();
  if (c == '/') {
    next_char();
    char c2 = peek_char();
    if (c2 == '/') {
      // 单行注释
      while (!eof() && peek_char() != '\n') {
        next_char();
      }
      if (!eof())
        next_char(); // 跳过换行符
    } else if (c2 == '*') {
      // 多行注释
      next_char(); // 跳过 '*'
      while (!eof()) {
        char c3 = next_char();
        if (c3 == '*' && peek_char() == '/') {
          next_char(); // 跳过 '/'
          break;
        }
      }
    } else {
      // 不是注释，回退
      input_stream->putback('/');
    }
  }
}

std::string LibertyParser::read_identifier() {
  skip_whitespace();
  std::string result;

  // 处理转义标识符
  if (peek_char() == '\\') {
    result += next_char();
    while (!eof() && peek_char() != ' ' && peek_char() != '\t' &&
           peek_char() != '\n') {
      result += next_char();
    }
    return result;
  }

  while (!eof() && is_identifier_char(peek_char())) {
    result += next_char();
  }

  return result;
}

std::string LibertyParser::read_string() {
  skip_whitespace();
  if (peek_char() != '"') {
    return "";
  }
  next_char(); // 跳过开始的引号

  std::string result;
  while (!eof()) {
    char c = next_char();
    if (c == '"') {
      break;
    }
    if (c == '\\' && !eof()) {
      c = next_char(); // 处理转义字符
    }
    result += c;
  }

  return result;
}

double LibertyParser::read_number() {
  skip_whitespace();
  std::string num_str;

  while (!eof()) {
    char c = peek_char();
    if (std::isdigit(c) || c == '.' || c == '-' || c == '+' || c == 'e' ||
        c == 'E') {
      num_str += next_char();
    } else {
      break;
    }
  }

  if (num_str.empty()) {
    report_error("Expected number");
    return 0.0;
  }

  try {
    return std::stod(num_str);
  } catch (...) {
    report_error("Invalid number: " + num_str);
    return 0.0;
  }
}

bool LibertyParser::expect_token(const std::string &token) {
  skip_whitespace();
  std::string found = read_identifier();
  return found == token;
}

void LibertyParser::report_error(const std::string &message) {
  has_error = true;
  std::ostringstream oss;
  oss << "Error at line " << line_number;
  if (!file_path.empty()) {
    oss << " in " << file_path;
  }
  oss << ": " << message;
  error_message = oss.str();
}

// ============================================================================
// 枚举转换函数
// ============================================================================

PinDirection LibertyParser::parse_pin_direction(const std::string &dir_str) {
  if (dir_str == "input")
    return PinDirection::INPUT;
  if (dir_str == "output")
    return PinDirection::OUTPUT;
  if (dir_str == "inout")
    return PinDirection::INOUT;
  if (dir_str == "internal")
    return PinDirection::INTERNAL;
  return PinDirection::INPUT;
}

TimingSense LibertyParser::parse_timing_sense(const std::string &sense_str) {
  if (sense_str == "positive_unate")
    return TimingSense::POSITIVE_UNATE;
  if (sense_str == "negative_unate")
    return TimingSense::NEGATIVE_UNATE;
  if (sense_str == "non_unate")
    return TimingSense::NON_UNATE;
  return TimingSense::POSITIVE_UNATE;
}

TimingType LibertyParser::parse_timing_type(const std::string &type_str) {
  if (type_str == "combinational")
    return TimingType::COMBINATIONAL;
  if (type_str == "setup_rising")
    return TimingType::SETUP_RISING;
  if (type_str == "setup_falling")
    return TimingType::SETUP_FALLING;
  if (type_str == "hold_rising")
    return TimingType::HOLD_RISING;
  if (type_str == "hold_falling")
    return TimingType::HOLD_FALLING;
  if (type_str == "rising_edge")
    return TimingType::RISING_EDGE;
  if (type_str == "falling_edge")
    return TimingType::FALLING_EDGE;
  if (type_str == "clear")
    return TimingType::CLEAR;
  if (type_str == "preset")
    return TimingType::PRESET;
  if (type_str == "min_pulse_width")
    return TimingType::MIN_PULSE_WIDTH;
  return TimingType::COMBINATIONAL;
}

// ============================================================================
// 单位转换函数
// ============================================================================

double LibertyParser::parse_time_unit(const std::string &unit_str) {
  // 解析 "1ps", "1ns" 等格式
  if (unit_str.find("ps") != std::string::npos) {
    return 1.0; // ps
  } else if (unit_str.find("ns") != std::string::npos) {
    return 1000.0; // ns to ps
  } else if (unit_str.find("us") != std::string::npos) {
    return 1000000.0; // us to ps
  }
  return 1.0; // 默认ps
}

// 辅助函数：将时间值转换为ps单位
double LibertyParser::convert_time_to_ps(double time_value) {
  const std::string &time_unit = library.get_time_unit();
  if (time_unit.find("ns") != std::string::npos) {
    return time_value * 1000.0; // ns to ps
  } else if (time_unit.find("us") != std::string::npos) {
    return time_value * 1000000.0; // us to ps
  }
  // 如果已经是ps或未指定，保持原值
  return time_value;
}

double LibertyParser::parse_capacitance_unit(const std::string &unit_str) {
  // 解析 "(1,ff)" 格式
  // 简化实现：假设单位是ff
  return 1.0; // ff
}

// ============================================================================
// 辅助工具函数：跳过未知内容
// ============================================================================

/**
 * 跳过括号块，如 (content)
 */
void LibertyParser::skip_parentheses_block() {
  if (peek_char() != '(')
    return;

  int paren_count = 1;
  next_char();
  while (!eof() && paren_count > 0) {
    char c = next_char();
    if (c == '(')
      paren_count++;
    else if (c == ')')
      paren_count--;
  }
}

/**
 * 跳过大括号块，如 {content}
 */
void LibertyParser::skip_brace_block() {
  if (peek_char() != '{')
    return;

  int brace_count = 1;
  next_char();
  while (!eof() && brace_count > 0) {
    char c = next_char();
    if (c == '{')
      brace_count++;
    else if (c == '}')
      brace_count--;
  }
}

/**
 * 跳过带冒号的属性值，直到分号或块开始
 */
void LibertyParser::skip_attribute_value() {
  skip_whitespace();
  while (!eof() && peek_char() != ';' && peek_char() != '}' &&
         peek_char() != '{') {
    next_char();
  }
  if (peek_char() == '{') {
    skip_brace_block();
  }
  skip_whitespace();
  if (peek_char() == ';') {
    next_char();
  }
}

/**
 * 跳过未知属性（带括号、冒号或块的）
 */
void LibertyParser::skip_unknown_attribute() {
  skip_whitespace();

  // 处理带括号的属性，如 pg_pin(VDD) 或 internal_power()
  if (peek_char() == '(') {
    skip_parentheses_block();
    skip_whitespace();
  }

  // 处理大括号块
  if (peek_char() == '{') {
    skip_brace_block();
  } else if (peek_char() == ':') {
    // 带冒号的属性值
    next_char();
    skip_attribute_value();
  } else {
    // 简单属性，跳过直到分号
    while (!eof() && peek_char() != ';' && peek_char() != '}') {
      next_char();
    }
    if (peek_char() == ';') {
      next_char();
    }
  }
}

// ============================================================================
// 库级别解析
// ============================================================================

void LibertyParser::parse_library() {
  skip_whitespace();
  skip_comment();

  if (!expect_token("library")) {
    report_error("Expected 'library' keyword");
    return;
  }

  // 读取库名称
  skip_whitespace();
  if (peek_char() == '(') {
    next_char();
    std::string lib_name = read_identifier();
    library.set_library_name(lib_name);
    skip_whitespace();
    if (peek_char() == ')') {
      next_char();
    }
  }

  // 进入库块
  skip_whitespace();
  if (peek_char() != '{') {
    report_error("Expected '{' after library declaration");
    return;
  }
  next_char();

  // 解析库属性和所有cell
  int cell_count = 0;
  while (!eof() && !has_error) {
    skip_whitespace();
    skip_comment();

    if (peek_char() == '}') {
      next_char();
      break;
    }

    std::string token = read_identifier();
    if (token.empty()) {
      // 如果无法读取标识符，可能是遇到了特殊字符或注释，跳过
      if (!eof() && peek_char() != '}') {
        char c = peek_char();
        // 如果是换行符或空白字符，跳过
        if (c == '\n' || is_whitespace(c)) {
          skip_whitespace();
          skip_comment();
        } else {
          // 跳过未知字符
          next_char();
        }
      }
      continue;
    }

    // 调试：打印读取的token（仅在verbose模式下）
    // std::cerr << "Debug: Read token: " << token << " at line " << line_number
    // << std::endl;

    if (token == "cell") {
      StandardCell cell;
      parse_cell(cell);
      if (!has_error && !cell.name.empty()) {
        library.add_cell(cell);
        cell_count++;
      } else {
        if (has_error) {
          std::cerr << "Error parsing cell at line " << line_number
                    << ", error: " << error_message << std::endl;
        }
        if (cell.name.empty()) {
          std::cerr << "Warning: Cell name is empty after parsing at line "
                    << line_number << std::endl;
        }
      }
    } else if (token == "time_unit") {
      parse_time_unit_attribute();
    } else if (token == "capacitive_load_unit") {
      parse_capacitive_load_unit_attribute();
    } else if (token == "lu_table_template") {
      parse_table_template();
    } else {
      // 跳过其他未知的库级别属性
      skip_unknown_attribute();
    }
  }

  // 调试信息
  if (cell_count == 0 && !has_error) {
    std::cerr
        << "Warning: No cells were parsed from library. Total tokens processed."
        << std::endl;
  }
}

void LibertyParser::parse_time_unit_attribute() {
  skip_whitespace();
  if (peek_char() == ':') {
    next_char();
    skip_whitespace();
    std::string unit = read_string();
    if (unit.empty()) {
      unit = read_identifier();
    }
    library.set_time_unit(unit);
    skip_whitespace();
    if (peek_char() == ';')
      next_char();
  }
}

void LibertyParser::parse_capacitive_load_unit_attribute() {
  skip_whitespace();
  if (peek_char() == ':') {
    next_char();
    skip_whitespace();
    if (peek_char() == '(') {
      next_char();
      skip_whitespace();
      read_number(); // 读取数值
      skip_whitespace();
      if (peek_char() == ',') {
        next_char();
        skip_whitespace();
        read_identifier(); // 读取单位
      }
      skip_whitespace();
      if (peek_char() == ')') {
        next_char();
      }
    }
    skip_whitespace();
    if (peek_char() == ';')
      next_char();
  }
}

void LibertyParser::parse_library_attributes() {
  // 此函数已废弃，逻辑已整合到parse_library中
  // 保留空实现以避免编译错误
}

void LibertyParser::parse_table_template() {
  skip_whitespace();
  std::string template_name;
  if (peek_char() == '(') {
    next_char();
    skip_whitespace();
    template_name = read_identifier();
    skip_whitespace();
    if (peek_char() == ')') {
      next_char();
    }
  }

  if (template_name.empty()) {
    // 如果没有名称，跳过这个template
    skip_unknown_attribute();
    return;
  }

  TableTemplate templ(template_name);

  skip_whitespace();
  if (peek_char() == '{') {
    next_char();
    while (!eof() && !has_error) {
      skip_whitespace();
      skip_comment();
      if (peek_char() == '}') {
        next_char();
        break;
      }

      std::string attr = read_identifier();
      if (attr.empty())
        continue;

      skip_whitespace();
      if (peek_char() == ':') {
        next_char();
        skip_whitespace();
        std::string value = read_string();
        if (value.empty()) {
          value = read_identifier();
        }

        if (attr == "variable_1") {
          templ.variable_1 = value;
        } else if (attr == "variable_2") {
          templ.variable_2 = value;
        }

        skip_whitespace();
        if (peek_char() == ';')
          next_char();
      } else if (attr == "index_1") {
        skip_whitespace();
        if (peek_char() == '(') {
          next_char();
          skip_whitespace();
          parse_index_list(templ.index_1);
          skip_whitespace();
          if (peek_char() == ')') {
            next_char();
          }
          skip_whitespace();
          if (peek_char() == ';')
            next_char();
        }
      } else if (attr == "index_2") {
        skip_whitespace();
        if (peek_char() == '(') {
          next_char();
          skip_whitespace();
          parse_index_list(templ.index_2);
          skip_whitespace();
          if (peek_char() == ')') {
            next_char();
          }
          skip_whitespace();
          if (peek_char() == ';')
            next_char();
        }
      } else {
        skip_unknown_attribute();
      }
    }
  }

  library.add_table_template(templ);
  // 调试输出
  std::cout << "  [DEBUG] Parsed table template: " << template_name << " (var1="
            << (templ.variable_1.has_value() ? templ.variable_1.value()
                                             : "none")
            << ", var2="
            << (templ.variable_2.has_value() ? templ.variable_2.value()
                                             : "none")
            << ", index_1 size=" << templ.index_1.size()
            << ", index_2 size=" << templ.index_2.size() << ")" << std::endl;
}

// ============================================================================
// 单元级别解析
// ============================================================================

void LibertyParser::parse_cell(StandardCell &cell) {
  // 读取单元名称
  skip_whitespace();
  if (peek_char() == '(') {
    next_char();
    cell.name = read_identifier();
    skip_whitespace();
    if (peek_char() == ')') {
      next_char();
    }
  }

  // 进入单元块
  skip_whitespace();
  if (peek_char() != '{') {
    report_error("Expected '{' after cell declaration");
    return;
  }
  next_char();

  // 解析单元属性
  while (!eof() && !has_error) {
    skip_whitespace();
    skip_comment();

    if (peek_char() == '}') {
      next_char();
      break;
    }

    std::string token = read_identifier();
    if (token.empty()) {
      if (!eof() && peek_char() != '}') {
        next_char();
      }
      continue;
    }

    if (token == "pin") {
      Pin pin;
      parse_pin(pin);
      if (!has_error && !pin.name.empty()) {
        cell.add_pin(pin);
      }
    } else if (token == "area") {
      parse_cell_area_attribute(cell);
    } else if (token == "drive_strength") {
      parse_cell_drive_strength_attribute(cell);
    } else if (token == "cell_leakage_power") {
      parse_cell_leakage_power_attribute(cell);
    } else if (token == "ff") {
      parse_ff_definition(cell);
    } else if (token == "pg_pin") {
      parse_pg_pin(cell);
    } else if (token == "leakage_power") {
      parse_leakage_power(cell);
    } else {
      // 保留未知属性，但暂时跳过（可以后续扩展）
      skip_unknown_attribute();
    }
  }
}

void LibertyParser::parse_cell_area_attribute(StandardCell &cell) {
  skip_whitespace();
  if (peek_char() == ':') {
    next_char();
    skip_whitespace();
    cell.area = read_number();
    skip_whitespace();
    if (peek_char() == ';')
      next_char();
  }
}

void LibertyParser::parse_cell_drive_strength_attribute(StandardCell &cell) {
  skip_whitespace();
  if (peek_char() == ':') {
    next_char();
    skip_whitespace();
    cell.drive_strength = read_number();
    skip_whitespace();
    if (peek_char() == ';')
      next_char();
  }
}

void LibertyParser::parse_cell_leakage_power_attribute(StandardCell &cell) {
  skip_whitespace();
  if (peek_char() == ':') {
    next_char();
    skip_whitespace();
    cell.cell_leakage_power = read_number();
    skip_whitespace();
    if (peek_char() == ';')
      next_char();
  }
}

void LibertyParser::parse_ff_definition(StandardCell &cell) {
  skip_whitespace();
  if (peek_char() == '(') {
    next_char();
    skip_whitespace();
    std::string state_var = read_string();
    if (state_var.empty()) {
      state_var = read_identifier();
    }
    skip_whitespace();
    if (peek_char() == ',') {
      next_char();
      skip_whitespace();
    }
    std::string state_var_inv = read_string();
    if (state_var_inv.empty()) {
      state_var_inv = read_identifier();
    }
    skip_whitespace();
    if (peek_char() == ')') {
      next_char();
    }

    FFDefinition ff_def;
    ff_def.state_var = state_var;
    ff_def.state_var_inv = state_var_inv;

    skip_whitespace();
    if (peek_char() == '{') {
      next_char();
      while (!eof() && !has_error) {
        skip_whitespace();
        skip_comment();
        if (peek_char() == '}') {
          next_char();
          break;
        }

        std::string attr = read_identifier();
        if (attr.empty())
          continue;

        skip_whitespace();
        if (peek_char() == ':') {
          next_char();
          skip_whitespace();
          std::string value = read_string();
          if (value.empty()) {
            value = read_identifier();
          }

          if (attr == "next_state") {
            ff_def.next_state = value;
          } else if (attr == "clocked_on") {
            ff_def.clocked_on = value;
          } else if (attr == "clear") {
            ff_def.clear = value;
          } else if (attr == "preset") {
            ff_def.preset = value;
          }

          skip_whitespace();
          if (peek_char() == ';')
            next_char();
        } else {
          skip_unknown_attribute();
        }
      }
    }

    cell.ff = ff_def;
  }
}

void LibertyParser::parse_pg_pin(StandardCell &cell) {
  skip_whitespace();
  std::string pin_name;
  if (peek_char() == '(') {
    next_char();
    skip_whitespace();
    pin_name = read_identifier();
    skip_whitespace();
    if (peek_char() == ')') {
      next_char();
    }
  }

  PGPin pg_pin(pin_name);

  skip_whitespace();
  if (peek_char() == '{') {
    next_char();
    while (!eof() && !has_error) {
      skip_whitespace();
      skip_comment();
      if (peek_char() == '}') {
        next_char();
        break;
      }

      std::string attr = read_identifier();
      if (attr.empty())
        continue;

      skip_whitespace();
      if (peek_char() == ':') {
        next_char();
        skip_whitespace();
        std::string value = read_string();
        if (value.empty()) {
          value = read_identifier();
        }

        if (attr == "voltage_name") {
          pg_pin.voltage_name = value;
        } else if (attr == "pg_type") {
          pg_pin.pg_type = value;
        }

        skip_whitespace();
        if (peek_char() == ';')
          next_char();
      } else {
        skip_unknown_attribute();
      }
    }
  }

  cell.pg_pins.push_back(pg_pin);
}

void LibertyParser::parse_leakage_power(StandardCell &cell) {
  skip_whitespace();
  if (peek_char() == '(') {
    skip_parentheses_block();
    skip_whitespace();
  }

  LeakagePower leakage;

  skip_whitespace();
  if (peek_char() == '{') {
    next_char();
    while (!eof() && !has_error) {
      skip_whitespace();
      skip_comment();
      if (peek_char() == '}') {
        next_char();
        break;
      }

      std::string attr = read_identifier();
      if (attr.empty())
        continue;

      skip_whitespace();
      if (peek_char() == ':') {
        next_char();
        skip_whitespace();

        if (attr == "when") {
          leakage.when = read_string();
        } else if (attr == "value") {
          leakage.value = read_number();
        }

        skip_whitespace();
        if (peek_char() == ';')
          next_char();
      } else {
        skip_unknown_attribute();
      }
    }
  }

  cell.leakage_power.push_back(leakage);
}

// ============================================================================
// Pin级别解析
// ============================================================================

void LibertyParser::parse_pin(Pin &pin) {
  // 读取pin名称
  skip_whitespace();
  if (peek_char() == '(') {
    next_char();
    pin.name = read_identifier();
    skip_whitespace();
    if (peek_char() == ')') {
      next_char();
    }
  }

  // 进入pin块
  skip_whitespace();
  if (peek_char() != '{') {
    report_error("Expected '{' after pin declaration");
    return;
  }
  next_char();

  // 解析pin属性
  while (!eof() && !has_error) {
    skip_whitespace();
    skip_comment();

    if (peek_char() == '}') {
      next_char();
      break;
    }

    // 跳过空行
    if (peek_char() == '\n') {
      continue;
    }

    std::string attr = read_identifier();
    if (attr.empty()) {
      if (!eof() && peek_char() != '}') {
        next_char();
      }
      continue;
    }

    if (attr == "timing") {
      parse_pin_timing_block(pin);
    } else {
      parse_pin_attribute(pin, attr);
    }
  }
}

void LibertyParser::parse_pin_timing_block(Pin &pin) {
  skip_whitespace();
  // 跳过timing后的括号，如 timing()
  if (peek_char() == '(') {
    skip_parentheses_block();
    skip_whitespace();
  }

  // 进入timing块
  if (peek_char() == '{') {
    next_char();
    TimingArc arc;
    parse_timing_arc(arc, pin.name);
    pin.timing_arcs.push_back(arc);
  }
}

void LibertyParser::parse_pin_attribute(Pin &pin, const std::string &attr) {
  skip_whitespace();

  if (peek_char() == ':') {
    // 带冒号的属性
    next_char();
    skip_whitespace();

    if (attr == "direction") {
      std::string dir = read_identifier();
      pin.direction = parse_pin_direction(dir);
    } else if (attr == "capacitance") {
      pin.capacitance = read_number();
    } else if (attr == "rise_capacitance") {
      pin.rise_capacitance_max = read_number();
    } else if (attr == "fall_capacitance") {
      pin.fall_capacitance_max = read_number();
    } else if (attr == "max_capacitance") {
      pin.max_capacitance = read_number();
    } else if (attr == "function") {
      pin.function = read_string();
    } else if (attr == "clock") {
      std::string clock_val = read_identifier();
      pin.is_clock = (clock_val == "true");
    } else if (attr == "related_power_pin") {
      pin.related_power_pin = read_string();
    } else if (attr == "related_ground_pin") {
      pin.related_ground_pin = read_string();
    } else if (attr == "internal_power") {
      parse_internal_power(pin);
      return; // internal_power已经处理了分号
    } else {
      // 保留未知属性，但暂时跳过值
      skip_attribute_value();
      return;
    }

    skip_whitespace();
    if (peek_char() == ';') {
      next_char();
    }
  } else {
    // 其他格式的属性，尝试解析
    if (attr == "internal_power") {
      parse_internal_power(pin);
    } else {
      skip_unknown_attribute();
    }
  }
}

// ============================================================================
// Timing Arc解析
// ============================================================================

void LibertyParser::parse_timing_arc(TimingArc &arc,
                                     const std::string &output_pin_name) {
  while (!eof() && !has_error) {
    skip_whitespace();
    skip_comment();

    if (peek_char() == '}') {
      next_char();
      break;
    }

    // 跳过空行
    if (peek_char() == '\n') {
      continue;
    }

    std::string attr = read_identifier();
    if (attr.empty()) {
      if (!eof() && peek_char() != '}') {
        next_char();
      }
      continue;
    }

    // 检查是否是查找表
    if (is_lookup_table_attribute(attr)) {
      parse_timing_arc_lookup_table(arc, attr);
      continue;
    }

    // 检查是否是constraint查找表
    if (is_constraint_attribute(attr)) {
      parse_timing_arc_constraint(arc, attr);
      continue;
    }

    // 解析普通属性
    parse_timing_arc_attribute(arc, attr);
  }
}

bool LibertyParser::is_lookup_table_attribute(const std::string &attr) {
  return attr == "cell_rise" || attr == "cell_fall" ||
         attr == "rise_transition" || attr == "fall_transition";
}

bool LibertyParser::is_constraint_attribute(const std::string &attr) {
  return attr == "fall_constraint" || attr == "rise_constraint";
}

void LibertyParser::parse_timing_arc_lookup_table(TimingArc &arc,
                                                  const std::string &attr) {
  skip_whitespace();
  std::string template_name;
  // 读取查找表名称，如 cell_rise(Timing_7_7)
  if (peek_char() == '(') {
    next_char();
    skip_whitespace();
    template_name = read_identifier();
    skip_whitespace();
    if (peek_char() == ')') {
      next_char();
    }
    skip_whitespace();
  }

  // 进入查找表块
  if (peek_char() == '{') {
    next_char();
    LookupTable lut;
    if (!template_name.empty()) {
      lut.template_name = template_name;
    }
    parse_lookup_table(lut, attr);

    // 根据属性类型存储到相应的字段
    if (attr == "cell_rise") {
      arc.cell_rise = lut;
    } else if (attr == "cell_fall") {
      arc.cell_fall = lut;
    } else if (attr == "rise_transition") {
      arc.rise_transition = lut;
    } else if (attr == "fall_transition") {
      arc.fall_transition = lut;
    }
  }
}

void LibertyParser::parse_timing_arc_constraint(TimingArc &arc,
                                                const std::string &attr) {
  skip_whitespace();
  std::string template_name;
  // 读取constraint名称，如 fall_constraint(Hold_3_3)
  if (peek_char() == '(') {
    next_char();
    skip_whitespace();
    template_name = read_identifier();
    skip_whitespace();
    if (peek_char() == ')') {
      next_char();
    }
    skip_whitespace();
  }

  // 进入constraint查找表块
  if (peek_char() == '{') {
    next_char();
    LookupTable lut;
    if (!template_name.empty()) {
      lut.template_name = template_name;
    }
    parse_lookup_table(lut, attr);

    // 根据属性类型存储到相应的字段
    if (attr == "fall_constraint") {
      arc.fall_constraint = lut;
    } else if (attr == "rise_constraint") {
      arc.rise_constraint = lut;
    }
  }
}

void LibertyParser::parse_timing_arc_attribute(TimingArc &arc,
                                               const std::string &attr) {
  skip_whitespace();

  if (peek_char() == ':') {
    next_char();
    skip_whitespace();

    if (attr == "related_pin") {
      arc.related_pin = read_string();
      if (arc.related_pin.empty()) {
        arc.related_pin = read_identifier();
      }
    } else if (attr == "timing_sense") {
      std::string sense = read_identifier();
      arc.timing_sense = parse_timing_sense(sense);
    } else if (attr == "timing_type") {
      std::string type = read_identifier();
      arc.timing_type = parse_timing_type(type);
    } else if (attr == "intrinsic_rise") {
      double val = read_number();
      arc.intrinsic_rise = convert_time_to_ps(val);
    } else if (attr == "intrinsic_fall") {
      double val = read_number();
      arc.intrinsic_fall = convert_time_to_ps(val);
    } else {
      // 未知属性，跳过值
      skip_attribute_value();
      return;
    }

    skip_whitespace();
    if (peek_char() == ';') {
      next_char();
    }
  } else {
    // 其他格式，跳过
    skip_unknown_attribute();
  }
}

// ============================================================================
// 查找表解析
// ============================================================================

void LibertyParser::parse_lookup_table(LookupTable &lut,
                                       const std::string &table_type) {
  while (!eof() && !has_error) {
    skip_whitespace();
    skip_comment();

    if (peek_char() == '}') {
      next_char();
      break;
    }

    // 跳过空行
    if (peek_char() == '\n') {
      continue;
    }

    std::string attr = read_identifier();
    if (attr.empty()) {
      if (!eof() && peek_char() != '}') {
        next_char();
      }
      continue;
    }

    // 解析查找表的核心属性
    if (attr == "index_1") {
      parse_lookup_table_index_1(lut);
    } else if (attr == "index_2") {
      parse_lookup_table_index_2(lut);
    } else if (attr == "values") {
      parse_lookup_table_values(lut);
    } else {
      // 跳过其他属性
      skip_unknown_attribute();
    }
  }

  // 转换index_1：如果是时间相关的查找表，index_1通常是input_net_transition（时间）
  // 对于timing相关的查找表（cell_rise, cell_fall, rise_transition,
  // fall_transition, fall_constraint,
  // rise_constraint），index_1是时间值，需要转换
  if (table_type.find("transition") != std::string::npos ||
      table_type.find("timing") != std::string::npos ||
      table_type.find("constraint") != std::string::npos ||
      table_type == "cell_rise" || table_type == "cell_fall" ||
      table_type == "rise_transition" || table_type == "fall_transition" ||
      table_type == "fall_constraint" || table_type == "rise_constraint") {
    for (auto &val : lut.index_1) {
      val = convert_time_to_ps(val);
    }
  }
}

void LibertyParser::parse_lookup_table_index_1(LookupTable &lut) {
  skip_whitespace();
  if (peek_char() == '(') {
    next_char();
    skip_whitespace();
    parse_index_list(lut.index_1);
    skip_whitespace();
    if (peek_char() == ')') {
      next_char();
    }
    skip_whitespace();
    if (peek_char() == ';') {
      next_char();
    }
  }
}

void LibertyParser::parse_lookup_table_index_2(LookupTable &lut) {
  skip_whitespace();
  if (peek_char() == '(') {
    next_char();
    skip_whitespace();
    parse_index_list(lut.index_2);
    skip_whitespace();
    if (peek_char() == ')') {
      next_char();
    }
    skip_whitespace();
    if (peek_char() == ';') {
      next_char();
    }
  }
}

void LibertyParser::parse_lookup_table_values(LookupTable &lut) {
  skip_whitespace();
  if (peek_char() == '(') {
    next_char();
    skip_whitespace();
    parse_values_list(lut.values, lut.index_1.size());
    skip_whitespace();
    if (peek_char() == ')') {
      next_char();
    }
    skip_whitespace();
    if (peek_char() == ';') {
      next_char();
    }
  }

  // 转换查找表的值从库的时间单位到ps
  // 查找表的values都是时间值（延迟、转换时间等）
  for (auto &row : lut.values) {
    for (auto &val : row) {
      val = convert_time_to_ps(val);
    }
  }
}

void LibertyParser::parse_index_list(std::vector<double> &indices) {
  skip_whitespace();
  std::string list_str = read_string();

  if (list_str.empty()) {
    // 尝试读取不带引号的数字列表
    while (!eof() && peek_char() != ')') {
      skip_whitespace();
      if (peek_char() == ',') {
        next_char();
        skip_whitespace();
      }
      if (peek_char() == ')')
        break;
      double value = read_number();
      indices.push_back(value);
    }
  } else {
    // 解析逗号分隔的字符串
    parse_comma_separated_numbers(list_str, indices);
  }
}

void LibertyParser::parse_values_list(std::vector<std::vector<double>> &values,
                                      size_t num_rows) {
  skip_whitespace();

  while (!eof() && peek_char() != ')') {
    skip_whitespace();
    skip_comment();

    if (peek_char() == ')') {
      break;
    }

    if (peek_char() == '"') {
      // 读取带引号的字符串行
      std::string line_str = read_string();
      std::vector<double> row;
      parse_comma_separated_numbers(line_str, row);
      if (!row.empty()) {
        values.push_back(row);
      }

      // 处理续行符和逗号
      skip_continuation_and_comma();
    } else {
      // 尝试读取不带引号的数字
      parse_unquoted_value_row(values);
    }
  }
}

void LibertyParser::parse_comma_separated_numbers(
    const std::string &str, std::vector<double> &numbers) {
  std::istringstream iss(str);
  std::string token;
  while (std::getline(iss, token, ',')) {
    // 去除空格
    token.erase(std::remove_if(token.begin(), token.end(), ::isspace),
                token.end());
    if (!token.empty()) {
      try {
        double val = std::stod(token);
        numbers.push_back(val);
      } catch (...) {
        // 忽略无效数字
      }
    }
  }
}

void LibertyParser::skip_continuation_and_comma() {
  skip_whitespace();
  if (peek_char() == ',') {
    next_char();
    skip_whitespace();
  }
  if (peek_char() == '\\') {
    // 续行符，跳过它和后面的换行符
    next_char();
    skip_whitespace();
    if (peek_char() == '\n') {
      next_char();
    }
    skip_whitespace();
  }
}

void LibertyParser::parse_unquoted_value_row(
    std::vector<std::vector<double>> &values) {
  double value = read_number();
  if (value != 0.0 || peek_char() == '0' || peek_char() == '.') {
    // 成功读取了数字
    std::vector<double> row;
    row.push_back(value);

    // 继续读取同一行的其他数字
    skip_whitespace();
    while (peek_char() == ',') {
      next_char();
      skip_whitespace();
      double next_val = read_number();
      row.push_back(next_val);
      skip_whitespace();
    }

    if (!row.empty()) {
      values.push_back(row);
    }
  } else {
    // 无法读取数字，可能是其他内容
    if (peek_char() == ',') {
      next_char();
    } else if (peek_char() == '\\') {
      next_char();
      skip_whitespace();
      if (peek_char() == '\n') {
        next_char();
      }
    } else {
      // 结束
    }
  }
}

void LibertyParser::parse_internal_power(Pin &pin) {
  skip_whitespace();
  // 跳过internal_power后的括号，如 internal_power()
  if (peek_char() == '(') {
    skip_parentheses_block();
    skip_whitespace();
  }

  InternalPower internal;

  // 进入internal_power块
  if (peek_char() == '{') {
    next_char();
    while (!eof() && !has_error) {
      skip_whitespace();
      skip_comment();
      if (peek_char() == '}') {
        next_char();
        break;
      }

      std::string attr = read_identifier();
      if (attr.empty()) {
        if (!eof() && peek_char() != '}') {
          next_char();
        }
        continue;
      }

      skip_whitespace();
      if (peek_char() == ':') {
        next_char();
        skip_whitespace();

        if (attr == "when") {
          internal.when = read_string();
        } else if (attr == "related_pin") {
          internal.related_pin = read_string();
        }

        skip_whitespace();
        if (peek_char() == ';')
          next_char();
      } else if (attr == "rise_power" || attr == "fall_power") {
        // 解析功耗查找表
        skip_whitespace();
        std::string template_name;
        if (peek_char() == '(') {
          next_char();
          skip_whitespace();
          template_name = read_identifier();
          skip_whitespace();
          if (peek_char() == ')') {
            next_char();
          }
          skip_whitespace();
        }
        if (peek_char() == '{') {
          next_char();
          LookupTable lut;
          if (!template_name.empty()) {
            lut.template_name = template_name;
          }
          parse_lookup_table(lut, attr);
          if (attr == "rise_power") {
            internal.rise_power = lut;
          } else if (attr == "fall_power") {
            internal.fall_power = lut;
          }
        }
      } else {
        skip_unknown_attribute();
      }
    }
  }

  pin.internal_power.push_back(internal);
}

} // namespace celllib
