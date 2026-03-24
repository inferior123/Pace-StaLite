create_clock -name config_clk -period 10 -waveform {0 5} [get_ports {config_clk}]

set_propagated_clock [get_clocks {config_clk}]

set_input_delay  0 -clock clk [get_ports {config_en}]
set_output_delay 0 -clock clk [get_ports {out}]
