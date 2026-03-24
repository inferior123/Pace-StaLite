create_clock -name clk -period 10 -waveform {0 5} [get_ports {clk}]

set_propagated_clock [get_clocks {clk}]

set_input_delay  0 -clock clk [get_ports {reset ce MW MR DmaAck}]
set_output_delay 0 -clock clk [get_ports {DmaReq odd_or_even IRQ}]
