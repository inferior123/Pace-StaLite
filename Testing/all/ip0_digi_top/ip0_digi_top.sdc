create_clock -name sys_clk_i -period 10 -waveform {0 5} [get_ports {sys_clk_i}]

set_propagated_clock [get_clocks {sys_clk_i}]

set_input_delay  0 -clock clk [get_ports {TEST_PIN sys_por_n_i jtag_jtrstn jtag_jtck jtag_jtms jtag_jtdi uart_rx spi_miso int_n_i sram_wait}]
set_output_delay 0 -clock clk [get_ports {jtag_jtdo uart_tx spi_mcsn spi_mclk spi_mosi sram_csn sram_wen sram_oen}]
