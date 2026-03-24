create_clock -name clk -period 10 -waveform {0 5} [get_ports {clk}]

set_propagated_clock [get_clocks {clk}]

set_input_delay  0 -clock clk [get_ports {i_rst i_timer_irq i_rf_ready i_rdata0 i_rdata1 i_ibus_ack i_dbus_ack i_ext_ready}]
set_output_delay 0 -clock clk [get_ports {o_rf_rreq o_rf_wreq o_wen0 o_wen1 o_wdata0 o_wdata1 o_ibus_cyc o_dbus_we o_dbus_cyc o_mdu_valid}]
