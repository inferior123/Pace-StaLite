create_clock -name raw_clk -period 10 -waveform {0 5} [get_ports {raw_clk}]

set_propagated_clock [get_clocks {raw_clk}]

set_input_delay  0 -clock clk [get_ports {start width_16 miso}]
set_output_delay 0 -clock clk [get_ports {busy sclk mosi}]
