create_clock -name CK -period 10 -waveform {0 5} [get_ports {CK}]

set_propagated_clock [get_clocks {CK}]

set_input_delay  0 -clock CK [get_ports {CLR v0 v1 v2 v3 v4 v5 v6}]
set_output_delay 0 -clock CK [get_ports {v13_D_13 v13_D_12 v13_D_17 v13_D_7 v13_D_14 v13_D_24 v13_D_8 v13_D_10 v13_D_9 v13_D_15 v13_D_6 v13_D_23 v13_D_11 v13_D_18 v13_D_19 v13_D_22 v13_D_16 v13_D_21 v13_D_20}]
