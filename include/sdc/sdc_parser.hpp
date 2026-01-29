#ifndef SDC_PARSER_HPP
#define SDC_PARSER_HPP

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <map>
#include <functional>

namespace sdc {

// ============================================================================
// SDC 数据类型定义
// ============================================================================

/**
 * SDC 值类型：可以是数字、字符串或列表
 */
struct SDCValue {
    enum Type {
        NUMBER,
        STRING,
        LIST
    };
    
    Type type;
    double number_value;
    std::string string_value;
    std::vector<SDCValue> list_value;
    
    SDCValue() : type(NUMBER), number_value(0.0) {}
    SDCValue(double val) : type(NUMBER), number_value(val) {}
    SDCValue(const std::string& val) : type(STRING), string_value(val) {}
    SDCValue(const std::vector<SDCValue>& val) : type(LIST), list_value(val) {}
};

/**
 * SDC 对象集合：表示 [get_ports CLKM] 或 [all_clocks] 等
 */
struct SDCObjectCollection {
    std::string collection_type;  // "get_ports", "all_clocks", "get_clocks" 等
    std::vector<std::string> arguments;  // 参数列表，如 "CLKM"
    
    SDCObjectCollection() {}
    SDCObjectCollection(const std::string& type, const std::vector<std::string>& args = {})
        : collection_type(type), arguments(args) {}
};

/**
 * SDC 命令选项
 */
struct SDCOption {
    std::string name;   // 选项名（不含 -），如 "name", "period"
    SDCValue value;     // 选项值
    
    SDCOption() {}
    SDCOption(const std::string& n, const SDCValue& v) : name(n), value(v) {}
};

/**
 * SDC 命令
 */
struct SDCCommand {
    std::string command_name;                    // 命令名，如 "create_clock"
    std::vector<SDCValue> values;                // 选项值
    std::vector<SDCOption> options;              // 选项列表
    std::vector<SDCObjectCollection> objects;    // 对象集合列表
    
    SDCCommand() {}
    SDCCommand(const std::string& name) : command_name(name) {}
};

// ============================================================================
// SDC 解析器接口
// ============================================================================

/**
 * SDC 解析器接口
 * 使用回调函数模式，类似 verilog parser
 */
class SDCParserInterface {
public:
    virtual ~SDCParserInterface() {}
    
    // 处理 create_clock 命令
    virtual void create_clock(
        const std::string& name,
        double period,
        const std::vector<double>& waveform,  // {rise_time, fall_time}
        const SDCObjectCollection& objects
    ) = 0;
    
    // 处理 set_clock_uncertainty 命令
    virtual void set_clock_uncertainty(
        double setup_uncertainty,
        double hold_uncertainty,
        const SDCObjectCollection& objects
    ) = 0;
    
    // 处理 set_clock_transition 命令
    virtual void set_clock_transition(
        double rise_transition,
        double fall_transition,
        const SDCObjectCollection& objects
    ) = 0;
    
    // 处理 set_input_delay 命令
    virtual void set_input_delay(
        double delay_value,
        const std::string& clock_name,
        const SDCObjectCollection& objects
    ) = 0;
    
    // 处理 set_output_delay 命令
    virtual void set_output_delay(
        double delay_value,
        const std::string& clock_name,
        const SDCObjectCollection& objects
    ) = 0;

    virtual void read_verilog(
        const std::string &filename
    ) = 0;

    virtual void read_liberty(
        const std::string &filename
    ) = 0;
    
    // 处理其他命令（可选）
    virtual void unknown_command(const SDCCommand& cmd) {
        
    }
};

// ============================================================================
// 命令处理器函数类型
// ============================================================================

/**
 * 命令处理器函数类型
 * 用户可以通过注册函数来处理自定义命令，无需修改接口
 */
using CommandHandler = std::function<void(SDCParserInterface*, const SDCCommand&)>;

// ============================================================================
// 命令处理器函数类型
// ============================================================================

/**
 * 命令处理器函数类型
 * 用户可以通过注册函数来处理自定义命令
 */
using CommandHandler = std::function<void(SDCParserInterface*, const SDCCommand&)>;

// ============================================================================
// SDC 解析器
// ============================================================================

/**
 * SDC 解析器：解析 SDC 文件并调用相应的回调函数
 * 支持命令注册机制，方便扩展新命令
 */
class SDCParser {
public:
    SDCParser(SDCParserInterface* interface) : interface_(interface) {
        register_default_commands();
    }
    
    /**
     * 解析 SDC 文件
     * @param file_path SDC 文件路径
     */
    void parse_file(const std::string& file_path);
    
    /**
     * 解析 SDC 字符串
     * @param content SDC 内容字符串
     */
    void parse_string(const std::string& content);
    
    /**
     * 注册自定义命令处理器
     * @param command_name 命令名称
     * @param handler 处理函数
     * 
     * 示例：
     *   parser.register_command("set_false_path", [](SDCParserInterface* iface, const SDCCommand& cmd) {
     *       // 处理 set_false_path 命令
     *   });
     */
    void register_command(const std::string& command_name, CommandHandler handler) {
        command_handlers_[command_name] = handler;
    }
    
    /**
     * 取消注册命令处理器
     */
    void unregister_command(const std::string& command_name) {
        command_handlers_.erase(command_name);
    }
    
private:
    SDCParserInterface* interface_;
    std::map<std::string, CommandHandler> command_handlers_;  // 命令处理器注册表
    
    // 注册默认命令处理器
    void register_default_commands();
    
    // 解析辅助函数
    std::string preprocess(const std::string& content);  // 预处理（处理续行等）
    std::vector<std::string> tokenize(const std::string& line);  // 词法分析
    SDCCommand parse_command(const std::vector<std::string>& tokens);  // 解析命令
    SDCObjectCollection parse_object_collection(const std::string& token);  // 解析对象集合
    SDCValue parse_value(const std::string& token);  // 解析值
    std::vector<double> parse_waveform(const std::vector<SDCValue>& list);  // 解析波形列表
    
    // 默认命令处理器（内部辅助函数）
    void handle_create_clock(const SDCCommand& cmd);
    void handle_set_clock_uncertainty(const SDCCommand& cmd);
    void handle_set_clock_transition(const SDCCommand& cmd);
    void handle_set_input_delay(const SDCCommand& cmd);
    void handle_set_output_delay(const SDCCommand& cmd);
    void handle_read_verilog(const SDCCommand &cmd);
    void handle_read_liberty(const SDCCommand &cmd);
};

} // namespace sdc

#endif // SDC_PARSER_HPP
