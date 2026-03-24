create_clock -name clk_48 -period 10 -waveform {0 5} [get_ports {clk_48}]

set_propagated_clock [get_clocks {clk_48}]

set_input_delay  0 -clock clk [get_ports {rst_n rx_j rx_se0 data_toggle data_in_valid}]
set_output_delay 0 -clock clk [get_ports {tx_en tx_j tx_se0 usb_rst transaction_active direction_in setup data_strobe success}]
