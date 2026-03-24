create_clock -name wb_clk_i -period 10 -waveform {0 5} [get_ports {wb_clk_i}]

set_propagated_clock [get_clocks {wb_clk_i}]

set_input_delay  0 -clock clk [get_ports {wb_rst_i arst_i wb_we_i wb_stb_i wb_cyc_i scl_pad_i sda_pad_i}]
set_output_delay 0 -clock clk [get_ports {wb_ack_o wb_inta_o scl_pad_o scl_padoen_o sda_pad_o sda_padoen_o}]
