#include "sta/sta_data_structures.hpp"
#include "parser-verilog/verilog_data.hpp"
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>
#include <map>
#include <iomanip>
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
            reg.arrival_times["q"] = std::make_pair(20, "clk");
            // 时序约束：d 端的 setup time（相对于时钟）
            // 格式：输入端口 -> (约束值, 时钟端口)
            reg.required_times["d"] = std::make_pair(10, "clk");
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
                    // 使用规范代表作为 key（sigmap.find）
                    SignalBit d_canonical = sigmap.find(d_bit);
                    endpoints[d_canonical] = TimingEndpoint(reg_instance.get(), "d", setup_time);
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
                    // 使用规范代表（sigmap.find）
                    SignalBit input_canonical = sigmap.find(bit);
                    driven_signals.insert(input_canonical);
                    timing_queue.push_back(input_canonical);
                    arrival_time[input_canonical] = 0; 
                } else {
                    // OUTPUT
                    assert(port.dir == PortDirection::OUTPUT && "invalid port dir");
                    // 使用规范代表作为 key（sigmap.find）
                    SignalBit output_canonical = sigmap.find(bit);
                    endpoints[output_canonical] = TimingEndpoint{nullptr, sig_name, 0};  // 标记为top_module的output
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
        
        // 重新规范化所有 endpoints 的 key（因为 assign 语句可能改变了规范代表）
        std::unordered_map<SignalBit, TimingEndpoint, SignalBitHash> normalized_endpoints;
        for (const auto& [bit, endpoint] : endpoints) {
            SignalBit canonical = sigmap.find(bit);
            normalized_endpoints[canonical] = endpoint;
        }
        endpoints = std::move(normalized_endpoints);
        
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
            const auto timing = get_cell_timing(instance->module_name);
            if(!timing.has_value()) {
                assert(false && "invalid instance, cannot find in the standard cell lib");
            }
            
            // 处理 REG 类型：建立从时钟到 q 端的路径
            if (instance->module_name == "REG") {
                // 检查 REG 的 q 和 d 端口连接数量是否匹配
                if(instance->connections.count("q") && instance->connections.count("d")) {
                    if(instance->connections["q"].size() != instance->connections["d"].size()) {
                        assert(false && "the num of connection of d pins unequ to connections of q pins");
                    }
                }
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

    void STAWorker::run() {
        while (!timing_queue.empty()) {
            SignalBit bit = timing_queue.front();
            timing_queue.pop_front();
            
            // 关键：获取规范代表（sigmap统一映射）
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
                int delay = fanout.delay;
                int new_arrival = src_arrival + delay;
                
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
                        int required_time = endpoints[dst_canonical].required_time;
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
            int setup_time = endpoint.required_time;
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
        std::unordered_set<SignalBit, SignalBitHash> visited;  // 防止循环
        
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

    void STAWorker::report(int clock_period) {
        std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
        std::cout << "║              Static Timing Analysis Report               ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
        
        std::cout << "Module: " << top_moudle << "\n";
        std::cout << "----------------------------------------\n\n";
        
        // 1. 显示关键路径信息
        if (critical_signal.wire_name == "") {
            std::cout << "No timing paths found.\n";
            return;
        }
        
        SignalBit canonical_critical = sigmap.find(critical_signal);
        
        std::cout << "Latest arrival time: " << max_arrival_time << "ps\n";
        
        // 显示关键 endpoint 信息
        if (endpoints.count(canonical_critical)) {
            const auto& endpoint = endpoints.at(canonical_critical);
            if (endpoint.sink) {
                std::cout << "  Critical endpoint: " << endpoint.sink->instance_name 
                          << " (" << endpoint.sink->module_name << "." << endpoint.port << ")\n";
            } else {
                std::cout << "  Critical endpoint: Top module output (" << endpoint.port << ")\n";
            }
        }
        
        // 2. 回溯并显示关键路径（参考 yosys 格式）
        std::cout << "\nCritical path:\n";
        std::cout << "----------------------------------------\n";
        
        SignalBit current = canonical_critical;
        std::vector<std::pair<SignalBit, int>> path_nodes;  // (signal, arrival_time)
        
        // 回溯路径
        while (true) {
            SignalBit canonical = sigmap.find(current);
            int arrival = -1;
            if (arrival_time.count(canonical)) {
                arrival = arrival_time.at(canonical);
            }
            path_nodes.push_back({canonical, arrival});
            
            // 检查是否是输入端口
            if (driven_signals.count(canonical) && arrival == 0) {
                break;
            }
            
            // 检查是否有 backtrack 信息
            if (!timing_data.count(canonical)) {
                break;
            }
            
            const auto& timing = timing_data.at(canonical);
            if (timing.backtrack.wire_name == "" || 
                (timing.backtrack.wire_name == canonical.wire_name && 
                 timing.backtrack.bit_offset == canonical.bit_offset)) {
                break;
            }
            
            current = timing.backtrack;
        }
        
        // 反转路径（从输入到输出）
        std::reverse(path_nodes.begin(), path_nodes.end());
        
        // 显示路径（参考 yosys 格式）
        for (size_t i = 0; i < path_nodes.size(); ++i) {
            const auto& [bit, arrival] = path_nodes[i];
            bool has_timing = timing_data.count(bit);
            const auto* timing = has_timing ? &timing_data.at(bit) : nullptr;
            
            if (i == path_nodes.size() - 1) {
                // 最后一个节点（endpoint）
                std::cout << "  " << std::setw(6) << arrival << "  " 
                          << bit.wire_name << "[" << bit.bit_offset << "]";
                if (endpoints.count(bit)) {
                    const auto& endpoint = endpoints.at(bit);
                    if (endpoint.sink) {
                        std::cout << " (" << endpoint.sink->instance_name 
                                  << "." << endpoint.port << ")";
                    } else {
                        std::cout << " (<primary output>)";
                    }
                }
                std::cout << "\n";
            } else if (timing && timing->driver) {
                // 有驱动单元的节点
                std::cout << "           " << bit.wire_name << "[" << bit.bit_offset << "]\n";
                std::cout << "  " << std::setw(6) << arrival << "  " 
                          << timing->driver->instance_name 
                          << " (" << timing->driver->module_name;
                if (!timing->source_port.empty()) {
                    std::cout << "." << timing->source_port;
                }
                if (timing->driver->connections.count("o") || timing->driver->connections.count("q")) {
                    std::string out_port = timing->driver->connections.count("o") ? "o" : "q";
                    std::cout << "->" << out_port;
                }
                std::cout << ")\n";
            } else if (driven_signals.count(bit) && arrival == 0) {
                // 输入端口
                std::cout << "  " << std::setw(6) << arrival << "  " 
                          << bit.wire_name << "[" << bit.bit_offset << "] "
                          << "(<primary input>)\n";
            }
        }
        
        // 3. 显示所有 endpoints 的时序信息（如果有时钟周期）
        if (clock_period > 0) {
            std::cout << "\n----------------------------------------\n";
            std::cout << "Endpoint timing summary:\n";
            std::cout << "----------------------------------------\n";
            
            std::map<int, unsigned> arrival_histogram;
            int violation_count = 0;
            
            for (const auto& [bit, endpoint] : endpoints) {
                SignalBit canonical = sigmap.find(bit);
                
                if (!arrival_time.count(canonical)) {
                    continue;
                }
                
                int arrival = arrival_time.at(canonical);
                int required = endpoint.required_time;
                int total_time = arrival + required;
                
                arrival_histogram[total_time]++;
                
                if (clock_period > 0 && total_time > clock_period) {
                    violation_count++;
                }
            }
            
            if (clock_period > 0) {
                std::cout << "Clock period: " << clock_period << "ps\n";
                std::cout << "Violations: " << violation_count << "\n";
                std::cout << "Max arrival time: " << max_arrival_time << "ps\n";
                std::cout << "Worst slack: " << (clock_period - max_arrival_time) << "ps\n";
            }
            
            // 4. 显示 arrival histogram（参考 yosys）
            if (arrival_histogram.size() > 0) {
                std::cout << "\n----------------------------------------\n";
                std::cout << "Arrival histogram:\n";
                std::cout << "----------------------------------------\n";
                
                unsigned num_bins = 20;
                unsigned bar_width = 60;
                auto min_arrival = arrival_histogram.begin()->first;
                auto max_arrival = arrival_histogram.rbegin()->first;
                
                if (min_arrival == max_arrival) {
                    std::cout << "All endpoints have the same arrival time: " << min_arrival << "ps\n";
                } else {
                    auto bin_size = std::max<unsigned>(1, (max_arrival - min_arrival + num_bins - 1) / num_bins);
                    std::vector<unsigned> bins(num_bins);
                    unsigned max_freq = 0;
                    
                    for (const auto& [time, count] : arrival_histogram) {
                        unsigned bin_idx = (time - min_arrival) / bin_size;
                        if (bin_idx >= num_bins) bin_idx = num_bins - 1;
                        bins[bin_idx] += count;
                        max_freq = std::max(max_freq, bins[bin_idx]);
                    }
                    
                    bar_width = std::min(bar_width, max_freq);
                    
                    if (max_freq > 0) {
                        std::cout << " legend: * represents " << std::max(1u, max_freq / bar_width) << " endpoint(s)\n";
                        std::cout << "         + represents [1," << std::max(1u, max_freq / bar_width) << ") endpoint(s)\n\n";
                        
                        for (int i = num_bins - 1; i >= 0; --i) {
                            int bin_start = min_arrival + bin_size * i;
                            int bin_end = min_arrival + bin_size * (i + 1);
                            unsigned bar_length = bins[i] * bar_width / max_freq;
                            char extra_char = (bins[i] * bar_width) % max_freq > 0 ? '+' : ' ';
                            
                            std::cout << "(" << std::setw(6) << bin_end << ", " 
                                      << std::setw(6) << bin_start << "] |"
                                      << std::string(bar_length, '*') << extra_char << "\n";
                        }
                    }
                }
            }
        }
        
        std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
        std::cout << "║              End of Report                               ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    }

}