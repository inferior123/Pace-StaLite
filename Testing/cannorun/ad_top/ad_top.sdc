create_clock -name  -period 10 -waveform {0 5} [get_ports {}]

set_propagated_clock [get_clocks {}]

set_input_delay  0 -clock clk [get_ports {clk rst_n busy fdata}]
set_output_delay 0 -clock clk [get_ports {cs rd cvtA cvtB range phy_rst vio}]
