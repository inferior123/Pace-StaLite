#include "cell/cell_data_structure.hpp"
#include "sta/sta_data_structures.hpp"
#include "parser-verilog/verilog_data.hpp"
#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>
#include <unordered_set>
#include <cmath>

using namespace verilog;

namespace sta {
    SignalSpec STAWorker::get_signal_bits(const std::string &signame) const {
        auto it = signal_registry.find(signame);
        if(it != signal_registry.end()) {
            return it->second;
        }

        assert(false && "trans wire name to spec, find a unknown wire"); 
        return {};
    }

    bool STAWorker::is_reg(std::string name) {
        // 遍历 instances，查找是否有对应的 REG 实例
        // REG 实例的命名规则是 "reg_" + name
        std::string expected_instance_name = "reg_" + name;
        for (const auto& instance : instances) {
            if (instance->module_name == "REG" && instance->instance_name == expected_instance_name) {
                return true;
            }
        }
        return false;
    }

    SignalSpec STAWorker::convert_to_signalspec(const LHS &lhs) {
        // Left hand side can be: a wire, a bit in a wire, a part of a wire 
        // std::vector<std::variant<std::string, NetBit, NetRange>>
        SignalSpec result;

        for(const auto &item : lhs) {
            std::visit([&result, this](const auto &elem){
                using T = std::decay_t<decltype(elem)>;

                if constexpr (std::is_same_v<T, std::string>) {
                    std::string target;
                    if(is_reg(elem)) target = "reg_" + elem + "_d"; else target = elem;
                    auto bits = this->get_signal_bits(target);
                    result.insert(result.end(), bits.begin(), bits.end());
                } else if constexpr (std::is_same_v<T, NetBit>) {
                    std::string target;
                    if(is_reg(elem.name)) target = "reg_" + elem.name + "_d"; else target = elem.name;
                    result.emplace_back(target, elem.bit);
                } else if constexpr (std::is_same_v<T, NetRange>) {
                    int left = elem.beg;
                    int right = elem.end;
                    // Keep the same convention as collect_net: only support ascending [left:right].
                    assert(left <= right && "unsupported NetRange with left > right; use ascending range like [0:7]");
                    std::string target;
                    if(is_reg(elem.name)) target = "reg_" + elem.name + "_d"; else target = elem.name;
                    for (int i = left; i <= right; ++i)
                        result.emplace_back(target, i);
                } else {
                    assert(false && "should not reach here");
                }
            }, item);
        }

        return result;
    }

    SignalSpec STAWorker::convert_to_signalspec(const RHS &rhs) {
        // Right hand side can be: a wire, a bit in a wire, a part of a wire, a constant
        // std::vector<std::variant<std::string, NetBit, NetRange, Constant>>
        SignalSpec result;

        for(const auto &item : rhs) {
            std::visit([&result, this](const auto &elem){
                using T = std::decay_t<decltype(elem)>;

                if constexpr (std::is_same_v<T, std::string>) {
                    std::string target;
                    if(is_reg(elem)) target = "reg_" + elem + "_q"; else target = elem;
                    auto bits = this->get_signal_bits(target);
                    result.insert(result.end(), bits.begin(), bits.end());
                } else if constexpr (std::is_same_v<T, NetBit>) {
                    std::string target;
                    if(is_reg(elem.name)) target =  "reg_" + elem.name + "_q"; else target = elem.name;
                    result.emplace_back(target, elem.bit);
                } else if constexpr (std::is_same_v<T, NetRange>) {
                    int left = elem.beg;
                    int right = elem.end;
                    assert(left <= right && "unsupported NetRange with left > right; use ascending range like [0:7]");
                    std::string target;
                    if(is_reg(elem.name)) target =  "reg_" + elem.name + "_q"; else target = elem.name;
                    for (int i = left; i <= right; ++i)
                        result.emplace_back(target, i);
                } else if constexpr (std::is_same_v<T, Constant>) {
                    // constants don't correspond to a SignalBit; ignore for connectivity
                    // (for proper bit-blasting of assigns with constants, handle separately)
                } else {
                    assert(false && "should not reach here");
                }
            }, item);
        }

        return result;
    }

    void STAWorker::collect_net(verilog::Net &net) {
        if(net.type == verilog::NetType::WIRE) {
            int left = net.beg == -1 ? 0 : net.beg;
            int right = net.end == -1 ? 0 : net.end;

            // For this prototype, only accept ascending ranges like [0:7].
            // Reject descending ranges like [7:0].
            if (left > right) {
                assert(false && "unsupported net range: left > right (e.g. [7:0]); use ascending range like [0:7]");
            }
            for(auto &name : net.names) {
                std::vector<SignalBit> bits;
                for(int i = left; i <= right; i++) {
                    sta::SignalBit bit(name, i);
                    timing_data[bit] = SignalTimingData();
                    bits.push_back(bit);
                }
                signal_registry[name] = std::move(bits);
            }
        } else if(net.type == verilog::NetType::REG){
           assert(false && "should not meet a reg type in the netlist"); 
        } else {
            assert(false && "other type not implement yet");
        }
    }

    void STAWorker::collect_port(verilog::Port &port) {
        int left = port.beg == -1 ? 0 : port.beg;
        int right = port.end == -1 ? 0 : port.end;

        // only accept ranges like [0:7].
        // dont alllow ranges like [7:0].
        if (left > right) {
            assert(false && "unsupported net range: left > right (e.g. [7:0]); use ascending range like [0:7]");
        }

        for(auto &sig_name : port.names) {
            std::vector<SignalBit> bits;
            for(int i = left; i <= right; i++) {
                SignalBit bit(sig_name, i);
                if (!timing_data.count(bit)) {
                    timing_data[bit] = SignalTimingData{};
                }
                
                // Always add to signal_registry
                bits.push_back(bit);
                
                // Process based on port direction
                if(port.dir == PortDirection::INOUT) {
                    assert(false && "not suport the INOUT port yet");
                } else if(port.dir == PortDirection::INPUT) {
                    SignalBit input_canonical = sigmap.find(bit);
                    top_module_inputs.insert(input_canonical);  // 记录顶层模块输入端口
                    driven_signals.insert(input_canonical);
                    timing_queue.push_back(input_canonical);
                    arrival_time[input_canonical] = 0; 
                } else {
                    // OUTPUT
                    assert(port.dir == PortDirection::OUTPUT && "invalid port dir");
                    SignalBit output_canonical = sigmap.find(bit);
                    endpoints[output_canonical] = TimingEndpoint{nullptr, sig_name};  // 标记为top_module的output
                    endpoints[output_canonical].Setup_req = 0;
                    endpoints[output_canonical].Hold_req = 0;
                }
            }
            signal_registry[sig_name] = std::move(bits);
        }
    }

    void STAWorker::collect_instance(verilog::Instance &inst) {
        // 如果使用 CellLibrary，验证单元是否存在
        if (cell_library_) {
            const auto* cell = cell_library_->get_cell(inst.module_name);
            if (!cell) {
                std::cerr << "cannot find the cell " << inst.module_name << " in CellLibrary" << std::endl;
                // 调试信息：打印库中所有可用的单元名称
                auto cell_names = cell_library_->get_cell_names();
                std::cerr << "Available cells in library (" << cell_names.size() << "): ";
                for (const auto& name : cell_names) {
                    std::cerr << name << " ";
                }
                std::cerr << std::endl;
                assert(false && "Cell not found in CellLibrary");
            }

            auto input_pin_names = cell->get_input_pins();
            auto output_pin_names = cell->get_output_pins();
            
            // 将vector转换为set以便快速查找
            std::unordered_set<std::string> input_pin_set(input_pin_names.begin(), input_pin_names.end());
            std::unordered_set<std::string> output_pin_set(output_pin_names.begin(), output_pin_names.end());
            
            for(const auto &pin_name_variant : inst.pin_names) {
                // 从variant中提取pin名称
                std::string pin_name;
                if (std::holds_alternative<std::string>(pin_name_variant)) {
                    pin_name = std::get<std::string>(pin_name_variant);
                } else if (std::holds_alternative<NetBit>(pin_name_variant)) {
                    pin_name = std::get<NetBit>(pin_name_variant).name;
                } else if (std::holds_alternative<NetRange>(pin_name_variant)) {
                    pin_name = std::get<NetRange>(pin_name_variant).name;
                }
                
                // 检查pin是否在input或output pin列表中
                if (input_pin_set.find(pin_name) == input_pin_set.end() && 
                    output_pin_set.find(pin_name) == output_pin_set.end()) {
                    std::cerr << "Invalid pin \"" << pin_name << "\" for cell \"" 
                              << inst.module_name << "\" (instance: " << inst.inst_name << ")" << std::endl;
                    std::cerr << "Available input pins: ";
                    for (const auto& name : input_pin_names) {
                        std::cerr << name << " ";
                    }
                    std::cerr << std::endl;
                    std::cerr << "Available output pins: ";
                    for (const auto& name : output_pin_names) {
                        std::cerr << name << " ";
                    }
                    std::cerr << std::endl;
                    assert(false && "Invalid pin name for cell");
                }
            }
        } else {
            assert(false && "cannot find standard library");
        }

        auto instance = std::make_unique<Instance>(inst.module_name, inst.inst_name);
        if(inst.pin_names.empty()) {
            assert(false && "Positional port connection not supported in flattened netlist");
        }

        for(size_t i = 0;i < inst.pin_names.size() && i < inst.net_names.size() ;i++) {
            std::string port_name = std::get<std::string>(inst.pin_names[i]);
            SignalSpec signals = convert_to_signalspec(inst.net_names[i]);
            instance->connections[port_name] = std::move(signals);
        }
        instances.push_back(std::move(instance));
    }

    void STAWorker::collect_assign(verilog::Assignment &assign) {
        SignalSpec lhs = convert_to_signalspec(assign.lhs);
        SignalSpec rhs = convert_to_signalspec(assign.rhs);
        
        // 建立信号连接
        sigmap.add_connection(lhs, rhs);
    }

    // 辅助函数：从查找表中获取悲观值（最大值）
    double get_pessimistic_delay_from_lut(const celllib::LookupTable& lut) {
        double max_delay = 0.0;
        for (const auto& row : lut.values) {
            for (double val : row) {
                if (val > max_delay) {
                    max_delay = val;
                }
            }
        }
        
        return max_delay;
    }

    void STAWorker::build_fanouts() {
        static SignalBit global_clk("__clk__", 0);
        
        // 必须要有CellLibrary，不再使用硬编码
        if (!cell_library_) {
            assert(false && "CellLibrary is required, no hardcoded fallback");
            return;
        }
        
        // 初始化虚拟时钟信号的时序数据（只需初始化一次）
        if (!timing_data.count(global_clk)) {
            timing_data[global_clk] = SignalTimingData();
            // 虚拟时钟信号的arrival_time默认为0（理想时钟树，无delay）
            arrival_time[global_clk] = 0;
            // 标记虚拟时钟为已驱动，并加入队列开始传播
            driven_signals.insert(global_clk);
            timing_queue.push_back(global_clk);
        }
        
        for(auto &instance : instances) {
            // 从CellLibrary获取单元信息
            const auto* cell = cell_library_->get_cell(instance->module_name);
            if (!cell) {
                std::cerr << "Warning: Cannot find cell '" << instance->module_name 
                          << "' in CellLibrary, skipping." << std::endl;
                continue;
            }
            
            // 判断是否为寄存器（时序单元）
            bool is_sequential = cell->ff.has_value();
            
            if (is_sequential) {
                // 处理寄存器：建立从时钟到输出端的路径
                const auto& ff_def = cell->ff.value();
                std::string clock_pin_name = ff_def.clocked_on.value_or("CK");  // 默认使用"CK" -> 全局时钟
                
                // 获取时钟pin的连接（如果存在）
                SignalBit clock_source = global_clk;  // 默认使用全局时钟
                if (instance->connections.count(clock_pin_name)) {
                    SignalSpec clock_signals = instance->connections[clock_pin_name];
                    if (!clock_signals.empty()) {
                        clock_source = sigmap.find(clock_signals[0]);
                    }
                }
                
                // 确保时钟源在timing_data中
                if (!timing_data.count(clock_source)) {
                    timing_data[clock_source] = SignalTimingData();
                }
                
                // 遍历所有输出pin，查找clock-to-Q时序弧
                auto output_pins = cell->get_output_pins();
                for (const auto& output_pin_name : output_pins) {
                    const auto* output_pin = cell->get_pin(output_pin_name);
                    if (!output_pin) continue;
                    
                    if (!instance->connections.count(output_pin_name)) {
                        continue;
                    }
                    SignalSpec output_signals = instance->connections[output_pin_name];
                    
                    // 查找clock-to-Q时序弧（RISING_EDGE或FALLING_EDGE类型，related_pin是时钟pin）
                    for (const auto& arc : output_pin->timing_arcs) {
                        if ((arc.timing_type == celllib::TimingType::RISING_EDGE || 
                             arc.timing_type == celllib::TimingType::FALLING_EDGE) &&
                            arc.related_pin == clock_pin_name) {
                            
                            // 根据颗粒度决定延迟值
                            int delay = 0;
                            if (analysis_granularity_ == AnalysisGranularity::COARSE) {
                                // COARSE模式：获取悲观延迟值
                                if (arc.cell_rise.has_value()) {
                                    delay = static_cast<int>(get_pessimistic_delay_from_lut(arc.cell_rise.value()));
                                } else if (arc.cell_fall.has_value()) {
                                    delay = static_cast<int>(get_pessimistic_delay_from_lut(arc.cell_fall.value()));
                                } else if (arc.intrinsic_rise.has_value() || arc.intrinsic_fall.has_value()) {
                                    double rise = arc.intrinsic_rise.value_or(0.0);
                                    double fall = arc.intrinsic_fall.value_or(0.0);
                                    delay = static_cast<int>(std::max(rise, fall));
                                }
                            }
                            // MEDIUM/FINE模式：delay保持为0（占位符），后续在calculate_timing_arcs中计算
                            
                            // 建立从时钟到输出的fanout
                            for (size_t i = 0; i < output_signals.size(); ++i) {
                                SignalBit output_connect = sigmap.find(output_signals[i]);
                                
                                if (!timing_data.count(output_connect)) {
                                    timing_data[output_connect] = SignalTimingData();
                                }
                                
                                timing_data[clock_source].fanouts.emplace_back(
                                    output_connect, delay, clock_pin_name, instance.get()
                                );
                                
                                // 标记输出端为已驱动
                                driven_signals.insert(output_connect);
                            }
                            break;  // 找到第一个clock-to-Q弧即可
                        }
                    }
                }
                
                // 建立寄存器输入端的fanout关系（用于负载电容计算）
                // 遍历所有输入pin（如D端），建立fanout关系，并将D端注册为endpoint
                auto input_pins = cell->get_input_pins();
                for (const auto& input_pin_name : input_pins) {
                    // 跳过时钟pin，因为时钟pin的fanout已经建立（clock-to-Q）
                    if (input_pin_name == clock_pin_name) {
                        continue;
                    }
                    
                    if (!instance->connections.count(input_pin_name)) {
                        continue;
                    }
                    
                    SignalSpec input_signals = instance->connections[input_pin_name];
                    
                    // 为每个输入信号建立fanout到寄存器实例（用于负载电容计算）
                    // 使用一个特殊的target_bit：创建一个虚拟信号，名称包含寄存器实例名和输入pin名
                    // 这样可以在calculate_load_capacitance中识别这是寄存器输入端的连接
                    for (size_t i = 0; i < input_signals.size(); ++i) {
                        SignalBit input_connect = sigmap.find(input_signals[i]);
                        
                        if (!timing_data.count(input_connect)) {
                            timing_data[input_connect] = SignalTimingData();
                        }
                        
                        // 创建一个虚拟的target_bit用于标识寄存器输入端连接
                        // 格式：__reg_input__<instance_name>__<pin_name>
                        SignalBit reg_input_target("__reg_input__" + instance->instance_name + "__" + input_pin_name, 0);
                        timing_data[input_connect].fanouts.emplace_back(
                            reg_input_target, 0, input_pin_name, instance.get()
                        );
                        
                        // 将寄存器的D端（数据输入端）注册为endpoint；若该 net 已是顶层输出（sigmap 合并），保留 primary output 端口名
                        TimingEndpoint ep(instance.get(), input_pin_name);
                        if (endpoints.count(input_connect) && endpoints[input_connect].sink == nullptr) {
                            ep.primary_output_port = endpoints[input_connect].port;
                        }
                        endpoints[input_connect] = std::move(ep);
                    }
                }
            } else {
                // 处理组合逻辑：建立从输入到输出的路径
                auto output_pins = cell->get_output_pins();
                for (const auto& output_pin_name : output_pins) {
                    const auto* output_pin = cell->get_pin(output_pin_name);
                    if (!output_pin) continue;
                    
                    if (!instance->connections.count(output_pin_name)) {
                        continue;
                    }
                    SignalSpec output_signals = instance->connections[output_pin_name];
                    
                    // 遍历时序弧（只处理组合逻辑类型）
                    for (const auto& arc : output_pin->timing_arcs) {
                        if (arc.timing_type != celllib::TimingType::COMBINATIONAL) {
                            continue;
                        }
                        
                        std::string input_pin_name = arc.related_pin;
                        if (!instance->connections.count(input_pin_name)) {
                            continue;
                        }
                        SignalSpec input_signals = instance->connections[input_pin_name];
                        
                        // 根据颗粒度决定延迟值
                        int delay = 0;
                        if (analysis_granularity_ == AnalysisGranularity::COARSE) {
                            // COARSE模式：获取悲观延迟值
                            if (arc.cell_rise.has_value()) {
                                delay = static_cast<int>(get_pessimistic_delay_from_lut(arc.cell_rise.value()));
                            } else if (arc.cell_fall.has_value()) {
                                delay = static_cast<int>(get_pessimistic_delay_from_lut(arc.cell_fall.value()));
                            } else if (arc.intrinsic_rise.has_value() || arc.intrinsic_fall.has_value()) {
                                double rise = arc.intrinsic_rise.value_or(0.0);
                                double fall = arc.intrinsic_fall.value_or(0.0);
                                delay = static_cast<int>(std::max(rise, fall));
                            }
                        }
                        // MEDIUM/FINE模式：delay保持为0（占位符），后续在calculate_timing_arcs中计算
                        
                        // 建立fanout连接
                        for (size_t i = 0; i < input_signals.size() && i < output_signals.size(); ++i) {
                            SignalBit input_connect = sigmap.find(input_signals[i]);
                            SignalBit output_connect = sigmap.find(output_signals[i]);
                            
                            if (!timing_data.count(input_connect)) {
                                timing_data[input_connect] = SignalTimingData();
                            }
                            if (!timing_data.count(output_connect)) {
                                timing_data[output_connect] = SignalTimingData();
                            }
                            
                            timing_data[input_connect].fanouts.emplace_back(
                                output_connect, delay, input_pin_name, instance.get()
                            );
                            
                            driven_signals.insert(output_connect);
                        }
                    }
                }
            }
        }
    }

    void STAWorker::calculate_load_capacitance() {
        assert(cell_library_); // 必须要加载单元库以后再调用这个函数

        for(auto &instance : instances) {
            const auto cell = cell_library_->get_cell(instance->module_name);
            if(cell == nullptr) {
                std::cerr << "could not find the standard cell " << instance->module_name << std::endl;
                assert(false);
            }

           auto output_pins = cell->get_output_pins();
           for(const auto &output_pin_name : output_pins) {
                double total_load = 0.0;

                if(!instance->connections.count(output_pin_name)) {
                    continue;
                }

                SignalSpec output_signals = instance->connections[output_pin_name];
                for(auto &output_signal : output_signals) {
                    SignalBit cannocial = sigmap.find(output_signal);

                    if(timing_data.count(cannocial)) {
                        for(auto fanout : timing_data[cannocial].fanouts) {
                            if(fanout.cell) {
                                // cell 存在，计算负载电容
                                // 包括正常的组合逻辑fanout和寄存器输入端的fanout
                                const auto *fanout_cell = cell_library_->get_cell(fanout.cell->module_name);
                                if(fanout_cell) {
                                    const auto *input_pin = fanout_cell->get_pin(fanout.port_name);
                                    if(input_pin && input_pin->capacitance.has_value()) {
                                        total_load += input_pin->capacitance.value();
                                    }
                                } else {
                                    std::cerr << "invalid cell " << fanout.cell->module_name << std::endl;
                                    assert(false);
                                }
                            }
                        }                     
                    }
                }

                instance->load_capacitance[output_pin_name] = total_load;
            }
        }
    }

    void STAWorker::calculate_timing_arcs() {
        assert(cell_library_);

        if(analysis_granularity_ == AnalysisGranularity::FINE) {
            assert(false && "find mode not implement yet");
        }

        if(analysis_granularity_ == AnalysisGranularity::COARSE) {
            return ; // 无需计算
        }

        for(auto &instance : instances) {
            const auto *cell = cell_library_->get_cell(instance->module_name);
            if(!cell) {
                std::cerr << "invalid cell " << instance->module_name << std::endl;
                assert(false);
            }

            auto output_pins = cell->get_output_pins();
            for(const auto &output_pin_name : output_pins) {
                const auto *output_pin = cell->get_pin(output_pin_name);

                if(!output_pin) {
                    continue;
                }

                double load_cap = instance->load_capacitance.count(output_pin_name) ? 
                    instance->load_capacitance[output_pin_name] : 0.0 ;
                
                // 调试输出：显示负载电容
                if (load_cap == 0.0) {
                    std::cerr << "  [WARNING] Load capacitance is 0 for instance " 
                              << instance->instance_name << " output pin " 
                              << output_pin_name << std::endl;
                }

                for(const auto &arc : output_pin->timing_arcs) {
                    // 当时设计的时候 pin 是output和input放在一起了
                    // 然后要找到所有的output的pin的transition
                    bool is_combinational = (arc.timing_type == celllib::TimingType::COMBINATIONAL);
                    bool is_clock_to_q = (arc.timing_type == celllib::TimingType::RISING_EDGE || 
                                          arc.timing_type == celllib::TimingType::FALLING_EDGE);

                    if(!is_combinational && !is_clock_to_q) {
                        continue;
                    }

                    std::string related_pin_name = arc.related_pin;

                    if(!instance->connections.count(related_pin_name) ||
                       !instance->connections.count(output_pin_name)) {
                        continue;
                    }

                    SignalSpec related_signals = instance->connections[related_pin_name];
                    SignalSpec output_signals = instance->connections[output_pin_name];

                    // 获取输入信号的转换时间（input_slew）
                    // rise延迟使用rise的slew，fall延迟使用fall的slew
                    double input_slew_rise = 0.0;
                    double input_slew_fall = 0.0;

                    if(!related_signals.empty()) {
                        SignalBit input_canonical = sigmap.find(related_signals[0]);
                        if(timing_data.count(input_canonical)) {
                            const auto &input_timing = timing_data[input_canonical];
                            
                            // 获取rise和fall的转换时间
                            if (input_timing.rise_transition_time.has_value()) {
                                input_slew_rise = input_timing.rise_transition_time.value();
                            } else {
                                std::cerr << "  [WARNING] rise_transition_time not available for signal " 
                                          << input_canonical.wire_name << "[" << input_canonical.bit_offset 
                                          << "], instance " << instance->instance_name 
                                          << ", input pin " << related_pin_name 
                                          << " (will use default value)" << std::endl;
                            }
                            if (input_timing.fall_transition_time.has_value()) {
                                input_slew_fall = input_timing.fall_transition_time.value();
                            } else {
                                std::cerr << "  [WARNING] fall_transition_time not available for signal " 
                                          << input_canonical.wire_name << "[" << input_canonical.bit_offset 
                                          << "], instance " << instance->instance_name 
                                          << ", input pin " << related_pin_name 
                                          << " (will use default value)" << std::endl;
                            }
                        }

                        // 如果转换时间为0，使用默认值
                        double default_slew = 0.0;
                        if(is_top_module_input(input_canonical)) {
                            // 作为顶层模块输入，使用查找表index_1的中间值作为默认值
                            if (arc.cell_rise.has_value() && !arc.cell_rise->index_1.empty()) {
                                size_t mid = arc.cell_rise->index_1.size() / 2;
                                default_slew = arc.cell_rise->index_1[mid];
                            }
                        } else {
                            // 其他情况：使用查找表index_1的中间值作为默认值
                            if (arc.cell_rise.has_value() && !arc.cell_rise->index_1.empty()) {
                                size_t mid = arc.cell_rise->index_1.size() / 2;
                                default_slew = arc.cell_rise->index_1[mid];
                            }
                        }
                        
                        // 如果rise或fall的转换时间为0，使用默认值
                        if (input_slew_rise == 0.0) {
                            input_slew_rise = default_slew;
                        }
                        if (input_slew_fall == 0.0) {
                            input_slew_fall = default_slew;
                        }
                    }

                    // 同时计算cell_rise和cell_fall的延迟，取最大值
                    double delay_rise = 0.0;
                    double delay_fall = 0.0;
                    double delay = 0.0;

                    // 计算cell_rise延迟（使用rise的slew）
                    if (arc.cell_rise.has_value() && arc.cell_rise->template_name.has_value()) {
                        std::string template_name = arc.cell_rise->template_name.value();
                        const auto* templ = cell_library_->get_table_template(template_name);
                        if (templ && templ->variable_1.has_value() && templ->variable_2.has_value()) {
                            std::string var1 = templ->variable_1.value();
                            std::string var2 = templ->variable_2.value();
                            delay_rise = cell_library_->caculate_lookuptable(
                                arc.cell_rise.value(), input_slew_rise, load_cap, var1, var2);
                        }
                    } else if (arc.intrinsic_rise.has_value()) {
                        delay_rise = arc.intrinsic_rise.value();
                    }

                    // 计算cell_fall延迟（使用fall的slew）
                    if (arc.cell_fall.has_value() && arc.cell_fall->template_name.has_value()) {
                        std::string template_name = arc.cell_fall->template_name.value();
                        const auto* templ = cell_library_->get_table_template(template_name);
                        if (templ && templ->variable_1.has_value() && templ->variable_2.has_value()) {
                            std::string var1 = templ->variable_1.value();
                            std::string var2 = templ->variable_2.value();
                            delay_fall = cell_library_->caculate_lookuptable(
                                arc.cell_fall.value(), input_slew_fall, load_cap, var1, var2);
                        }
                    } else if (arc.intrinsic_fall.has_value()) {
                        delay_fall = arc.intrinsic_fall.value();
                    }

                    // 取最大值作为延迟
                    delay = std::max(delay_rise, delay_fall);
                    
                    if (delay == 0.0) {
                        std::cerr << "  [WARNING] Delay is 0 for instance " 
                                  << instance->instance_name << ", output pin " 
                                  << output_pin_name << ", related pin " 
                                  << related_pin_name << std::endl;
                    }
                    
                    // // 调试输出：显示计算的延迟值
                    // if (delay > 0) {
                    //     std::cout << "  [DEBUG] Calculated delay: " << delay << "ps for " 
                    //               << instance->instance_name << " " << output_pin_name 
                    //               << " (load_cap=" << load_cap << "ff, input_slew=" 
                    //               << input_slew << "ps, related_pin=" << related_pin_name << ")" << std::endl;
                    // } else if (delay == 0 && load_cap > 0) {
                    //     std::cerr << "  [WARNING] Delay is 0 for " << instance->instance_name 
                    //               << " " << output_pin_name << " (load_cap=" << load_cap 
                    //               << "ff, input_slew=" << input_slew << "ps)" << std::endl;
                    // }

                    // 计算转换时间（transition time）
                    // 同时计算rise_transition和fall_transition，分别记录
                    // rise_transition使用rise的slew，fall_transition使用fall的slew
                    double rise_transition_time = 0.0;
                    double fall_transition_time = 0.0;
                    
                    // 计算rise_transition（使用rise的slew）
                    if (arc.rise_transition.has_value() && arc.rise_transition->template_name.has_value()) {
                        std::string template_name = arc.rise_transition->template_name.value();
                        const auto* templ = cell_library_->get_table_template(template_name);
                        if (templ && templ->variable_1.has_value() && templ->variable_2.has_value()) {
                            std::string var1 = templ->variable_1.value();
                            std::string var2 = templ->variable_2.value();
                            rise_transition_time = cell_library_->caculate_lookuptable(
                                arc.rise_transition.value(), input_slew_rise, load_cap, var1, var2);
                        }
                    }
                    
                    // 计算fall_transition（使用fall的slew）
                    if (arc.fall_transition.has_value() && arc.fall_transition->template_name.has_value()) {
                        std::string template_name = arc.fall_transition->template_name.value();
                        const auto* templ = cell_library_->get_table_template(template_name);
                        if (templ && templ->variable_1.has_value() && templ->variable_2.has_value()) {
                            std::string var1 = templ->variable_1.value();
                            std::string var2 = templ->variable_2.value();
                            fall_transition_time = cell_library_->caculate_lookuptable(
                                arc.fall_transition.value(), input_slew_fall, load_cap, var1, var2);
                        }
                    }
                    
                    // 确定输出转换方向
                    // 由于同时计算rise和fall的转换时间，output_direction保持UNKNOWN
                    // 对于clock-to-Q，可以根据时序弧类型确定
                    TransitionDirection output_direction = TransitionDirection::UNKNOWN;
                    if (is_clock_to_q) {
                        if (arc.timing_type == celllib::TimingType::RISING_EDGE) {
                            output_direction = TransitionDirection::RISING;
                        } else if (arc.timing_type == celllib::TimingType::FALLING_EDGE) {
                            output_direction = TransitionDirection::FALLING;
                        }
                    }
                    // 对于组合逻辑，由于同时计算rise和fall，保持UNKNOWN

                    if(is_combinational) {
                        // 组合逻辑没那么复杂，仅仅更新fanout之内的delau就可以了
                        // 然后存储一下转换时间和方向
                        for(size_t i = 0; i < related_signals.size(); i++) {
                            SignalBit input_connect = sigmap.find(related_signals[i]);
                            SignalBit output_connect = sigmap.find(output_signals[i]);

                            if(timing_data.count(input_connect)) {
                                auto &fanouts = timing_data[input_connect].fanouts;
                                bool found = false;
                                for(auto &fanout : fanouts) {
                                    if(fanout.target_bit == output_connect &&
                                        fanout.cell == instance.get() &&
                                        fanout.port_name == related_pin_name) {
                                            fanout.delay = static_cast<int>(delay);
                                            found = true;
                                            // if (delay > 0) {
                                            //     std::cout << "  [DEBUG] Updated fanout delay: " 
                                            //               << delay << "ps for " 
                                            //               << input_connect.wire_name << " -> " 
                                            //               << output_connect.wire_name << std::endl;
                                            // }
                                            break;
                                        }
                                }
                                if (!found && delay > 0) {
                                    std::cerr << "  [WARNING] Could not find matching fanout for "
                                              << "instance " << instance->instance_name 
                                              << ", input " << related_pin_name 
                                              << " (port_name in fanout: ";
                                    for (const auto& f : fanouts) {
                                        std::cerr << f.port_name << " ";
                                    }
                                    std::cerr << "), output " << output_pin_name 
                                              << ", delay=" << delay << "ps" << std::endl;
                                }
                            }

                            // 存储转换时间和方向到输出信号的SignalTimingData
                            // 对于多输入门，应该使用延迟最大的路径的转换时间（更保守）
                            if (!timing_data.count(output_connect)) {
                                timing_data[output_connect] = SignalTimingData();
                            }
                           
                            // FIXME 这里存在一个设计逻辑上的bug
                            // 由于当前计算延迟如果大于等于最大路径，当更新转换时间的时候
                            // 如果时序路径本身也是违例的，那么这个条路径本也应该被记录
                            // 但是这样就会使报告存在一个问题，预计在0.0.3版本之中进行修复

                            // 查找所有输入路径到该输出的最大延迟
                            double max_delay_for_output = 0.0;
                            for (const auto& [src_bit, timing] : timing_data) {
                                for (const auto& f : timing.fanouts) {
                                    SignalBit f_target = sigmap.find(f.target_bit);
                                    if (f_target == output_connect && 
                                        f.cell == instance.get()) {
                                        max_delay_for_output = std::max(max_delay_for_output, static_cast<double>(f.delay));
                                    }
                                }
                            }
                            
                            // 如果当前计算的延迟大于等于最大延迟，更新转换时间
                            // 或者如果转换时间还未设置，则设置它
                            if (!timing_data[output_connect].rise_transition_time.has_value() || 
                                delay >= max_delay_for_output) {
                                timing_data[output_connect].rise_transition_time = rise_transition_time;
                                timing_data[output_connect].fall_transition_time = fall_transition_time;
                                timing_data[output_connect].transition_direction = output_direction;
                            }
                        }
                    } else {
                        // 时序逻辑，clock to q
                        for (size_t i = 0; i < related_signals.size() && i < output_signals.size(); ++i) {
                            SignalBit clock_connect = sigmap.find(related_signals[i]);
                            SignalBit output_connect = sigmap.find(output_signals[i]);
                            
                            // 更新fanout中的延迟
                            if (timing_data.count(clock_connect)) {
                                auto& fanouts = timing_data[clock_connect].fanouts;
                                for (auto& fanout : fanouts) {
                                    if (fanout.target_bit == output_connect && 
                                        fanout.cell == instance.get() &&
                                        fanout.port_name == related_pin_name) {
                                        fanout.delay = static_cast<int>(delay);
                                        break;
                                    }
                                }
                            }
                            
                            // 存储转换时间和方向到输出信号的SignalTimingData
                            if (!timing_data.count(output_connect)) {
                                timing_data[output_connect] = SignalTimingData();
                            }
                            timing_data[output_connect].rise_transition_time = rise_transition_time;
                            timing_data[output_connect].fall_transition_time = fall_transition_time;
                            timing_data[output_connect].transition_direction = output_direction;
                        }
                    }
                } 
            }
        }
    }

    void STAWorker::run() {
        while (!timing_queue.empty()) {
            SignalBit bit = timing_queue.front();
            timing_queue.pop_front();
            
            SignalBit canonical_bit = sigmap.find(bit);
            
            // 获取当前信号的arrival time（基于规范代表）
            // 如果不存在，说明这个信号还没有被初始化，跳过
            if (!arrival_time.count(canonical_bit)) {
                continue;
            }
            int src_arrival = arrival_time[canonical_bit];
            
            // 获取该信号的时序数据
            // 如果不存在，说明这个信号没有fanout，跳过
            if (!timing_data.count(canonical_bit)) {
                continue;
            }
            auto& timing = timing_data[canonical_bit];
            
            // 遍历所有fanout（fanout中的target_bit应该是规范代表）
            for (const auto& fanout : timing.fanouts) {
                SignalBit dst_canonical = sigmap.find(fanout.target_bit);  // 确保是规范代表
                int delay = static_cast<int>(fanout.delay);
                int new_arrival = src_arrival + delay;
                
                // 调试输出：显示传播信息
                // if (delay > 0) {
                //     std::cout << "  [DEBUG] Propagating: " << canonical_bit.wire_name 
                //               << "[" << canonical_bit.bit_offset << "] (arrival=" << src_arrival 
                //               << "ps) -> " << dst_canonical.wire_name << "[" 
                //               << dst_canonical.bit_offset << "] (delay=" << delay 
                //               << "ps, new_arrival=" << new_arrival << "ps)" << std::endl;
                // }
                
                // 初始化目标信号的arrival_time（如果不存在）
                if (!arrival_time.count(dst_canonical)) {
                    arrival_time[dst_canonical] = -1;  // 初始化为-1，表示未到达
                }
                
                // 更新arrival time（取最大值，基于规范代表）
                if (new_arrival > arrival_time[dst_canonical]) {
                    arrival_time[dst_canonical] = new_arrival;
                    
                    // 确保目标信号的时序数据存在
                    if (!timing_data.count(dst_canonical)) {
                        timing_data[dst_canonical] = SignalTimingData();
                    }
                    
                    // 记录backtrack信息用于关键路径追踪
                    timing_data[dst_canonical].backtrack = canonical_bit;
                    timing_data[dst_canonical].source_port = fanout.port_name;
                    timing_data[dst_canonical].driver = fanout.cell;
                    
                    // 将目标信号加入队列继续传播
                    timing_queue.push_back(dst_canonical);
                    
                    // 检查是否是endpoint（endpoint中的key也应该是规范代表）
                    if (endpoints.count(dst_canonical)) {
                        std::cout << "[DEBUG] start the endpoint caculate" << std::endl;
                        int required_time;
                        if(endpoints[dst_canonical].sink ==nullptr) {
                            required_time = 0;
                        } else {
                            std::cout << "[DEBUG] start the setup/holdon caculate" << std::endl;
                            // flip-flop d port, caculate the setup time by lookuptable
                            // 获取sink的module_name和端口名
                            std::string module_name = endpoints[dst_canonical].sink->module_name;
                            std::string port_name = endpoints[dst_canonical].port;
                            
                            std::cout << "\n[DEBUG] Setup/Hold Calculation for endpoint:\n";
                            std::cout << "  Signal: " << dst_canonical.wire_name << "[" << dst_canonical.bit_offset << "]\n";
                            std::cout << "  Cell: " << module_name << ", Port: " << port_name << "\n";
                            
                            // 从cell library获取对应的cell
                            const auto* cell = cell_library_->get_cell(module_name);
                            if (cell) {
                                const auto* pin = cell->get_pin(port_name);
                                if (pin) {
                                    // 获取data信号的transition time (constrained_pin_transition)
                                    double data_trans_rise = 0.0;
                                    double data_trans_fall = 0.0;
                                    if (timing_data.count(dst_canonical)) {
                                        const auto& data_timing = timing_data[dst_canonical];
                                        if (data_timing.rise_transition_time.has_value()) {
                                            data_trans_rise = data_timing.rise_transition_time.value();
                                        }
                                        if (data_timing.fall_transition_time.has_value()) {
                                            data_trans_fall = data_timing.fall_transition_time.value();
                                        }
                                    }
                                    
                                    // 获取clock的transition time (related_pin_transition)
                                    double clk_trans_rise = static_cast<double>(cfg.clock_transit_raise);
                                    double clk_trans_fall = static_cast<double>(cfg.clock_transit_fall);
                                    
                                    // 取rise和fall的最大值作为lookup参数
                                    double data_trans = std::max(data_trans_rise, data_trans_fall);
                                    double clk_trans = std::max(clk_trans_rise, clk_trans_fall);
                                    
                                    std::cout << "  Data transition: rise=" << data_trans_rise << "ps, fall=" << data_trans_fall << "ps, max=" << data_trans << "ps\n";
                                    std::cout << "  Clock transition: rise=" << clk_trans_rise << "ps, fall=" << clk_trans_fall << "ps, max=" << clk_trans << "ps\n";
                                    
                                    // 遍历timing arcs查找setup/hold约束
                                    for (const auto& arc : pin->timing_arcs) {
                                        // Setup约束计算
                                        if (arc.timing_type == celllib::TimingType::SETUP_RISING || 
                                            arc.timing_type == celllib::TimingType::SETUP_FALLING) {
                                            double setup_rise = 0.0;
                                            double setup_fall = 0.0;
                                            
                                            std::cout << "  [SETUP] Found setup timing arc (related_pin: " << arc.related_pin << ")\n";
                                            
                                            // 计算rise_constraint (data rising edge)
                                            if (arc.rise_constraint.has_value() && arc.rise_constraint->template_name.has_value()) {
                                                std::string template_name = arc.rise_constraint->template_name.value();
                                                const auto* templ = cell_library_->get_table_template(template_name);
                                                if (templ && templ->variable_1.has_value() && templ->variable_2.has_value()) {
                                                    std::string var1 = templ->variable_1.value();
                                                    std::string var2 = templ->variable_2.value();
                                                    setup_rise = cell_library_->caculate_lookuptable(
                                                        arc.rise_constraint.value(), data_trans, clk_trans, var1, var2);
                                                    std::cout << "    rise_constraint: template=" << template_name 
                                                              << ", var1=" << var1 << ", var2=" << var2 
                                                              << " => setup_rise=" << setup_rise << "ps\n";
                                                }
                                            } else {
                                                std::cout << "    rise_constraint: NOT AVAILABLE\n";
                                            }
                                            
                                            // 计算fall_constraint (data falling edge)
                                            if (arc.fall_constraint.has_value() && arc.fall_constraint->template_name.has_value()) {
                                                std::string template_name = arc.fall_constraint->template_name.value();
                                                const auto* templ = cell_library_->get_table_template(template_name);
                                                if (templ && templ->variable_1.has_value() && templ->variable_2.has_value()) {
                                                    std::string var1 = templ->variable_1.value();
                                                    std::string var2 = templ->variable_2.value();
                                                    setup_fall = cell_library_->caculate_lookuptable(
                                                        arc.fall_constraint.value(), data_trans, clk_trans, var1, var2);
                                                    std::cout << "    fall_constraint: template=" << template_name 
                                                              << ", var1=" << var1 << ", var2=" << var2 
                                                              << " => setup_fall=" << setup_fall << "ps\n";
                                                }
                                            } else {
                                                std::cout << "    fall_constraint: NOT AVAILABLE\n";
                                            }
                                            
                                            // 取最大值作为setup time
                                            int setup_time = static_cast<int>(std::max(setup_rise, setup_fall));
                                            endpoints[dst_canonical].Setup_req = setup_time;
                                            std::cout << "    => Final Setup Time: " << setup_time << "ps (max of rise=" << setup_rise << ", fall=" << setup_fall << ")\n";
                                        }
                                        
                                        // Hold约束计算
                                        if (arc.timing_type == celllib::TimingType::HOLD_RISING || 
                                            arc.timing_type == celllib::TimingType::HOLD_FALLING) {
                                            double hold_rise = 0.0;
                                            double hold_fall = 0.0;
                                            
                                            std::cout << "  [HOLD] Found hold timing arc (related_pin: " << arc.related_pin << ")\n";
                                            
                                            // 计算rise_constraint (data rising edge)
                                            if (arc.rise_constraint.has_value() && arc.rise_constraint->template_name.has_value()) {
                                                std::string template_name = arc.rise_constraint->template_name.value();
                                                const auto* templ = cell_library_->get_table_template(template_name);
                                                if (templ && templ->variable_1.has_value() && templ->variable_2.has_value()) {
                                                    std::string var1 = templ->variable_1.value();
                                                    std::string var2 = templ->variable_2.value();
                                                    hold_rise = cell_library_->caculate_lookuptable(
                                                        arc.rise_constraint.value(), data_trans, clk_trans, var1, var2);
                                                    std::cout << "    rise_constraint: template=" << template_name 
                                                              << ", var1=" << var1 << ", var2=" << var2 
                                                              << " => hold_rise=" << hold_rise << "ps\n";
                                                }
                                            } else {
                                                std::cout << "    rise_constraint: NOT AVAILABLE\n";
                                            }
                                            
                                            // 计算fall_constraint (data falling edge)
                                            if (arc.fall_constraint.has_value() && arc.fall_constraint->template_name.has_value()) {
                                                std::string template_name = arc.fall_constraint->template_name.value();
                                                const auto* templ = cell_library_->get_table_template(template_name);
                                                if (templ && templ->variable_1.has_value() && templ->variable_2.has_value()) {
                                                    std::string var1 = templ->variable_1.value();
                                                    std::string var2 = templ->variable_2.value();
                                                    hold_fall = cell_library_->caculate_lookuptable(
                                                        arc.fall_constraint.value(), data_trans, clk_trans, var1, var2);
                                                    std::cout << "    fall_constraint: template=" << template_name 
                                                              << ", var1=" << var1 << ", var2=" << var2 
                                                              << " => hold_fall=" << hold_fall << "ps\n";
                                                }
                                            } else {
                                                std::cout << "    fall_constraint: NOT AVAILABLE\n";
                                            }
                                            
                                            // 取最大值作为hold time (注意hold可能为负数)
                                            int hold_time = static_cast<int>(std::max(hold_rise, hold_fall));
                                            endpoints[dst_canonical].Hold_req = hold_time;
                                            std::cout << "    => Final Hold Time: " << hold_time << "ps (max of rise=" << hold_rise << ", fall=" << hold_fall << ")\n";
                                        }
                                    }
                                    
                                    // 使用setup_time作为required_time
                                    if (endpoints[dst_canonical].Setup_req.has_value()) {
                                        required_time = endpoints[dst_canonical].Setup_req.value();
                                        std::cout << "  => Required Time (Setup): " << required_time << "ps\n";
                                    } else {
                                        required_time = 0;
                                        std::cout << "  => Required Time: 0ps (no setup constraint found)\n";
                                    }
                                } else {
                                    required_time = 0;
                                }
                            } else {
                                required_time = 0;
                            }
                        }
                        int total_time = new_arrival + required_time;
                        // 只有源信号被驱动时才更新max_arrival_time
                        if (total_time > max_arrival_time && driven_signals.count(canonical_bit)) {
                            max_arrival_time = total_time;
                            critical_signal = dst_canonical;
                        }
                    }
                }
            }
        }
    }

    void STAWorker::sta_check(int clock_period) {
        // 更新配置中的时钟周期（如果传入）
        if (clock_period > 0) {
            cfg.clk_period = clock_period;
        }
        
        // 计算有效时钟周期（考虑 uncertainty 等时序参数）
        int effective_period = get_effective_clock_period();
        
        // 如果最大的路径时序长度小于有效时钟周期，那么直接返回没有时序违规
        if (max_arrival_time <= effective_period) {
            std::cout << "\n✓ No timing violations found.\n";
            std::cout << "  Max arrival time: " << max_arrival_time << "ps\n";
            std::cout << "  Clock period: " << cfg.clk_period << "ps";
            if (cfg.clock_uncertain > 0) {
                std::cout << " (effective: " << effective_period << "ps after uncertainty)";
            }
            std::cout << "\n";
            std::cout << "  Slack: " << (effective_period - max_arrival_time) << "ps\n";
            return;
        }
        
        // 否则找到所有大于有效时钟周期的路径（时序违规）
        std::cout << "\n⚠ Timing violations detected!\n";
        std::cout << "  Clock period: " << cfg.clk_period << "ps";
        if (cfg.clock_uncertain > 0) {
            std::cout << " (effective: " << effective_period << "ps after uncertainty)";
        }
        std::cout << "\n";
        std::cout << "  Max arrival time: " << max_arrival_time << "ps\n";
        std::cout << "  Worst slack: " << (effective_period - max_arrival_time) << "ps\n\n";
        
        std::cout << "Violating paths:\n";
        std::cout << "----------------------------------------\n";
        
        int violation_count = 0;
        for (const auto& [bit, endpoint] : endpoints) {
            SignalBit canonical = sigmap.find(bit);
            
            // 只检查有 arrival time 的 endpoint
            if (!arrival_time.count(canonical)) {
                continue;
            }
            
            int arrival = arrival_time.at(canonical);
            int setup_time = endpoint.Setup_req.value();
            // 计算考虑所有时序参数后的 data required time
            int data_required_time = calculate_data_required_time(setup_time);
            
            // 检查是否超过有效时钟周期（使用 data_required_time 进行比较）
            if (arrival > data_required_time) {
                violation_count++;
                int slack = data_required_time - arrival;
                
                std::cout << "\n[" << violation_count << "] Violation at: " 
                          << canonical.wire_name << "[" << canonical.bit_offset << "]\n";
                std::cout << "  Arrival time: " << arrival << "ps\n";
                std::cout << "  Setup time: " << setup_time << "ps\n";
                std::cout << "  Data required time: " << data_required_time << "ps";
                if (cfg.clock_uncertain > 0) {
                    std::cout << " (clock period " << cfg.clk_period << "ps - setup " << setup_time 
                              << "ps - uncertainty " << cfg.clock_uncertain << "ps)";
                }
                std::cout << "\n";
                std::cout << "  Slack: " << slack << "ps (violation: " << (-slack) << "ps)\n";
                
                // 显示 endpoint 信息
                if (endpoint.sink) {
                    std::cout << "  Endpoint: " << endpoint.sink->instance_name 
                              << " (" << endpoint.sink->module_name << "." << endpoint.port << ")\n";
                } else {
                    std::cout << "  Endpoint: Top module output (" << endpoint.port << ")\n";
                }
                
                // 回溯并打印完整路径
                std::cout << "\n  Path trace (from input to endpoint):\n";
                trace_path(canonical);
            }
        }
        
        std::cout << "\n----------------------------------------\n";
        std::cout << "Total violations: " << violation_count << "\n";
        
        if (violation_count == 0) {
            std::cout << "Note: No violations found in endpoints, but max_arrival_time exceeds clock period.\n";
            std::cout << "This may indicate an issue with the critical path calculation.\n";
        }
    }

    void STAWorker::trace_path(const SignalBit& endpoint_bit) {
        // 从 endpoint 回溯到输入端口
        std::vector<std::pair<SignalBit, int>> path;  // (signal, arrival_time)
        SignalBit current = endpoint_bit;
        std::unordered_set<SignalBit, SignalBitHash> visited; 
        
        while (true) {
            SignalBit canonical = sigmap.find(current);
            
            // 防止循环
            if (visited.count(canonical)) {
                break;
            }
            visited.insert(canonical);
            
            // 获取 arrival time
            int arrival = -1;
            if (arrival_time.count(canonical)) {
                arrival = arrival_time.at(canonical);
            }
            
            path.push_back({canonical, arrival});
            
            // 检查是否是输入端口（在 driven_signals 中且 arrival_time 为 0）
            if (driven_signals.count(canonical) && arrival == 0) {
                // 输入端口或虚拟时钟，回溯结束
                break;
            }
            
            // 检查是否有 backtrack 信息
            if (!timing_data.count(canonical)) {
                // 没有时序数据，无法继续回溯
                break;
            }
            
            const auto& timing = timing_data.at(canonical);
            
            // 检查 backtrack 是否有效
            if (timing.backtrack.wire_name == "" || 
                (timing.backtrack.wire_name == canonical.wire_name && 
                 timing.backtrack.bit_offset == canonical.bit_offset)) {
                break;
            }
            
            // 继续回溯
            current = timing.backtrack;
        }
        
        // 反转路径（从输入到输出）
        std::reverse(path.begin(), path.end());
        
        // 打印路径
        int path_index = 0;
        for (size_t i = 0; i < path.size(); ++i) {
            const auto& [bit, arrival] = path[i];
            
            // 获取时序数据
            bool has_timing = timing_data.count(bit);
            const auto* timing = has_timing ? &timing_data.at(bit) : nullptr;
            
            std::cout << "    [" << path_index++ << "] ";
            
            // 打印信号信息
            std::cout << bit.wire_name << "[" << bit.bit_offset << "]";
            if (arrival >= 0) {
                std::cout << " (arrival: " << arrival << "ps)";
            }
            
            // 如果是输入端口（arrival_time 为 0 且在 driven_signals 中）
            if (driven_signals.count(bit) && arrival == 0) {
                if (bit.wire_name == "__clk__") {
                    std::cout << " [Virtual Clock]\n";
                } else {
                    std::cout << " [Primary Input]\n";
                }
                continue;
            }
            
            // 如果有驱动单元
            if (timing && timing->driver) {
                std::cout << "\n        → ";
                std::cout << timing->driver->instance_name 
                          << " (" << timing->driver->module_name << ")";
                if (!timing->source_port.empty()) {
                    std::cout << " via port " << timing->source_port;
                }
                std::cout << "\n";
            }
            
            // 如果是最后一个节点（endpoint）
            if (i == path.size() - 1) {
                std::cout << " [Endpoint]\n";
            } else {
                std::cout << "\n";
            }
        }
    }

}