#include "cell/cell_data_structure.hpp"
#include "cell/liberty_parser.hpp"
#include "lib_parser/Lib.hh"
#include "lib_parser/lib_to_celllib_converter.hpp"
#include <string>

#include "interface/verilog_adapter.hpp"

#include "interface/sdc_adapter.hpp"

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

