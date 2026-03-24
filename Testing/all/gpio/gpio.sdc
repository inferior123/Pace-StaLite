create_clock -name clk_i -period 10 -waveform {0 5} [get_ports {clk_i}]

set_propagated_clock [get_clocks {clk_i}]

set_input_delay  0 -clock clk [get_ports {rst_i cfg_awvalid_i cfg_wvalid_i cfg_bready_i cfg_arvalid_i cfg_rready_i}]
set_output_delay 0 -clock clk [get_ports {cfg_awready_o cfg_wready_o cfg_bvalid_o cfg_arready_o cfg_rvalid_o intr_o}]
