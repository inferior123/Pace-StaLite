create_clock -name clk -period 10 -waveform {0 5} [get_ports {clk}]

set_propagated_clock [get_clocks {clk}]

set_input_delay  0 -clock clk [get_ports {rst_n \\parallel_data_in[0]  \\parallel_data_in[1]  \\parallel_data_in[2]  \\parallel_data_in[3]  \\parallel_data_in[4]  \\parallel_data_in[5]  \\parallel_data_in[6]  \\parallel_data_in[7] }]
set_output_delay 0 -clock clk [get_ports {\\parallel_data_out[0]  \\parallel_data_out[1]  \\parallel_data_out[2]  \\parallel_data_out[3]  \\parallel_data_out[4]  \\parallel_data_out[5]  \\parallel_data_out[6]  \\parallel_data_out[7] }]
