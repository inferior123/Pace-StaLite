#include "cell/cell_data_structure.hpp"
#include "sta/sta_data_structures.hpp"
#include "parser-verilog/verilog_data.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>
#include <unordered_set>
#include <stack>
#include <queue>
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
                    TimingEndpoint ep{nullptr, sig_name};
                    ep.Setup_req = 0;
                    ep.Hold_req = 0;
                    endpoints[output_canonical].push_back(std::move(ep));
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
                        if (endpoints.count(input_connect)) {
                            for (const auto& existing : endpoints[input_connect]) {
                                if (existing.sink == nullptr) {
                                    ep.primary_output_port = existing.port;
                                    break;
                                }
                            }
                        }
                        // 避免重复：同一 (sink, port) 只添加一次
                        auto& eps = endpoints[input_connect];
                        bool is_dup = std::any_of(eps.begin(), eps.end(), [&ep](const TimingEndpoint& e) {
                            return e.sink == ep.sink && e.port == ep.port;
                        });
                        if (!is_dup) {
                            eps.push_back(std::move(ep));
                        }
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
                            // 这个问题通过使用 dfs_run 解决了，但是依靠纯的拓扑排序始终没办法解决这个问题
                            // 本质原因是当我丢弃一个节点的时候就必然会丢弃其信息

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

    int STAWorker::process_endpoint_timing(TimingEndpoint& ep, const SignalBit& dst_canonical) {
        if (ep.sink == nullptr) return 0;
        const auto* cell = cell_library_->get_cell(ep.sink->module_name);
        if (!cell) return 0;
        const auto* pin = cell->get_pin(ep.port);
        if (!pin) return 0;

        double data_trans_rise = 0.0, data_trans_fall = 0.0;
        if (timing_data.count(dst_canonical)) {
            const auto& dt = timing_data.at(dst_canonical);
            if (dt.rise_transition_time.has_value()) data_trans_rise = dt.rise_transition_time.value();
            if (dt.fall_transition_time.has_value()) data_trans_fall = dt.fall_transition_time.value();
        }
        double data_trans = std::max(data_trans_rise, data_trans_fall);
        double clk_trans = std::max(static_cast<double>(cfg.clock_transit_raise),
                                    static_cast<double>(cfg.clock_transit_fall));

        for (const auto& arc : pin->timing_arcs) {
            if (arc.timing_type == celllib::TimingType::SETUP_RISING ||
                arc.timing_type == celllib::TimingType::SETUP_FALLING) {
                double setup_rise = 0.0, setup_fall = 0.0;
                if (arc.rise_constraint.has_value() && arc.rise_constraint->template_name.has_value()) {
                    const auto* t = cell_library_->get_table_template(arc.rise_constraint->template_name.value());
                    if (t && t->variable_1.has_value() && t->variable_2.has_value())
                        setup_rise = cell_library_->caculate_lookuptable(arc.rise_constraint.value(),
                            data_trans, clk_trans, t->variable_1.value(), t->variable_2.value());
                }
                if (arc.fall_constraint.has_value() && arc.fall_constraint->template_name.has_value()) {
                    const auto* t = cell_library_->get_table_template(arc.fall_constraint->template_name.value());
                    if (t && t->variable_1.has_value() && t->variable_2.has_value())
                        setup_fall = cell_library_->caculate_lookuptable(arc.fall_constraint.value(),
                            data_trans, clk_trans, t->variable_1.value(), t->variable_2.value());
                }
                ep.Setup_req = static_cast<int>(std::max(setup_rise, setup_fall));
            }
            if (arc.timing_type == celllib::TimingType::HOLD_RISING ||
                arc.timing_type == celllib::TimingType::HOLD_FALLING) {
                double hold_rise = 0.0, hold_fall = 0.0;
                if (arc.rise_constraint.has_value() && arc.rise_constraint->template_name.has_value()) {
                    const auto* t = cell_library_->get_table_template(arc.rise_constraint->template_name.value());
                    if (t && t->variable_1.has_value() && t->variable_2.has_value())
                        hold_rise = cell_library_->caculate_lookuptable(arc.rise_constraint.value(),
                            data_trans, clk_trans, t->variable_1.value(), t->variable_2.value());
                }
                if (arc.fall_constraint.has_value() && arc.fall_constraint->template_name.has_value()) {
                    const auto* t = cell_library_->get_table_template(arc.fall_constraint->template_name.value());
                    if (t && t->variable_1.has_value() && t->variable_2.has_value())
                        hold_fall = cell_library_->caculate_lookuptable(arc.fall_constraint.value(),
                            data_trans, clk_trans, t->variable_1.value(), t->variable_2.value());
                }
                ep.Hold_req = static_cast<int>(std::max(hold_rise, hold_fall));
            }
        }
        return ep.Setup_req.value_or(0);
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
                        for (auto& ep : endpoints[dst_canonical]) {
                            int required_time = (ep.sink == nullptr) ? 0 : process_endpoint_timing(ep, dst_canonical);
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
    }

    void STAWorker::run_dfs() {
        max_arrival_time = 0;
        critical_signal = SignalBit();

        // 重置 arrival_time：仅保留 startpoint (driven_signals 且 arrival=0)，其余置 -1
        for (auto& [bit, t] : arrival_time) {
            if (!(driven_signals.count(bit) && t == 0))
                t = -1;
        }

        // 使用 stack 模拟 DFS：从 input 端口出发，沿 fanout 深度优先遍历
        // 栈中存储待处理的 SignalBit，处理顺序为 LIFO（深度优先）
        std::stack<SignalBit> dfs_stack;

        for (const auto& bit : timing_queue) {
            SignalBit canonical = sigmap.find(bit);
            if (arrival_time.count(canonical) && arrival_time[canonical] == 0)
                dfs_stack.push(canonical);
        }

        while (!dfs_stack.empty()) {
            SignalBit canonical_bit = dfs_stack.top();
            dfs_stack.pop();

            if (!arrival_time.count(canonical_bit) || arrival_time[canonical_bit] < 0)
                continue;
            int src_arrival = arrival_time[canonical_bit];

            if (!timing_data.count(canonical_bit))
                continue;
            auto& timing = timing_data[canonical_bit];

            for (const auto& fanout : timing.fanouts) {
                SignalBit dst_canonical = sigmap.find(fanout.target_bit);
                int delay = static_cast<int>(fanout.delay);
                int new_arrival = src_arrival + delay;

                if (!arrival_time.count(dst_canonical))
                    arrival_time[dst_canonical] = -1;

                if (new_arrival > arrival_time[dst_canonical]) {
                    arrival_time[dst_canonical] = new_arrival;
                    if (!timing_data.count(dst_canonical))
                        timing_data[dst_canonical] = SignalTimingData();
                    timing_data[dst_canonical].backtrack = canonical_bit;
                    timing_data[dst_canonical].source_port = fanout.port_name;
                    timing_data[dst_canonical].driver = fanout.cell;

                    dfs_stack.push(dst_canonical);

                    if (endpoints.count(dst_canonical)) {
                        for (auto& ep : endpoints[dst_canonical]) {
                            int required_time = (ep.sink == nullptr) ? 0 : process_endpoint_timing(ep, dst_canonical);
                            int total_time = new_arrival + required_time;
                            if (total_time > max_arrival_time && driven_signals.count(canonical_bit)) {
                                max_arrival_time = total_time;
                                critical_signal = dst_canonical;
                            }
                        }
                    }
                }
            }
        }

        if (max_arrival_time == 0 && critical_signal.wire_name.empty()) {
            for (const auto& [bit, eps] : endpoints) {
                SignalBit canonical = sigmap.find(bit);
                if (!arrival_time.count(canonical)) continue;
                int arr = arrival_time.at(canonical);
                for (const auto& ep : eps) {
                    int req = ep.Setup_req.value_or(0);
                    int total = arr + req;
                    if (total > max_arrival_time) {
                        max_arrival_time = total;
                        critical_signal = canonical;
                    }
                }
            }
        }
    

        // Fallback: 若 max_arrival_time 仍为 0 但存在 arrival > 0 的 endpoint
        // （例如纯组合电路、顶层输出路径因 driven_signals 检查未通过），则重新计算
        if (max_arrival_time == 0 && critical_signal.wire_name.empty()) {
            for (const auto& [bit, eps] : endpoints) {
                SignalBit canonical = sigmap.find(bit);
                if (!arrival_time.count(canonical)) continue;
                int arr = arrival_time.at(canonical);
                for (const auto& ep : eps) {
                    int req = ep.Setup_req.value_or(0);
                    int total = arr + req;
                    if (total > max_arrival_time) {
                        max_arrival_time = total;
                        critical_signal = canonical;
                    }
                }
            }
        }
    }

    void STAWorker::print_all_timing_paths_dfs() {
        struct PathNodeInfo {
            SignalBit signal;
            int arrival;
            Instance* driver;
            std::string port;
        };
        std::vector<PathNodeInfo> path;
        struct StackFrame { SignalBit signal; int path_arrival; size_t fanout_idx; };
        std::stack<StackFrame> stk;
        int path_count = 0;
        std::unordered_set<std::string> path_printed;  // 已打印路径的指纹，用于去重

        std::unordered_set<SignalBit, SignalBitHash> startpoints;
        for (const auto& bit : timing_queue) {
            SignalBit canonical = sigmap.find(bit);
            if (!driven_signals.count(canonical)) continue;
            if (arrival_time.count(canonical) && arrival_time.at(canonical) != 0) continue;
            startpoints.insert(canonical);
        }

        for (const SignalBit& canonical : startpoints) {
            stk.push({canonical, 0, 0});

            while (!stk.empty()) {
                StackFrame f = stk.top();
                SignalBit cur = sigmap.find(f.signal);
                int arr = f.path_arrival;

                if (f.fanout_idx == 0) {
                    path.push_back({cur, arr, nullptr, ""});
                    if (timing_data.count(cur)) {
                        path.back().driver = timing_data.at(cur).driver;
                        path.back().port = timing_data.at(cur).source_port;
                    }
                }

                if (endpoints.count(cur)) {
                    std::string fingerprint;
                    for (const auto& n : path) {
                        fingerprint += n.signal.wire_name + "[" + std::to_string(n.signal.bit_offset) + "]:" +
                                       std::to_string(n.arrival) + "->";
                    }
                    if (path_printed.insert(fingerprint).second) {
                        path_count++;
                        std::cout << "\n--- Path #" << path_count << " (arrival: " << arr << "ps) ---\n";
                        for (size_t i = 0; i < path.size(); ++i) {
                            const auto& n = path[i];
                            std::cout << "  [" << i << "] " << n.signal.wire_name << "[" << n.signal.bit_offset << "]";
                            std::cout << " (arrival: " << n.arrival << "ps)";
                            if (driven_signals.count(n.signal) && n.arrival == 0)
                                std::cout << (n.signal.wire_name == "__clk__" ? " [Clock]" : " [Primary Input]");
                            if (n.driver) {
                                std::cout << " -> " << n.driver->instance_name << "(" << n.driver->module_name << ")";
                                if (!n.port.empty()) std::cout << "/" << n.port;
                            }
                            if (i == path.size() - 1) std::cout << " [Endpoint]";
                            std::cout << "\n";
                        }
                    }
                }

                if (!timing_data.count(cur)) {
                    path.pop_back();
                    stk.pop();
                    continue;
                }
                const auto& timing = timing_data.at(cur);
                if (f.fanout_idx >= timing.fanouts.size()) {
                    path.pop_back();
                    stk.pop();
                    continue;
                }

                const auto& fanout = timing.fanouts[f.fanout_idx];
                SignalBit dst = sigmap.find(fanout.target_bit);
                // 跳过重复：若同一节点有多个 fanout 指向同一 dst（不同 port），避免重复枚举同一条路径
                bool dup = false;
                for (size_t j = 0; j < f.fanout_idx; ++j) {
                    if (sigmap.find(timing.fanouts[j].target_bit) == dst) {
                        dup = true;
                        break;
                    }
                }
                stk.pop();
                stk.push({f.signal, f.path_arrival, f.fanout_idx + 1});
                if (!dup) {
                    int delay = static_cast<int>(fanout.delay);
                    stk.push({dst, arr + delay, 0});
                }
            }
        }
        std::cout << "\nTotal paths found: " << path_count << "\n";
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
        for (const auto& [bit, eps] : endpoints) {
            SignalBit canonical = sigmap.find(bit);
            
            // 只检查有 arrival time 的 endpoint
            if (!arrival_time.count(canonical)) {
                continue;
            }
            
            int arrival = arrival_time.at(canonical);
            for (const auto& endpoint : eps) {
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