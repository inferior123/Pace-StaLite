create_clock -name config_clk -period 10 -waveform {0 5} [get_ports {config_clk}]

set_propagated_clock [get_clocks {config_clk}]

set_input_delay  0 -clock config_clk [get_ports {\\addr[0]  \\addr[1]  \\addr[2]  \\addr[3]  \\addr[4]  \\addr[5]  \\addr[6]  config_en \\config_in[0]  \\config_in[1]  \\config_in[2]  \\config_in[3]  \\config_in[4]  \\config_in[5]  \\config_in[6]  \\config_in[7] }]
set_output_delay 0 -clock config_clk [get_ports {out \\config_out[0]  \\config_out[1]  \\config_out[2]  \\config_out[3]  \\config_out[4]  \\config_out[5]  \\config_out[6]  \\config_out[7] }]
