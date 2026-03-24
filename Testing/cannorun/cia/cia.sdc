create_clock -name E_CLK -period 10 -waveform {0 5} [get_ports {E_CLK}]

set_propagated_clock [get_clocks {E_CLK}]

set_input_delay  0 -clock clk [get_ports {RESET_n CS_n RW FLAG_n TICK}]
set_output_delay 0 -clock clk [get_ports {PC_n IRQ_n}]
