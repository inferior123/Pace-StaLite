create_clock -name clk_i -period 10 -waveform {0 5} [get_ports {clk_i}]

set_propagated_clock [get_clocks {clk_i}]

set_input_delay  0 -clock clk [get_ports {rst_n_i ser_rx reg_dat_we reg_dat_re}]
set_output_delay 0 -clock clk [get_ports {ser_tx reg_dat_wait irq_out}]
