create_clock -name clk -period 10 -waveform {0 5} [get_ports {clk}]

set_propagated_clock [get_clocks {clk}]

set_input_delay  0 -clock clk [get_ports {rd_en wr_en rst}]
set_output_delay 0 -clock clk [get_ports {buf_empty buf_full}]
