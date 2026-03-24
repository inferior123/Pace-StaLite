create_clock -name clk -period 10 -waveform {0 5} [get_ports {clk}]

set_propagated_clock [get_clocks {clk}]

set_input_delay  0 -clock clk [get_ports {reset \\red_in[0]  \\red_in[1]  \\red_in[2]  \\red_in[3]  \\red_in[4]  \\red_in[5]  \\red_in[6]  \\red_in[7]  \\green_in[0]  \\green_in[1]  \\green_in[2]  \\green_in[3]  \\green_in[4]  \\green_in[5]  \\green_in[6]  \\green_in[7]  \\blue_in[0]  \\blue_in[1]  \\blue_in[2]  \\blue_in[3]  \\blue_in[4]  \\blue_in[5]  \\blue_in[6]  \\blue_in[7] }]
set_output_delay 0 -clock clk [get_ports {data_request new_address \\address[0]  \\address[1]  DO}]
