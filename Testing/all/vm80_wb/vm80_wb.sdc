create_clock -name wb_clk_i -period 10 -waveform {0 5} [get_ports {wb_clk_i}]

set_propagated_clock [get_clocks {wb_clk_i}]

set_input_delay  0 -clock clk [get_ports {wb_rst_i wb_ack_i vm_cena vm_irq}]
set_output_delay 0 -clock clk [get_ports {wb_cyc_o wb_we_o wb_stb_o vm_inte}]
