create_clock -name raw_clk -period 10 -waveform {0 5} [get_ports {raw_clk}]

set_propagated_clock [get_clocks {raw_clk}]

set_input_delay  0 -clock raw_clk [get_ports {start width_16 \\data_tx[0]  \\data_tx[1]  \\data_tx[2]  \\data_tx[3]  \\data_tx[4]  \\data_tx[5]  \\data_tx[6]  \\data_tx[7]  \\data_tx[8]  \\data_tx[9]  \\data_tx[10]  \\data_tx[11]  \\data_tx[12]  \\data_tx[13]  \\data_tx[14]  \\data_tx[15]  miso}]
set_output_delay 0 -clock raw_clk [get_ports {\\data_rx[0]  \\data_rx[1]  \\data_rx[2]  \\data_rx[3]  \\data_rx[4]  \\data_rx[5]  \\data_rx[6]  \\data_rx[7]  busy sclk mosi}]
