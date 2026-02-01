#ifndef LIBERTY_PARSER_HPP
#define LIBERTY_PARSER_HPP

#include "cell/cell_data_structure.hpp"
#include <string>
#include <fstream>
#include <memory>

namespace celllib {

/**
 * Liberty Parser: 解析Liberty格式的标准单元库文件
 * 
 * 支持的格式特性：
 * - 库级别属性（time_unit, capacitive_load_unit等）
 * - 单元定义（cell）
 * - Pin定义（pin）
 * - 时序信息（timing）
 * - 查找表（lookup table）
 * - 固定延迟值（intrinsic_rise/intrinsic_fall）
 */
class LibertyParser {
public:
    /**
     * 构造函数：从文件路径解析
     */
    explicit LibertyParser(const std::string& file_path);
    
    /**
     * 构造函数：从输入流解析
     */
    explicit LibertyParser(std::istream& input);
    
    /**
     * 解析文件并返回CellLibrary
     */
    CellLibrary parse();
    
    /**
     * 检查解析是否成功
     */
    bool is_valid() const { return !has_error; }
    
    /**
     * 获取错误信息
     */
    const std::string& get_error() const { return error_message; }

private:
    std::string file_path;
    std::unique_ptr<std::ifstream> file_stream;
    std::istream* input_stream;
    bool has_error;
    std::string error_message;
    int line_number;
    
    // 解析状态
    CellLibrary library;
    
    // ========== 字符和Token处理函数 ==========
    void skip_whitespace();
    void skip_comment();
    bool expect_token(const std::string& token);
    std::string read_identifier();
    std::string read_string();
    double read_number();
    void report_error(const std::string& message);
    
    // ========== 工具函数 ==========
    bool is_whitespace(char c);
    bool is_identifier_char(char c);
    char peek_char();
    char next_char();
    bool eof();
    
    // ========== 枚举转换函数 ==========
    PinDirection parse_pin_direction(const std::string& dir_str);
    TimingSense parse_timing_sense(const std::string& sense_str);
    TimingType parse_timing_type(const std::string& type_str);
    
    // ========== 通用属性解析 ==========
    std::string read_attribute_value_string();  // 读取属性值（字符串或标识符）
    double read_attribute_value_number();      // 读取属性值（数字）
    
    // ========== 单位转换函数 ==========
    double parse_time_unit(const std::string& unit_str);
    double parse_capacitance_unit(const std::string& unit_str);
    double convert_time_to_ps(double time_value);  // 将时间值转换为ps单位
    
    // ========== 辅助工具函数：跳过未知内容 ==========
    void skip_parentheses_block();
    void skip_brace_block();
    void skip_attribute_value();
    void skip_unknown_attribute();
    void skip_continuation_and_comma();
    
    // ========== 库级别解析 ==========
    void parse_library();
    void parse_library_attributes();
    void parse_time_unit_attribute();
    void parse_capacitive_load_unit_attribute();
    void parse_table_template();
    
    // ========== 单元级别解析 ==========
    void parse_cell(StandardCell& cell);
    void parse_cell_area_attribute(StandardCell& cell);
    void parse_cell_drive_strength_attribute(StandardCell& cell);
    void parse_cell_leakage_power_attribute(StandardCell& cell);
    void parse_ff_definition(StandardCell& cell);
    void parse_pg_pin(StandardCell& cell);
    void parse_leakage_power(StandardCell& cell);
    
    // ========== Pin级别解析 ==========
    void parse_pin(Pin& pin);
    void parse_pin_timing_block(Pin& pin);
    void parse_pin_attribute(Pin& pin, const std::string& attr);
    void parse_pin_max_capacitance_attribute(Pin& pin);
    void parse_pin_related_power_pin_attribute(Pin& pin);
    void parse_pin_related_ground_pin_attribute(Pin& pin);
    void parse_internal_power(Pin& pin);
    
    // ========== Timing Arc解析 ==========
    void parse_timing_arc(TimingArc& arc, const std::string& output_pin_name);
    bool is_lookup_table_attribute(const std::string& attr);
    bool is_constraint_attribute(const std::string& attr);
    void parse_timing_arc_lookup_table(TimingArc& arc, const std::string& attr);
    void parse_timing_arc_constraint(TimingArc& arc, const std::string& attr);
    void parse_timing_arc_attribute(TimingArc& arc, const std::string& attr);
    
    // ========== 查找表解析 ==========
    void parse_lookup_table(LookupTable& lut, const std::string& table_type);
    void parse_lookup_table_index_1(LookupTable& lut);
    void parse_lookup_table_index_2(LookupTable& lut);
    void parse_lookup_table_values(LookupTable& lut);
    void parse_index_list(std::vector<double>& indices);
    void parse_values_list(std::vector<std::vector<double>>& values, size_t num_rows);
    void parse_comma_separated_numbers(const std::string& str, std::vector<double>& numbers);
    void parse_unquoted_value_row(std::vector<std::vector<double>>& values);
};

} // namespace celllib

#endif // LIBERTY_PARSER_HPP
