create_clock -name clk -period 10 -waveform {0 5} [get_ports {clk}]

set_propagated_clock [get_clocks {clk}]

set_input_delay  0 -clock clk [get_ports {rst_n cfg_wr_en cfg_rd_en}]
set_output_delay 0 -clock clk [get_ports {halted}]
