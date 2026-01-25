#include "sta_data_structures.hpp"
#include "verilog_data.hpp"
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

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

    // 获取标准单元的时序信息，不存在返回nullptr
    const std::optional<CellTiming> get_cell_timing(const std::string &module_name) {
        static std::unordered_map<std::string, CellTiming> cell_library;
    
        // 延迟初始化：只在第一次调用时初始化
        if (cell_library.empty()) {
            // AND2_X1: 2输入AND门
            CellTiming and2_timing;
            and2_timing.comb_delays[CellTiming::PortPair{"a", "o"}] = 100;  // 延迟单位：ps
            and2_timing.comb_delays[CellTiming::PortPair{"b", "o"}] = 100;
            cell_library["AND2_X1"] = std::move(and2_timing);
            
            // OR2_X1: 2输入OR门
            CellTiming or2_timing;
            or2_timing.comb_delays[CellTiming::PortPair{"a", "o"}] = 120;
            or2_timing.comb_delays[CellTiming::PortPair{"b", "o"}] = 120;
            cell_library["OR2_X1"] = std::move(or2_timing);
            
            // INV_X2: 反相器
            CellTiming inv_timing;
            inv_timing.comb_delays[CellTiming::PortPair{"a", "o"}] = 80;
            cell_library["INV_X2"] = std::move(inv_timing);
            
            // NOR2_X1: 2输入NOR门
            CellTiming nor2_timing;
            nor2_timing.comb_delays[CellTiming::PortPair{"a", "o"}] = 110;
            nor2_timing.comb_delays[CellTiming::PortPair{"b", "o"}] = 110;
            cell_library["NOR2_X1"] = std::move(nor2_timing);

            CellTiming reg;
            reg.comb_delays[CellTiming::PortPair{"clk", "q"}] = 100;
            // 时序延迟：q 端从时钟到输出的延迟（clock-to-output delay）
            // 格式：输出端口 -> (延迟, 时钟端口)
            reg.arrival_times["q"] = std::make_pair(200, "clk");
            // 时序约束：d 端的 setup time（相对于时钟）
            // 格式：输入端口 -> (约束值, 时钟端口)
            reg.required_times["d"] = std::make_pair(100, "clk");
            cell_library["REG"] = std::move(reg);
        }
        
        auto it = cell_library.find(module_name);
        if(it == cell_library.end()) {
            return std::nullopt;
        }
        return std::optional<CellTiming>{it->second};
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
            // 实例化一个寄存器接口，并将寄存器的q段标记为时序端点
            int left = net.beg == -1 ? 0 : net.beg;
            int right = net.end == -1 ? 0 : net.end;

            if (left > right) {
                assert(false && "unsupported net range: left > right (e.g. [7:0]); use ascending range like [0:7]");
            }

            for(auto &name : net.names) {
                std::string instance_name = "reg_" + name;
                auto reg_instance = std::make_unique<Instance>("REG", instance_name);

                std::vector<SignalBit> bits;
                auto q_name = "reg_" + name + "_q";
                for(int i = left; i <= right; i++) {
                    SignalBit bit(q_name, i);
                    bits.push_back(bit);
                    timing_data[bit] = SignalTimingData();
                }
                signal_registry[q_name] = std::move(bits);
                SignalSpec signal_q = get_signal_bits(q_name);

                // 创建 d 端信号（内部信号）
                // 获取 REG 的时序信息，用于设置 d 端的 required_time
                const auto reg_timing = get_cell_timing("REG");
                int setup_time = 0;
                if (reg_timing.has_value() && reg_timing->required_times.count("d")) {
                    setup_time = reg_timing->required_times.at("d").first;
                }
                
                std::vector<SignalBit> d_bits;
                auto d_name = "reg_" + name + "_d";
                for(int i = left; i <= right; i++) {
                    SignalBit d_bit(d_name, i);
                    if (!timing_data.count(d_bit)) {
                        timing_data[d_bit] = SignalTimingData();
                    }
                    d_bits.push_back(d_bit);
                    // 将 d 端标记为 endpoint，用于 setup time 检查
                    endpoints[d_bit] = TimingEndpoint(reg_instance.get(), "d", setup_time);
                }
                signal_registry[d_name] = std::move(d_bits);
                SignalSpec d_signals = get_signal_bits(d_name);

                reg_instance->connections["q"] = signal_q;
                reg_instance->connections["d"] = std::move(d_signals);

                instances.push_back(std::move(reg_instance));
            }
            
        } else {
            assert(false && "other type not implement yet");
        }
    }

    void STAWorker::collect_port(verilog::Port &port) {
        int left = port.beg == -1 ? 0 : port.beg;
        int right = port.end == -1 ? 0 : port.end;

        // For this prototype, only accept ascending ranges like [0:7].
        // Reject descending ranges like [7:0].
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
                
                // Always add to signal_registry (even if already existed)
                bits.push_back(bit);
                
                // Process based on port direction
                if(port.dir == PortDirection::INOUT) {
                    assert(false && "not suport the INOUT port yet");
                } else if(port.dir == PortDirection::INPUT) {
                    driven_signals.insert(bit);
                    timing_queue.push_back(bit);
                    arrival_time[bit] = 0; 
                } else {
                    // OUTPUT
                    assert(port.dir == PortDirection::OUTPUT && "invalid port dir");
                    endpoints[bit] = TimingEndpoint{nullptr, sig_name, 0};  // 标记为top_module的output
                }
            }
            signal_registry[sig_name] = std::move(bits);
        }
    }

    void STAWorker::collect_instance(verilog::Instance &inst) {
        const auto timing = get_cell_timing(inst.module_name);
        if(!timing) {
            assert(false && "module not found in standard cell library");
        }

        auto instance = std::make_unique<Instance>(inst.module_name, inst.inst_name);
        if(inst.pin_names.empty()) {
            // 位置连接：网表已展平，应该都是命名连接。不应该出现非命名连接
            assert(false && "Positional port connection not supported in flattened netlist");
        }

        // 线网链接： 将标准单元的引脚和wire相连接
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

    void STAWorker::build_fanouts() {
        static SignalBit global_clk("__clk__", 0);
        
        // 初始化虚拟时钟信号的时序数据（只需初始化一次）
        if (!timing_data.count(global_clk)) {
            timing_data[global_clk] = SignalTimingData();
            // 虚拟时钟信号的arrival_time默认为0（理想时钟树，无delay）
            arrival_time[global_clk] = 0;
        }
        
        for(auto &instance : instances) {
            const auto timing = get_cell_timing(instance->module_name);
            if(!timing.has_value()) {
                assert(false && "invalid instance, cannot find in the standard cell lib");
            }

            if(instance->connections["q"].size() != instance->connections["d"].size()) {
                assert(false && "the num of connection of d pins unequ to connections of q pins");
            }
            
            // 处理 REG 类型：建立从时钟到 q 端的路径
            if (instance->module_name == "REG") {
                // 检查是否有 arrival_times["q"]（clock-to-output delay）
                if (timing->arrival_times.count("q")) {
                    const auto& arr_info = timing->arrival_times.at("q");
                    int delay = arr_info.first;
                    
                    // 获取 q 端的连接
                    if (!instance->connections.count("q")) {
                        continue;
                    }
                    SignalSpec q_signals = instance->connections["q"];
                    
                    // 处理每一位 q 端信号
                    for (size_t i = 0; i < q_signals.size(); ++i) {
                        SignalBit q_canonical = sigmap.find(q_signals[i]);
                        
                        // 确保 q 端信号在 timing_data 中
                        if (!timing_data.count(q_canonical)) {
                            timing_data[q_canonical] = SignalTimingData();
                        }
                        
                        // 建立从虚拟时钟到 q 的 fanout（表示REG默认与时钟相连）
                        timing_data[global_clk].fanouts.emplace_back(
                            q_canonical, delay, "clk", instance.get()
                        );
                        
                        // 标记 q 端为已驱动
                        driven_signals.insert(q_canonical);
                    }
                }
                continue;
            }
            
            // 处理组合逻辑标准单元
            if (timing->comb_delays.empty()) {
                assert(false && "invalid instance, cannot find comb_delays in the standard cell lib");
            }
            
            for(const auto &[port_pair, delay] : timing->comb_delays) {
                const std::string &input_port = port_pair.src;
                const std::string &output_port = port_pair.dst;

                if(!instance->connections.count(output_port)) {
                    continue;
                }
                SignalSpec input_signals = instance->connections[input_port];

                if(!instance->connections.count(output_port)) {
                    continue;
                }
                SignalSpec output_signals = instance->connections[output_port];

                for(size_t i=0;i<input_signals.size() && i < output_signals.size(); i++) {
                    SignalBit input_connect = sigmap.find(input_signals[i]);
                    SignalBit output_connect = sigmap.find(output_signals[i]);

                    // 确保这个instance的时序弧被记录
                    if(!timing_data.count(input_connect)) {
                        timing_data[input_connect] = SignalTimingData();
                    }

                    if(!timing_data.count(output_connect)) {
                        timing_data[output_connect] = SignalTimingData();
                    }

                    timing_data[input_connect].fanouts.emplace_back(
                        output_connect, delay, input_port, instance.get()
                    );
                    
                    // 将这个output标记为已驱动
                    driven_signals.insert(output_connect);
                }
            }
        }
    }


}