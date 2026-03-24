create_clock -name clk_i -period 10 -waveform {0 5} [get_ports {clk_i}]

set_propagated_clock [get_clocks {clk_i}]

set_input_delay  0 -clock clk [get_ports {rst_i inport_awvalid_i inport_wvalid_i inport_wlast_i inport_bready_i inport_arvalid_i inport_rready_i}]
set_output_delay 0 -clock clk [get_ports {inport_awready_o inport_wready_o inport_bvalid_o inport_arready_o inport_rvalid_o inport_rlast_o sram_oe_n_o sram_cs_n_o sram_we_n_o}]
