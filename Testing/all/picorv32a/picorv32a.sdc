create_clock -name clk -period 10 -waveform {0 5} [get_ports {clk}]

set_propagated_clock [get_clocks {clk}]

set_input_delay  0 -clock clk [get_ports {resetn mem_ready pcpi_wr pcpi_wait pcpi_ready}]
set_output_delay 0 -clock clk [get_ports {trap mem_valid mem_instr mem_la_read mem_la_write pcpi_valid trace_valid}]
