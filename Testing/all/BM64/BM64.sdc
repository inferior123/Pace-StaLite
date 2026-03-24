create_clock -name Clk -period 10 -waveform {0 5} [get_ports {Clk}]

set_propagated_clock [get_clocks {Clk}]

set_input_delay  0 -clock clk [get_ports {Rst Ld}]
set_output_delay 0 -clock clk [get_ports {Valid}]
