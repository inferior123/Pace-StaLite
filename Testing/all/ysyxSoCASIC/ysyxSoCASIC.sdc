create_clock -name clock -period 10 -waveform {0 5} [get_ports {clock}]

set_propagated_clock [get_clocks {clock}]

set_input_delay  0 -clock clk [get_ports {reset uart0_rx spi_miso core_irq}]
set_output_delay 0 -clock clk [get_ports {uart0_tx spi_sck spi_mosi}]
