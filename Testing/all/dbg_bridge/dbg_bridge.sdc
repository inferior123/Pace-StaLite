create_clock -name clk_i -period 10 -waveform {0 5} [get_ports {clk_i}]

set_propagated_clock [get_clocks {clk_i}]

set_input_delay  0 -clock clk [get_ports {rst_i uart_rxd_i mem_awready_i mem_wready_i mem_bvalid_i mem_arready_i mem_rvalid_i mem_rlast_i}]
set_output_delay 0 -clock clk [get_ports {uart_txd_o mem_awvalid_o mem_wvalid_o mem_wlast_o mem_bready_o mem_arvalid_o mem_rready_o}]
