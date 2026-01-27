#include "sdc_parser.hpp"
#include <cassert>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cctype>

namespace sdc {

// 预处理：处理续行符和注释
std::string SDCParser::preprocess(const std::string& content) {
    std::istringstream iss(content);
    std::ostringstream oss;
    std::string line;
    bool continuation = false;
    
    while (std::getline(iss, line)) {
        // 移除行尾的续行符
        if (!line.empty() && line.back() == '\\') { 
            line.pop_back(); // 移出最后的换行符
            continuation = true;
        } else {
            continuation = false;
        }
        
        // 移除注释（# 到行尾）
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        
        // 移除首尾空白
        while (!line.empty() && std::isspace(line.front())) {
            line.erase(0, 1);
        }
        while (!line.empty() && std::isspace(line.back())) {
            line.pop_back();
        }
        
        if (!line.empty()) {
            oss << line;
            if (continuation) {
                oss << " ";  // 续行时添加空格
            } else {
                oss << "\n";
            }
        }
    }
    
    return oss.str();
}

// 词法分析：将一行分解为 token
std::vector<std::string> SDCParser::tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current_token;
    bool in_brackets = false;  // 在方括号内
    bool in_braces = false;    // 在大括号内
    bool in_quotes = false;    // 在引号内
    int bracket_depth = 0;
    int brace_depth = 0;
    
    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        
        if (in_quotes) {
            current_token += c;
            if (c == '"') {
                in_quotes = false;
                tokens.push_back(current_token);
                current_token.clear();
            }
        } else if (c == '"') {
            in_quotes = true;
            current_token += c;
        } else if (c == '[') {
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
            in_brackets = true;
            bracket_depth++;
            current_token += c;
        } else if (c == ']') {
            current_token += c;
            bracket_depth--;
            if (bracket_depth == 0) {
                in_brackets = false;
                tokens.push_back(current_token);
                current_token.clear();
            }
        } else if (c == '{') {
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
            in_braces = true;
            brace_depth++;
            current_token += c;
        } else if (c == '}') {
            current_token += c;
            brace_depth--;
            if (brace_depth == 0) {
                in_braces = false;
                tokens.push_back(current_token);
                current_token.clear();
            }
        } else if (std::isspace(c) && !in_brackets && !in_braces) {
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
        } else {
            current_token += c;
        }
    }
    
    if (!current_token.empty()) {
        tokens.push_back(current_token);
    }
    
    return tokens;
}

// 解析对象集合，如 [get_ports CLKM] 或 [all_clocks]
SDCObjectCollection SDCParser::parse_object_collection(const std::string& token) {
    SDCObjectCollection collection;
    
    // 移除方括号
    if (token.front() == '[' && token.back() == ']') {
        std::string content = token.substr(1, token.length() - 2);
        
        // 分割为命令和参数
        std::istringstream iss(content);
        std::string cmd;
        iss >> cmd;
        
        collection.collection_type = cmd;
        
        // 读取参数
        std::string arg;
        while (iss >> arg) {
            collection.arguments.push_back(arg);
        }
    }
    
    return collection;
}

// 解析值：数字、字符串或列表
SDCValue SDCParser::parse_value(const std::string& token) {
    // 检查是否是列表 { ... }
    if (token.front() == '{' && token.back() == '}') {
        std::string content = token.substr(1, token.length() - 2);
        std::istringstream iss(content);
        std::vector<SDCValue> list;
        
        std::string item;
        while (iss >> item) {
            list.push_back(parse_value(item));
        }
        
        return SDCValue(list);
    }
    
    // 检查是否是数字
    try {
        double num = std::stod(token);
        return SDCValue(num);
    } catch (...) {
        // 不是数字，作为字符串
        return SDCValue(token);
    }
}

// 解析波形列表 {rise_time fall_time}
std::vector<double> SDCParser::parse_waveform(const std::vector<SDCValue>& list) {
    std::vector<double> waveform;
    for (const auto& val : list) {
        if (val.type == SDCValue::NUMBER) {
            waveform.push_back(val.number_value);
        }
    }
    return waveform;
}

// 解析命令
SDCCommand SDCParser::parse_command(const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        return SDCCommand();
    }
    
    SDCCommand cmd(tokens[0]);
    
    // 解析选项和对象集合
    for (size_t i = 1; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];
        
        // 检查是否是选项（以 - 开头）
        if (token.front() == '-') {
            std::string option_name = token.substr(1);
            
            // 读取选项值
            if (i + 1 < tokens.size()) {
                SDCValue value = parse_value(tokens[i + 1]);
                cmd.options.emplace_back(option_name, value);
                i++;  // 跳过值
            } else {
                // 选项没有值（布尔选项）
                cmd.options.emplace_back(option_name, SDCValue(1.0));
            }
        } else if (token.front() == '[') {
            // 对象集合
            cmd.objects.push_back(parse_object_collection(token));
        } else {
            cmd.values.push_back(parse_value(token));
        }
    }
    
    return cmd;
}

// 注册默认命令处理器
void SDCParser::register_default_commands() {
    command_handlers_["read_verilog"] = [this](SDCParserInterface* iface, const SDCCommand& cmd) {
        handle_read_verilog(cmd);
    };

    command_handlers_["create_clock"] = [this](SDCParserInterface* iface, const SDCCommand& cmd) {
        handle_create_clock(cmd);
    };
    
    command_handlers_["set_clock_uncertainty"] = [this](SDCParserInterface* iface, const SDCCommand& cmd) {
        handle_set_clock_uncertainty(cmd);
    };
    
    command_handlers_["set_clock_transition"] = [this](SDCParserInterface* iface, const SDCCommand& cmd) {
        handle_set_clock_transition(cmd);
    };
    
    command_handlers_["set_input_delay"] = [this](SDCParserInterface* iface, const SDCCommand& cmd) {
        handle_set_input_delay(cmd);
    };
    
    command_handlers_["set_output_delay"] = [this](SDCParserInterface* iface, const SDCCommand& cmd) {
        handle_set_output_delay(cmd);
    };
}

// 默认命令处理器实现
void SDCParser::handle_read_verilog(const SDCCommand& cmd) {
    for(const auto &value : cmd.values) {
        if(value.type == SDCValue::STRING) {
            return interface_->read_verilog(value.string_value);
        }
    }

    for(const auto &opt : cmd.options) {
        if(opt.name == "file") {
            return interface_->read_verilog(opt.value.string_value);
        }
    }

    assert(false && "read_verilog command should contants a file name");
}

void SDCParser::handle_create_clock(const SDCCommand& cmd) {
    std::string clock_name;
    double period = 0.0;
    std::vector<double> waveform;
    SDCObjectCollection objects;
    
    for (const auto& opt : cmd.options) {
        if (opt.name == "name") {
            clock_name = opt.value.string_value;
        } else if (opt.name == "period") {
            period = opt.value.number_value;
        } else if (opt.name == "waveform") {
            waveform = parse_waveform(opt.value.list_value);
        }
    }
    
    if (!cmd.objects.empty()) {
        objects = cmd.objects[0];
    }
    
    interface_->create_clock(clock_name, period, waveform, objects);
}

void SDCParser::handle_set_clock_uncertainty(const SDCCommand& cmd) {
    double setup_uncertainty = 0.0;
    double hold_uncertainty = 0.0;
    SDCObjectCollection objects;
    
    for (const auto& opt : cmd.options) {
        if (opt.name == "setup") {
            setup_uncertainty = opt.value.number_value;
        } else if (opt.name == "hold") {
            hold_uncertainty = opt.value.number_value;
        }
    }
    
    if (!cmd.objects.empty()) {
        objects = cmd.objects[0];
    }
    
    interface_->set_clock_uncertainty(setup_uncertainty, hold_uncertainty, objects);
}

void SDCParser::handle_set_clock_transition(const SDCCommand& cmd) {
    double rise_transition = 0.0;
    double fall_transition = 0.0;
    SDCObjectCollection objects;
    
    for (const auto& opt : cmd.options) {
        if (opt.name == "rise") {
            rise_transition = opt.value.number_value;
        } else if (opt.name == "fall") {
            fall_transition = opt.value.number_value;
        }
    }
    
    if (!cmd.objects.empty()) {
        objects = cmd.objects[0];
    }
    
    interface_->set_clock_transition(rise_transition, fall_transition, objects);
}

void SDCParser::handle_set_input_delay(const SDCCommand& cmd) {
    double delay_value = 0.0;
    std::string clock_name;
    SDCObjectCollection objects;
    
    for (const auto& opt : cmd.options) {
        if (opt.name == "clock") {
            clock_name = opt.value.string_value;
        } else if (opt.name == "max" || opt.name == "min") {
            delay_value = opt.value.number_value;
        }
    }
    
    if (!cmd.objects.empty()) {
        objects = cmd.objects[0];
    }
    
    interface_->set_input_delay(delay_value, clock_name, objects);
}

void SDCParser::handle_set_output_delay(const SDCCommand& cmd) {
    double delay_value = 0.0;
    std::string clock_name;
    SDCObjectCollection objects;
    
    for (const auto& opt : cmd.options) {
        if (opt.name == "clock") {
            clock_name = opt.value.string_value;
        } else if (opt.name == "max" || opt.name == "min") {
            delay_value = opt.value.number_value;
        }
    }
    
    if (!cmd.objects.empty()) {
        objects = cmd.objects[0];
    }
    
    interface_->set_output_delay(delay_value, clock_name, objects);
}

// 解析字符串
void SDCParser::parse_string(const std::string& content) {
    std::string processed = preprocess(content);
    std::istringstream iss(processed);
    std::string line;
    
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty()) continue;
        
        SDCCommand cmd = parse_command(tokens);
        
        // 查找命令处理器
        auto it = command_handlers_.find(cmd.command_name);
        if (it != command_handlers_.end()) {
            // 调用注册的处理器
            it->second(interface_, cmd);
        } else {
            // 未知命令，调用通用处理器
            interface_->unknown_command(cmd);
        }
    }
}

// 解析文件
void SDCParser::parse_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open SDC file: " << file_path << std::endl;
        return;
    }
    
    std::ostringstream oss;
    oss << file.rdbuf(); // 拿到文件缓冲区的指针
    parse_string(oss.str());
}

} // namespace sdc
