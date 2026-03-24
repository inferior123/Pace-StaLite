create_clock -name clk -period 10 -waveform {0 5} [get_ports {clk}]

set_propagated_clock [get_clocks {clk}]

set_input_delay  0 -clock clk [get_ports {rst_n uart_rx jtag_tck_pin jtag_tms_pin jtag_tdi_pin}]
set_output_delay 0 -clock clk [get_ports {uart_tx jtag_tdo_pin}]
