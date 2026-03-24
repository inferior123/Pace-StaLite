create_clock -name clk -period 10 -waveform {0 5} [get_ports {clk}]

set_propagated_clock [get_clocks {clk}]

set_input_delay  0 -clock clk [get_ports {rst s_axis_tvalid m_axis_tready rxd}]
set_output_delay 0 -clock clk [get_ports {s_axis_tready m_axis_tvalid txd tx_busy rx_busy rx_overrun_error rx_frame_error}]
