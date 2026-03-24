create_clock -name clk -period 10 -waveform {0 5} [get_ports {clk}]

set_propagated_clock [get_clocks {clk}]

set_input_delay  0 -clock clk [get_ports {rst cpu_en cpu_restart rom_vld ram_rd_vld}]
set_output_delay 0 -clock clk [get_ports {rom_en ram_rd_en_data ram_rd_en_sfr ram_rd_en_xdata ram_wr_en_data ram_wr_en_sfr ram_wr_en_xdata}]
