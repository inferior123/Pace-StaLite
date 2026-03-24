create_clock -name clk -period 10 -waveform {0 5} [get_ports {clk}]

set_propagated_clock [get_clocks {clk}]

set_input_delay  0 -clock clk [get_ports {f_ack g_ack rst}]
set_output_delay 0 -clock clk [get_ports {f_stb f_wre g_stb g_wre}]
