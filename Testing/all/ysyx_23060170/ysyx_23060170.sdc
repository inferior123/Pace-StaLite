create_clock -name clock -period 10 -waveform {0 5} [get_ports {clock}]

set_propagated_clock [get_clocks {clock}]

set_input_delay  0 -clock clk [get_ports {reset io_interrupt io_master_awready io_master_wready io_master_bvalid io_master_arready io_master_rvalid io_master_rlast io_slave_awvalid io_slave_wvalid io_slave_wlast io_slave_bready io_slave_arvalid io_slave_rready}]
set_output_delay 0 -clock clk [get_ports {io_master_awvalid io_master_wvalid io_master_wlast io_master_bready io_master_arvalid io_master_rready io_slave_awready io_slave_wready io_slave_bvalid io_slave_arready io_slave_rvalid io_slave_rlast}]
