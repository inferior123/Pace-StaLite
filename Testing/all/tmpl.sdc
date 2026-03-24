create_clock -name $TMPL_CLK_NAME -period 10 -waveform {0 5} [get_ports {$TMPL_CLK_NAME}]

set_propagated_clock [get_clocks {$TMPL_CLK_NAME}]

set_input_delay  0 -clock clk [get_ports {$TMPL_INPUT_LIST}]
set_output_delay 0 -clock clk [get_ports {$TMPL_OUTPUT_LIST}]
