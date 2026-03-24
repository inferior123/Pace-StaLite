create_clock -name  -period 10 -waveform {0 5} [get_ports {}]

set_propagated_clock [get_clocks {}]

set_input_delay  0 -clock clk [get_ports {mpw_clk_i_pad mpw_rst_pad mpw_io_pad5 mpw_io_pad6 mpw_io_pad7 mpw_io_pad8 mpw_io_pad9}]
set_output_delay 0 -clock clk [get_ports {mpw_clk_o_pad mpw_io_pad0 mpw_io_pad1 mpw_io_pad2 mpw_io_pad3 mpw_io_pad4}]
