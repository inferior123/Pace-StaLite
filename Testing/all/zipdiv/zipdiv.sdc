create_clock -name i_clk -period 10 -waveform {0 5} [get_ports {i_clk}]

set_propagated_clock [get_clocks {i_clk}]

set_input_delay  0 -clock clk [get_ports {i_reset i_signed i_wr}]
set_output_delay 0 -clock clk [get_ports {o_err o_valid o_busy}]
